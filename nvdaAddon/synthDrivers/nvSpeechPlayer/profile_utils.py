# -*- coding: utf-8 -*-
"""NV Speech Player - Voice profile discovery and parsing.

This module contains:
- Voice profile discovery from phonemes.yaml
- Voicing tone parameter parsing
- Voice-to-frame application
"""

import os
from logHandler import log


def discoverVoiceProfiles(packsDir: str) -> list:
    """Discover voice profile names from phonemes.yaml.
    
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


def discoverVoicingTones(packsDir: str) -> dict:
    """Discover voicingTone parameters for each voice profile from phonemes.yaml.
    
    Returns a dict mapping profile name -> dict of voicing tone parameters.
    Only profiles that have a voicingTone: block are included.
    
    Example YAML:
        voiceProfiles:
          Crystal:
            voicingTone:
              voicingPeakPos: 0.88
              highShelfGainDb: 1.5
              voicedTiltDbPerOct: -6.0
    
    Args:
        packsDir: Path to the packs directory containing phonemes.yaml
        
    Returns:
        Dict mapping profile name to voicing tone parameters
        {"Crystal": {"voicingPeakPos": 0.88, "highShelfGainDb": 1.5, "voicedTiltDbPerOct": -6.0}}
    """
    tones = {}
    yamlPath = os.path.join(packsDir, "phonemes.yaml")
    
    # Valid voicing tone parameter names
    validParams = {
        "voicingPeakPos", "voicedPreEmphA", "voicedPreEmphMix",
        "highShelfGainDb", "highShelfFcHz", "highShelfQ",
        "voicedTiltDbPerOct"
    }
    
    try:
        if not os.path.isfile(yamlPath):
            return tones
            
        with open(yamlPath, "r", encoding="utf-8") as f:
            lines = f.readlines()
        
        inVoiceProfiles = False
        currentProfile = None
        inVoicingTone = False
        profileIndent = None
        voicingToneIndent = None
        
        for line in lines:
            stripped = line.strip()
            
            if not stripped or stripped.startswith("#"):
                continue
            
            # Check for voiceProfiles: at column 0
            if line and not line[0].isspace() and stripped.startswith("voiceProfiles:"):
                inVoiceProfiles = True
                profileIndent = None
                currentProfile = None
                inVoicingTone = False
                continue
            
            if not inVoiceProfiles:
                continue
            
            # Left the voiceProfiles section (back to column 0)
            if line and not line[0].isspace():
                inVoiceProfiles = False
                currentProfile = None
                inVoicingTone = False
                continue
            
            indent = len(line) - len(line.lstrip())
            
            # Determine profile indent level (first indented line under voiceProfiles:)
            if profileIndent is None:
                profileIndent = indent
            
            # Profile name line (at profile indent level)
            if indent == profileIndent and ":" in stripped:
                key = stripped.split(":")[0].strip()
                if key and not key.startswith("#") and "." not in key:
                    currentProfile = key
                    inVoicingTone = False
                    voicingToneIndent = None
                continue
            
            if currentProfile is None:
                continue
            
            # Check for voicingTone: under current profile
            if not inVoicingTone:
                if indent > profileIndent and stripped.startswith("voicingTone:"):
                    inVoicingTone = True
                    voicingToneIndent = None
                    if currentProfile not in tones:
                        tones[currentProfile] = {}
                continue
            
            # We're inside voicingTone: block
            # Check if we've left it (indent decreased to profile level or less)
            if indent <= profileIndent:
                # Back to profile level - new profile or leaving
                inVoicingTone = False
                key = stripped.split(":")[0].strip()
                if key and not key.startswith("#") and "." not in key:
                    currentProfile = key
                    voicingToneIndent = None
                else:
                    currentProfile = None
                continue
            
            # Check if we've left voicingTone (another sibling key like classScales:)
            if voicingToneIndent is not None and indent <= voicingToneIndent:
                # Could be a sibling key under the profile
                if ":" in stripped:
                    siblingKey = stripped.split(":")[0].strip()
                    if siblingKey in ("classScales", "phonemeOverrides"):
                        inVoicingTone = False
                        continue
            
            # Parse voicing tone parameter
            if ":" in stripped:
                if voicingToneIndent is None:
                    voicingToneIndent = indent
                
                if indent >= voicingToneIndent:
                    parts = stripped.split(":", 1)
                    paramName = parts[0].strip()
                    if paramName in validParams and len(parts) > 1:
                        valStr = parts[1].strip()
                        # Remove any trailing comments
                        if "#" in valStr:
                            valStr = valStr.split("#")[0].strip()
                        try:
                            val = float(valStr)
                            tones[currentProfile][paramName] = val
                        except (ValueError, TypeError):
                            pass
        
    except Exception:
        log.debug("nvSpeechPlayer: discoverVoicingTones failed", exc_info=True)
    
    return tones


def buildVoiceOps(voices: dict, frameFieldNames: set) -> dict:
    """Pre-calculate per-voice operations for fast application.
    
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
