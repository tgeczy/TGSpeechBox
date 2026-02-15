# -*- coding: utf-8 -*-
"""NV Speech Player - Text processing utilities.

This module contains:
- Regex patterns for text normalization
- Text normalization functions for eSpeak
- Sentence boundary detection
- Script-aware text splitting for multilingual IPA generation
"""

import re


# Split on punctuation+space for clause pauses
re_textPause = re.compile(r"(?<=[.?!,:;])\s", re.DOTALL | re.UNICODE)

# Normalize whitespace before feeding eSpeak
_re_lineBreaks = re.compile(r"[\r\n\u2028\u2029]+", re.UNICODE)
_re_spaceRuns = re.compile(r"[\t \u00A0]+", re.UNICODE)

# Sentence end detection for Say All coalescing
_SENT_END_RE = re.compile(r"(?:[.!?]+|\.{3})[)\]\"']*\s*$")


def normalizeTextForEspeak(text: str) -> str:
    """Normalize text before feeding to eSpeak.
    
    - Converts newlines to spaces (so line wrapping doesn't introduce pauses)
    - Collapses whitespace runs
    - Strips leading/trailing whitespace
    """
    if not text:
        return ""
    # Convert newlines to spaces so line wrapping doesn't introduce pauses.
    text = _re_lineBreaks.sub(" ", text)
    # Collapse other common whitespace runs.
    text = _re_spaceRuns.sub(" ", text)
    return text.strip()


def looksLikeSentenceEnd(s: str) -> bool:
    """Check if string ends with sentence-ending punctuation.
    
    Used for Say All coalescing to determine where to break.
    """
    if not s:
        return False
    return bool(_SENT_END_RE.search(s.strip()))


# ── Script-aware text splitting ─────────────────────────────────────────
#
# When a non-Latin language (Russian, Bulgarian, Greek, Arabic, etc.) is
# active in eSpeak and the text contains Latin-script words, eSpeak
# processes those Latin words through the wrong phonology.  For example,
# "Hello" in Russian mode becomes garbled because eSpeak applies Russian
# letter-to-sound rules to Latin characters.
#
# splitByScript() detects script boundaries and splits text into segments
# so the driver can switch eSpeak to a Latin-script language (e.g. en-GB)
# for those runs.

# Languages whose primary script is NOT Latin.
# The base language code (before any hyphen) is checked.
_NON_LATIN_LANGS = frozenset({
    # Cyrillic
    "ru", "bg", "uk", "sr", "mk", "be", "kk", "ky", "mn", "tg", "ba",
    # Greek
    "el",
    # Arabic script
    "ar", "fa", "ur", "ps", "ku",
    # Hebrew
    "he", "yi",
    # Georgian
    "ka",
    # Armenian
    "hy",
    # CJK
    "zh", "ja", "ko",
    # Thai, Khmer, Lao, Myanmar, etc.
    "th", "km", "lo", "my",
    # Devanagari / Indic
    "hi", "mr", "ne", "sa", "bn", "gu", "pa", "ta", "te", "kn", "ml", "si",
    # Ethiopic
    "am", "ti",
})


def _isLatinLetter(c: str) -> bool:
    """Check if a character is a Latin-script letter.
    
    Covers Basic Latin (A-Z, a-z) and Latin Extended (accented letters
    like é, ü, ñ, ø, etc.) which are common in European names and
    loanwords.
    """
    cp = ord(c)
    # Basic Latin letters
    if 0x0041 <= cp <= 0x005A or 0x0061 <= cp <= 0x007A:
        return True
    # Latin Extended-A/B and supplements (accented letters)
    if 0x00C0 <= cp <= 0x024F:
        return True
    return False


def _isNeutral(c: str) -> bool:
    """Check if a character is script-neutral (digits, punctuation, whitespace).
    
    Neutral characters don't determine script — they attach to whichever
    script surrounds them.
    """
    if c.isspace():
        return True
    if c.isdigit():
        return True
    # Common punctuation, symbols, math operators
    cp = ord(c)
    if cp < 0x0041:  # ASCII before 'A': digits, punctuation, space
        return True
    if 0x2000 <= cp <= 0x206F:  # General punctuation block
        return True
    if 0x2200 <= cp <= 0x22FF:  # Math operators
        return True
    # Other common symbols
    if c in "[]{}()«»‹›""''—–…·•§¶©®™°±×÷/\\|@#$%^&*~`":
        return True
    return False


def splitByScript(text, baseLang, latinFallback="en"):
    """Split text into segments by script for correct eSpeak language routing.
    
    When the base language uses a non-Latin script (Cyrillic, Greek, Arabic,
    etc.) and the text contains Latin-script words, those words need to be
    processed by eSpeak with a Latin-script language to get correct IPA.
    
    Args:
        text: The input text string.
        baseLang: Current eSpeak language code (e.g. "ru", "bg", "el", "en-gb").
            Only the base code before any hyphen is checked.
        latinFallback: Language code to use for Latin-script segments.
            Defaults to "en".  Pass "en-gb" for British English.
    
    Returns:
        A list of (segment_text, lang_or_None) tuples.
        lang_or_None is None for segments in the base language's script,
        or a language code string (e.g. "en-gb") for Latin segments that
        need a language switch.
        
        If the base language already uses Latin script, returns
        [(text, None)] — no splitting needed.
    
    Examples:
        >>> splitByScript("Привет Hello мир", "ru", "en-gb")
        [("Привет ", None), ("Hello", "en-gb"), (" мир", None)]
        
        >>> splitByScript("Hello world", "en-gb")
        [("Hello world", None)]
        
        >>> splitByScript("Отворете Microsoft Word", "bg", "en-gb")
        [("Отворете ", None), ("Microsoft Word", "en-gb")]
    """
    if not text:
        return [("", None)]
    
    # Check if the base language uses a non-Latin script.
    baseCode = baseLang.split("-")[0].split("_")[0].lower() if baseLang else ""
    if baseCode not in _NON_LATIN_LANGS:
        return [(text, None)]
    
    # Classify each character as 'L' (Latin), 'N' (native/other), or '?' (neutral).
    tags = []
    for c in text:
        if _isNeutral(c):
            tags.append("?")
        elif _isLatinLetter(c):
            tags.append("L")
        else:
            tags.append("N")
    
    # Resolve neutrals: attach each '?' to the script of the nearest
    # non-neutral neighbor.  Prefer the FOLLOWING script (so a space
    # before "Hello" goes with "Hello"), falling back to the preceding.
    resolved = list(tags)
    
    # Forward pass: propagate last known script into neutrals.
    lastScript = "N"  # default: base language
    for i in range(len(resolved)):
        if resolved[i] == "?":
            resolved[i] = lastScript
        else:
            lastScript = resolved[i]
    
    # Backward pass: neutrals BEFORE a script change should attach forward.
    # Walk backward; if a neutral is followed by a different script, adopt it.
    nextScript = "N"
    for i in range(len(resolved) - 1, -1, -1):
        if tags[i] == "?":
            # If the forward script differs from what the forward pass assigned,
            # prefer the forward (following) script.
            resolved[i] = nextScript
        else:
            nextScript = resolved[i]
    
    # Group consecutive same-script characters into segments.
    if not resolved:
        return [(text, None)]
    
    segments = []
    segStart = 0
    curScript = resolved[0]
    
    for i in range(1, len(resolved)):
        if resolved[i] != curScript:
            seg = text[segStart:i]
            lang = latinFallback if curScript == "L" else None
            segments.append((seg, lang))
            segStart = i
            curScript = resolved[i]
    
    # Final segment.
    seg = text[segStart:]
    lang = latinFallback if curScript == "L" else None
    segments.append((seg, lang))
    
    # Merge adjacent segments with the same language to reduce switches.
    merged = [segments[0]]
    for seg, lang in segments[1:]:
        if lang == merged[-1][1]:
            merged[-1] = (merged[-1][0] + seg, lang)
        else:
            merged.append((seg, lang))
    
    return merged
