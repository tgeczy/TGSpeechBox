###
# This file is a part of the NV Speech Player project.
# URL: https://bitbucket.org/nvaccess/speechplayer
# Copyright 2014 NV Access Limited.
# GNU GPL v2
#
# Modernization patch:
# - Explicit ctypes prototypes for all exported DLL functions.
# - terminate() method so NVDA can clean up deterministically.
# - queueFrame() accepts durations in milliseconds, converts to samples (DLL expects samples).
# - Supports both 32-bit and 64-bit NVDA by loading DLLs from ./x86 or ./x64.
# - setVoicingTone() for DSP-level voice quality adjustments.
###

from __future__ import annotations

import ctypes
from ctypes import (
    Structure,
    POINTER,
    byref,
    c_double,
    c_int,
    c_short,
    c_uint,
    c_void_p,
    cdll,
)
import os
from typing import Optional

from logHandler import log

speechPlayer_frameParam_t = c_double


class Frame(Structure):
    # Keep this field order exactly in sync with the C++ struct used to build speechPlayer.dll.
    # If you swap in an older DLL with a different struct layout, you'll get clicks/silence.
    _fields_ = [(name, speechPlayer_frameParam_t) for name in [
        "voicePitch",
        "vibratoPitchOffset",
        "vibratoSpeed",
        "voiceTurbulenceAmplitude",
        "glottalOpenQuotient",
        "voiceAmplitude",
        "aspirationAmplitude",
        "cf1", "cf2", "cf3", "cf4", "cf5", "cf6", "cfN0", "cfNP",
        "cb1", "cb2", "cb3", "cb4", "cb5", "cb6", "cbN0", "cbNP",
        "caNP",
        "fricationAmplitude",
        "pf1", "pf2", "pf3", "pf4", "pf5", "pf6",
        "pb1", "pb2", "pb3", "pb4", "pb5", "pb6",
        "pa1", "pa2", "pa3", "pa4", "pa5", "pa6",
        "parallelBypass",
        "preFormantGain",
        "outputGain",
        "endVoicePitch",
    ]]


class VoicingTone(Structure):
    """DSP-level voice quality parameters.
    
    These affect the wave generator's glottal pulse shape, voiced pre-emphasis,
    spectral tilt, and high-shelf EQ. Set once per voice change, not per-frame.
    
    This struct matches speechPlayer_voicingTone_t in voicingTone.h.
    """
    _fields_ = [
        ("voicingPeakPos", c_double),     # Glottal pulse peak position (0.85-0.95, default 0.91)
        ("voicedPreEmphA", c_double),     # Pre-emphasis coefficient (0.0-0.97, default 0.92)
        ("voicedPreEmphMix", c_double),   # Pre-emphasis mix (0.0-1.0, default 0.35)
        ("highShelfGainDb", c_double),    # High-shelf EQ gain in dB (default 4.0)
        ("highShelfFcHz", c_double),      # High-shelf corner frequency (default 2000.0)
        ("highShelfQ", c_double),         # High-shelf Q factor (default 0.7)
        ("voicedTiltDbPerOct", c_double), # Spectral tilt in dB/octave (default 0.0, negative = darker)
    ]
    
    @classmethod
    def defaults(cls) -> "VoicingTone":
        """Return a VoicingTone with default values matching the original DSP constants."""
        tone = cls()
        tone.voicingPeakPos = 0.91
        tone.voicedPreEmphA = 0.92
        tone.voicedPreEmphMix = 0.35
        tone.highShelfGainDb = 4.0
        tone.highShelfFcHz = 2000.0
        tone.highShelfQ = 0.7
        tone.voicedTiltDbPerOct = 0.0
        return tone


def _archFolderName() -> Optional[str]:
    """Return the subfolder name containing native DLLs for this Python process."""
    ptrSize = ctypes.sizeof(ctypes.c_void_p)
    if ptrSize == 4:
        return "x86"
    if ptrSize == 8:
        return "x64"
    return None


def getDllDir(baseDir: Optional[str] = None) -> str:
    """Return the directory that should contain speechPlayer.dll for this process."""
    here = baseDir or os.path.dirname(__file__)
    arch = _archFolderName()
    if arch:
        candidate = os.path.join(here, arch)
        if os.path.isdir(candidate):
            return candidate
    # Legacy layout: DLLs live next to this Python file.
    return here


# Exposed for other modules (e.g., __init__.py) to reuse if needed.
dllDir = getDllDir()
dllPath = os.path.join(dllDir, "speechPlayer.dll")


class SpeechPlayer(object):
    """Thin ctypes wrapper over speechPlayer.dll.

    queueFrame() expects minFrameDuration and fadeDuration in *milliseconds*.
    The DLL expects durations in *samples*.
    """

    def __init__(self, sampleRate: int):
        self.sampleRate = int(sampleRate)

        self._dllDirCookie = None
        # Python 3.8+ tightened Windows DLL search rules.
        # Ensure the folder containing speechPlayer.dll is on the DLL search path so its
        # dependencies can be found reliably.
        if hasattr(os, "add_dll_directory"):
            try:
                self._dllDirCookie = os.add_dll_directory(os.path.dirname(dllPath))
            except Exception:
                self._dllDirCookie = None

        self._dll = cdll.LoadLibrary(dllPath)
        self._setupPrototypes()

        self._speechHandle = self._dll.speechPlayer_initialize(self.sampleRate)
        if not self._speechHandle:
            raise RuntimeError("speechPlayer_initialize failed")

    def _setupPrototypes(self) -> None:
        # void* speechPlayer_initialize(int sampleRate);
        self._dll.speechPlayer_initialize.argtypes = (c_int,)
        self._dll.speechPlayer_initialize.restype = c_void_p

        # void speechPlayer_queueFrame(void* handle, Frame* frame, uint minSamples, uint fadeSamples,
        #                              int userIndex, bool purgeQueue);
        # Use c_int for purgeQueue (0/1) for ABI safety.
        self._dll.speechPlayer_queueFrame.argtypes = (
            c_void_p,
            POINTER(Frame),
            c_uint,
            c_uint,
            c_int,
            c_int,
        )
        self._dll.speechPlayer_queueFrame.restype = None

        # int speechPlayer_synthesize(void* handle, uint numSamples, short* out);
        self._dll.speechPlayer_synthesize.argtypes = (c_void_p, c_uint, POINTER(c_short))
        self._dll.speechPlayer_synthesize.restype = c_int

        # int speechPlayer_getLastIndex(void* handle);
        self._dll.speechPlayer_getLastIndex.argtypes = (c_void_p,)
        self._dll.speechPlayer_getLastIndex.restype = c_int

        # void speechPlayer_terminate(void* handle);
        self._dll.speechPlayer_terminate.argtypes = (c_void_p,)
        self._dll.speechPlayer_terminate.restype = None

        # Voicing tone API (optional - may not exist in older DLLs)
        self._hasVoicingToneApi = False
        try:
            # Check if the function exists by getting it explicitly
            # ctypes won't throw until you try to access a non-existent function
            _setTone = getattr(self._dll, "speechPlayer_setVoicingTone", None)
            _getTone = getattr(self._dll, "speechPlayer_getVoicingTone", None)
            
            if _setTone is not None and _getTone is not None:
                # void speechPlayer_setVoicingTone(void* handle, const VoicingTone* tone);
                self._dll.speechPlayer_setVoicingTone.argtypes = (c_void_p, POINTER(VoicingTone))
                self._dll.speechPlayer_setVoicingTone.restype = None
                
                # void speechPlayer_getVoicingTone(void* handle, VoicingTone* tone);
                self._dll.speechPlayer_getVoicingTone.argtypes = (c_void_p, POINTER(VoicingTone))
                self._dll.speechPlayer_getVoicingTone.restype = None
                
                self._hasVoicingToneApi = True
        except (AttributeError, OSError):
            # Older DLL without voicing tone support - that's fine
            pass

    def queueFrame(self, frame, minFrameDuration, fadeDuration, userIndex: int = -1, purgeQueue: bool = False) -> None:
        framePtr = byref(frame) if frame else None

        # Convert ms -> samples for the DLL.
        minSamples = int(float(minFrameDuration) * (self.sampleRate / 1000.0))
        fadeSamples = int(float(fadeDuration) * (self.sampleRate / 1000.0))

        if minSamples < 0:
            minSamples = 0
        if fadeSamples < 0:
            fadeSamples = 0

        self._dll.speechPlayer_queueFrame(
            self._speechHandle,
            framePtr,
            c_uint(minSamples),
            c_uint(fadeSamples),
            c_int(int(userIndex) if userIndex is not None else -1),
            c_int(1 if purgeQueue else 0),
        )

    def synthesize(self, numSamples: int):
        n = int(numSamples)
        if n <= 0:
            return None
        buf = (c_short * n)()
        res = self._dll.speechPlayer_synthesize(self._speechHandle, c_uint(n), buf)
        if res > 0:
            buf.length = min(int(res), n)
            return buf
        return None

    def getLastIndex(self) -> int:
        return int(self._dll.speechPlayer_getLastIndex(self._speechHandle))

    def hasVoicingToneSupport(self) -> bool:
        """Check if the DLL supports voicing tone adjustments."""
        return getattr(self, "_hasVoicingToneApi", False)

    def setVoicingTone(self, tone: Optional[VoicingTone]) -> bool:
        """Set DSP-level voice quality parameters.
        
        This affects the wave generator's glottal pulse shape, voiced pre-emphasis,
        and high-shelf EQ. Call this once when switching voices, not per-frame.
        
        Args:
            tone: VoicingTone struct with parameters, or None to reset to defaults.
            
        Returns:
            True on success, False if API not available.
        """
        if not getattr(self, "_hasVoicingToneApi", False):
            return False
        if not self._speechHandle:
            return False
        try:
            tonePtr = byref(tone) if tone else None
            self._dll.speechPlayer_setVoicingTone(self._speechHandle, tonePtr)
            return True
        except Exception:
            log.debug("nvSpeechPlayer: setVoicingTone failed", exc_info=True)
            return False

    def getVoicingTone(self) -> Optional[VoicingTone]:
        """Get current DSP-level voice quality parameters.
        
        Returns:
            VoicingTone struct with current values, or None if API not available.
        """
        if not getattr(self, "_hasVoicingToneApi", False):
            return None
        if not self._speechHandle:
            return None
        try:
            tone = VoicingTone()
            self._dll.speechPlayer_getVoicingTone(self._speechHandle, byref(tone))
            return tone
        except Exception:
            log.debug("nvSpeechPlayer: getVoicingTone failed", exc_info=True)
            return None

    def terminate(self) -> None:
        # First terminate the speech handle
        if getattr(self, "_speechHandle", None):
            try:
                self._dll.speechPlayer_terminate(self._speechHandle)
            except Exception:
                pass
            self._speechHandle = None

        # Close the DLL directory cookie (Python 3.8+)
        if getattr(self, "_dllDirCookie", None):
            try:
                self._dllDirCookie.close()
            except Exception:
                pass
            self._dllDirCookie = None

        # Unload the DLL so the file can be replaced/deleted
        # Import here to avoid circular import issues
        if self._dll:
            try:
                from ._dll_utils import freeDll
                freeDll(self._dll)
            except Exception:
                # Non-fatal - DLL will be unloaded when NVDA exits
                log.debug("nvSpeechPlayer: freeDll failed for speechPlayer.dll", exc_info=True)
        self._dll = None

    def __del__(self):
        try:
            self.terminate()
        except Exception:
            pass
