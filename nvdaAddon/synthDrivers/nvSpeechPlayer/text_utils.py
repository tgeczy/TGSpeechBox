# -*- coding: utf-8 -*-
"""NV Speech Player - Text processing utilities.

This module contains:
- Regex patterns for text normalization
- Text normalization functions for eSpeak
- Sentence boundary detection
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
