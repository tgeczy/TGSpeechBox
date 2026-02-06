# -*- coding: utf-8 -*-
"""NV Speech Player - Voice profile discovery and frame application.

This module contains:
- Voice profile discovery from phonemes.yaml (fallback if frontend doesn't support it)
- Voice-to-frame application for Python preset voices (Adam, Benjamin, etc.)

Note: Voicing tone parsing is now handled by the C++ frontend (ABI v2+).
"""

import os
from logHandler import log


def discoverVoiceProfiles(packsDir: str) -> list:
    """Discover voice profile names from phonemes.yaml.
    
    This is a fallback for older frontends that don't support getVoiceProfileNames().
    Modern frontends (ABI v2+) handle this directly.
    
    This is a minimal, tolerant parser that extracts profile names from
    the voiceProfiles: section. Unknown keys are ignored.
    
    Supports both nested format and dotted-key format:
      Nested:  female:
                 classScales:
      Dotted:  female.classScales.vowel.cf_mul: [...]
    
    Args:
        packsDir: Path to the packs directory containing phonemes.yaml
        
    Returns:
        List of profile names found
    """
    profiles = []
    seenProfiles = set()  # Avoid duplicates from dotted keys
    yamlPath = os.path.join(packsDir, "phonemes.yaml")
    
    try:
        if not os.path.isfile(yamlPath):
            return profiles
            
        with open(yamlPath, "r", encoding="utf-8") as f:
            lines = f.readlines()
        
        inVoiceProfiles = False
        baseIndent = None
        
        for line in lines:
            stripped = line.strip()
            
            if not stripped or stripped.startswith("#"):
                continue
            
            # Check for voiceProfiles: at column 0
            if line and not line[0].isspace() and stripped.startswith("voiceProfiles:"):
                inVoiceProfiles = True
                baseIndent = None
                continue
            
            if inVoiceProfiles:
                # Left the section (back to column 0)
                if line and not line[0].isspace():
                    inVoiceProfiles = False
                    continue
                
                indent = len(line) - len(line.lstrip())
                
                if baseIndent is None:
                    baseIndent = indent
                
                # Profile names are at base indent level and end with ':'
                if indent == baseIndent and ":" in stripped:
                    key = stripped.split(":")[0].strip()
                    if key and not key.startswith("#"):
                        # For dotted keys like "female.classScales.vowel.cf_mul",
                        # extract just the first part "female" as the profile name
                        if "." in key:
                            key = key.split(".")[0]
                        
                        # Only add if we haven't seen this profile yet
                        if key and key not in seenProfiles:
                            seenProfiles.add(key)
                            profiles.append(key)
        
    except Exception:
        log.debug("nvSpeechPlayer: discoverVoiceProfiles failed", exc_info=True)
    
    return profiles


def buildVoiceOps(voices: dict, frameFieldNames: set) -> dict:
    """Pre-calculate per-voice operations for fast application.
    
    This is used for Python preset voices (Adam, Benjamin, Robert, etc.)
    which use per-frame multipliers and overrides.
    
    Args:
        voices: Dict of voice name -> voice parameters
        frameFieldNames: Set of valid frame field names
        
    Returns:
        Dict of voice name -> (absOps tuple, mulOps tuple)
        where absOps are (paramName, value) for absolute overrides
        and mulOps are (paramName, multiplier) for multiplied values
    """
    voiceOps = {}
    for voiceName, voiceMap in voices.items():
        absOps = []
        mulOps = []
        for k, v in (voiceMap or {}).items():
            if not isinstance(k, str):
                continue
            if k.endswith("_mul"):
                param = k[:-4]
                if param in frameFieldNames:
                    mulOps.append((param, v))
            else:
                if k in frameFieldNames:
                    absOps.append((k, v))
        voiceOps[voiceName] = (tuple(absOps), tuple(mulOps))
    return voiceOps


def applyVoiceToFrame(frame, voiceName: str, voiceOps: dict) -> None:
    """Apply voice preset parameters to a frame.
    
    This applies per-frame multipliers and overrides for Python preset voices.
    YAML voice profiles are handled differently (formant transforms in frontend).
    
    Args:
        frame: The SpeechPlayer frame to modify (speechPlayer.Frame)
        voiceName: Name of the voice preset
        voiceOps: Pre-calculated voice operations dict
    """
    absOps, mulOps = voiceOps.get(voiceName) or voiceOps.get("Adam", ((), ()))

    for paramName, absVal in absOps:
        setattr(frame, paramName, absVal)

    for paramName, mulVal in mulOps:
        setattr(frame, paramName, getattr(frame, paramName) * mulVal)