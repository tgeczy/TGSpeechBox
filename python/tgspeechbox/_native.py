"""Finding and loading the engine's two shared libraries.

The wheel carries them in tgspeechbox/_libs; TGSB_LIB_DIR points elsewhere
(an in-repo build, for the tests).  Names differ by platform and by build
system: CMake builds speechPlayer / nvspFrontend, the Linux Makefile builds
libtgspeechbox.so / libtgsbFrontend.so.
"""
from __future__ import annotations

import ctypes
import os
import sys
from ctypes import c_char_p, c_double, c_int, c_uint, c_void_p

from ._structs import Frame, FrameEx, FrameExCallback, FrontendVoicingTone, VoicingTone

HERE = os.path.dirname(os.path.abspath(__file__))

_NAMES = {
    "dsp": {
        "win32": ["speechPlayer.dll"],
        "darwin": ["libspeechPlayer.dylib"],
        "linux": ["libspeechPlayer.so", "libtgspeechbox.so"],
    },
    "frontend": {
        "win32": ["nvspFrontend.dll"],
        "darwin": ["libnvspFrontend.dylib"],
        "linux": ["libnvspFrontend.so", "libtgsbFrontend.so"],
    },
}


def lib_dir() -> str:
    return os.environ.get("TGSB_LIB_DIR") or os.path.join(HERE, "_libs")


def packs_root() -> str:
    """The directory that contains packs/ (what the frontend is created with)."""
    return os.environ.get("TGSB_PACKS_ROOT") or HERE


def _platform() -> str:
    if sys.platform.startswith("win"):
        return "win32"
    if sys.platform == "darwin":
        return "darwin"
    return "linux"


def _find(kind: str) -> str:
    d = lib_dir()
    names = _NAMES[kind][_platform()]
    for n in names:
        p = os.path.join(d, n)
        if os.path.isfile(p):
            return p
    raise OSError("TGSpeechBox: none of %s found in %s" % (", ".join(names), d))


_dll_dir_handle = None


def _load(kind: str) -> ctypes.CDLL:
    global _dll_dir_handle
    path = _find(kind)
    if _platform() == "win32" and _dll_dir_handle is None and hasattr(os, "add_dll_directory"):
        _dll_dir_handle = os.add_dll_directory(os.path.dirname(path))
    return ctypes.CDLL(path)


_dsp = None
_frontend = None


def dsp() -> ctypes.CDLL:
    global _dsp
    if _dsp is None:
        d = _load("dsp")
        d.speechPlayer_initialize.argtypes = [c_int]
        d.speechPlayer_initialize.restype = c_void_p
        d.speechPlayer_terminate.argtypes = [c_void_p]
        d.speechPlayer_terminate.restype = None
        d.speechPlayer_queueFrameEx.argtypes = [c_void_p, ctypes.POINTER(Frame), ctypes.POINTER(FrameEx),
                                               c_uint, c_uint, c_uint, c_int, c_int]
        d.speechPlayer_queueFrameEx.restype = None
        d.speechPlayer_synthesize.argtypes = [c_void_p, c_uint, ctypes.POINTER(ctypes.c_short)]
        d.speechPlayer_synthesize.restype = c_int
        d.speechPlayer_setVoicingTone.argtypes = [c_void_p, ctypes.POINTER(VoicingTone)]
        d.speechPlayer_setVoicingTone.restype = None
        d.speechPlayer_getVoicingTone.argtypes = [c_void_p, ctypes.POINTER(VoicingTone)]
        d.speechPlayer_getVoicingTone.restype = None
        d.speechPlayer_setOutputGain.argtypes = [c_void_p, c_double]
        d.speechPlayer_setOutputGain.restype = None
        d.speechPlayer_getDspVersion.argtypes = []
        d.speechPlayer_getDspVersion.restype = c_uint
        _dsp = d
    return _dsp


def frontend() -> ctypes.CDLL:
    global _frontend
    if _frontend is None:
        f = _load("frontend")
        f.nvspFrontend_create.argtypes = [c_char_p]
        f.nvspFrontend_create.restype = c_void_p
        f.nvspFrontend_destroy.argtypes = [c_void_p]
        f.nvspFrontend_destroy.restype = None
        f.nvspFrontend_setLanguage.argtypes = [c_void_p, c_char_p]
        f.nvspFrontend_setLanguage.restype = c_int
        f.nvspFrontend_setVoiceProfile.argtypes = [c_void_p, c_char_p]
        f.nvspFrontend_setVoiceProfile.restype = c_int
        f.nvspFrontend_getVoiceProfileNames.argtypes = [c_void_p]
        f.nvspFrontend_getVoiceProfileNames.restype = c_char_p
        f.nvspFrontend_getVoicingTone.argtypes = [c_void_p, ctypes.POINTER(FrontendVoicingTone)]
        f.nvspFrontend_getVoicingTone.restype = c_int
        f.nvspFrontend_queueIPA_Ex.argtypes = [c_void_p, c_char_p, c_double, c_double, c_double,
                                              c_char_p, c_int, FrameExCallback, c_void_p]
        f.nvspFrontend_queueIPA_Ex.restype = c_int
        f.nvspFrontend_beginStream.argtypes = [c_void_p]
        f.nvspFrontend_beginStream.restype = None
        f.nvspFrontend_prepareText.argtypes = [c_void_p, c_char_p]
        f.nvspFrontend_prepareText.restype = c_void_p
        f.nvspFrontend_freeString.argtypes = [c_void_p]
        f.nvspFrontend_freeString.restype = None
        f.nvspFrontend_queryData.argtypes = [c_void_p, c_int, c_char_p, c_int, c_int]
        f.nvspFrontend_queryData.restype = c_void_p
        f.nvspFrontend_getLastError.argtypes = [c_void_p]
        f.nvspFrontend_getLastError.restype = c_char_p
        f.nvspFrontend_getABIVersion.argtypes = []
        f.nvspFrontend_getABIVersion.restype = c_int
        _frontend = f
    return _frontend
