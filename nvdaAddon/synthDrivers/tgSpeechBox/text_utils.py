# -*- coding: utf-8 -*-
"""NV Speech Player - Text processing utilities.

This module contains:
- Regex patterns for text normalization
- Text normalization functions for eSpeak
- Sentence boundary detection
- Script-aware text splitting for multilingual IPA generation
"""

import re


# Split on punctuation+space for clause pauses.
# Allow optional closing quotes/brackets between the punctuation mark and
# the whitespace so that  ." The  and  ?" She  are recognized as clause
# boundaries (not just  . The  and  ? She).
# Split on punctuation+space for clause pauses.
# (?<!\d)(?<=[.]) — dot NOT preceded by digit (avoids splitting "3. Mai" ordinals)
# (?<=[?!,:;]) — other punctuation always splits
# Optional closing quotes/brackets between punctuation and whitespace.
re_textPause = re.compile(r'(?:(?<!\d)(?<=[.])|(?<=[?!,:;]))[)\]"\u2019\u201D\']*\s', re.DOTALL | re.UNICODE)

# Normalize whitespace before feeding eSpeak
_re_lineBreaks = re.compile(r"[\r\n\u2028\u2029]+", re.UNICODE)
_re_spaceRuns = re.compile(r"[\t \u00A0]+", re.UNICODE)

# Normalize ellipsis: U+2026 → "...", then ensure space after "..." when
# followed by a word character so re_textPause can split on the boundary.
_re_ellipsis_normalize = re.compile(r"\u2026")
_re_ellipsis_space = re.compile(r"\.{2,}(?=[A-Za-z0-9\u00C0-\u024F])")

# Sentence end detection for Say All coalescing
_SENT_END_RE = re.compile(r"(?:[.!?]+|\.{3})[)\]\"']*\s*$")


_re_surrogate = re.compile("[\ud800-\udfff]")


def repairSurrogates(text: str) -> str:
    """Text that can be encoded as UTF-8: no surrogate left on its own.

    Windows text is UTF-16.  A program that cuts a string in the middle of an
    emoji, or a move by character inside one, hands NVDA half a surrogate
    pair, and NVDA hands it on.  Encoding that as UTF-8 for the frontend
    raised, and the whole utterance was lost with its indexes
    (tests/nvda/test_driver_broken_text.py).  Two halves that do arrive
    together and in order are joined into the character they stand for; a
    half on its own is dropped.
    """
    if not _re_surrogate.search(text):
        return text
    return text.encode("utf-16-le", "surrogatepass").decode("utf-16-le", "ignore")


def normalizeTextForEspeak(text: str) -> str:
    """Normalize text before feeding to eSpeak.

    - Drops half surrogate pairs (repairSurrogates)
    - Converts newlines to spaces (so line wrapping doesn't introduce pauses)
    - Collapses whitespace runs
    - Strips leading/trailing whitespace
    """
    if not text:
        return ""
    text = repairSurrogates(text)
    # Normalize Unicode ellipsis to three dots.
    text = _re_ellipsis_normalize.sub("...", text)
    # Insert space after ellipsis glued to next word ("owwwh...I'll" → "owwwh... I'll")
    # so clause splitting can produce a pause there.
    text = _re_ellipsis_space.sub(lambda m: m.group() + " ", text)
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



    # Resolve neutrals.  The goal:
    #   - Whitespace/punctuation between same-script letters → that script
    #   - Whitespace/punctuation at a script boundary → attach to following
    #   - Digits → ALWAYS native script (numbers should be spoken in the
    #     user's language, not the Latin fallback)
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
            resolved[i] = nextScript
        else:
            nextScript = resolved[i]


    # Digit override: force all digit characters to native script.
    # Numbers like "2026" should always be read in the user's language,
    # not the Latin fallback.  Surrounding whitespace is left to the
    # normal resolution (it will merge with whichever script is adjacent).
    for i in range(len(resolved)):
        if text[i].isdigit():
            resolved[i] = "N"


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
