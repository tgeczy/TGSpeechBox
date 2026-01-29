# -*- coding: utf-8 -*-
"""Helpers for locating the correct DLL directory (x86 vs x64).

This module intentionally contains "infrastructure" code (PE header parsing)
so the main synth driver (``__init__.py``) stays focused on speech logic.
"""

from __future__ import annotations

import ctypes
import os
import struct
from typing import Optional

from logHandler import log


_PE_MACHINE_X86 = 0x014C
_PE_MACHINE_AMD64 = 0x8664


def _readPeMachine(path: str) -> Optional[int]:
    """Return the PE COFF Machine field for a DLL/EXE, or None on error."""
    try:
        with open(path, "rb") as f:
            if f.read(2) != b"MZ":
                return None
            f.seek(0x3C)
            data = f.read(4)
            if len(data) != 4:
                return None
            peOffset = struct.unpack("<I", data)[0]
            f.seek(peOffset)
            if f.read(4) != b"PE\0\0":
                return None
            data = f.read(2)
            if len(data) != 2:
                return None
            return struct.unpack("<H", data)[0]
    except (OSError, ValueError, struct.error):
        # This can happen when files are missing/corrupt. It is not fatal here;
        # the caller will try other candidates.
        log.debug("nvSpeechPlayer: could not read PE machine for %r", path, exc_info=True)
        return None


def _expectedPeMachine() -> Optional[int]:
    ptrSize = ctypes.sizeof(ctypes.c_void_p)
    if ptrSize == 4:
        return _PE_MACHINE_X86
    if ptrSize == 8:
        return _PE_MACHINE_AMD64
    return None


def _archFolderName() -> Optional[str]:
    ptrSize = ctypes.sizeof(ctypes.c_void_p)
    if ptrSize == 4:
        return "x86"
    if ptrSize == 8:
        return "x64"
    return None


def findDllDir(baseDir: str) -> Optional[str]:
    """Return directory containing speechPlayer.dll + nvspFrontend.dll for this process arch."""
    expectedMachine = _expectedPeMachine()
    archFolder = _archFolderName()

    candidates = []
    if archFolder:
        candidates.append(os.path.join(baseDir, archFolder))
    # Legacy flat layout (DLLs next to __init__.py)
    candidates.append(baseDir)

    for d in candidates:
        try:
            dsp = os.path.join(d, "speechPlayer.dll")
            fe = os.path.join(d, "nvspFrontend.dll")
            if not (os.path.isfile(dsp) and os.path.isfile(fe)):
                continue

            if expectedMachine is not None:
                if _readPeMachine(dsp) != expectedMachine:
                    continue
                if _readPeMachine(fe) != expectedMachine:
                    continue

            return d
        except OSError:
            # Non-fatal: try next candidate.
            log.debug("nvSpeechPlayer: error while probing DLL directory %r", d, exc_info=True)
            continue
        except Exception:
            # Keep this very defensive: we don't want DLL probing to crash NVDA.
            log.debug("nvSpeechPlayer: unexpected error while probing DLL directory %r", d, exc_info=True)
            continue

    return None


def freeDll(dllObj) -> bool:
    """Unload a ctypes DLL object using FreeLibrary.
    
    This releases the DLL file so it can be deleted or replaced.
    Must only be called after all handles/resources from the DLL are released.
    
    Args:
        dllObj: A ctypes CDLL or WinDLL object
        
    Returns:
        True if FreeLibrary succeeded, False otherwise
        
    Compatibility:
        Works on Python 3.7+ (NVDA 2023.2+) on Windows.
        On non-Windows or if FreeLibrary fails, returns False gracefully.
    """
    if dllObj is None:
        return False
    
    try:
        # Get the native handle from the ctypes DLL object
        dllHandle = getattr(dllObj, "_handle", None)
        if not dllHandle:
            return False
        
        # Import kernel32 FreeLibrary
        # Use windll.kernel32 which is always available on Windows
        kernel32 = ctypes.windll.kernel32
        
        # Set up FreeLibrary prototype
        # BOOL FreeLibrary(HMODULE hLibModule);
        # HMODULE is HANDLE which is void* - use c_void_p for maximum compatibility
        FreeLibrary = kernel32.FreeLibrary
        FreeLibrary.argtypes = [ctypes.c_void_p]
        FreeLibrary.restype = ctypes.c_int  # BOOL, but c_int is safer across versions
        
        result = FreeLibrary(dllHandle)
        return bool(result)
    except AttributeError:
        # windll not available (non-Windows)
        return False
    except OSError:
        # FreeLibrary call failed
        log.debug("nvSpeechPlayer: FreeLibrary OSError", exc_info=True)
        return False
    except Exception:
        # Unexpected error - log but don't crash
        log.debug("nvSpeechPlayer: freeDll unexpected error", exc_info=True)
        return False
