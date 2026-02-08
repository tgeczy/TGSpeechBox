# -*- coding: utf-8 -*-
"""TGSpeechBox – Dictionary overlay for eSpeak phrase IPA.

This module implements the "eSpeak-primary, dict-overlay" architecture:
eSpeak generates natural phrase-level IPA (with vowel reduction, function
word weakening, and cross-word flow), then a pronunciation dictionary
selectively replaces content words with human-verified pronunciations
for better stress accuracy.

The alignment logic handles cases where eSpeak merges adjacent function
words (e.g. "for a" → fɚɹə, "of the" → ʌvðə) by detecting merge
candidates and scoring them by initial-phoneme plausibility.
"""

import re
from itertools import combinations


# ---------------------------------------------------------------------------
# Regex helpers
# ---------------------------------------------------------------------------

re_wordSplit = re.compile(r"\s+")

re_punctStrip = re.compile(r"^[^\w]*|[^\w]*$", re.UNICODE)

# Characters that eSpeak silently drops from IPA output.  Stripping these
# before the alignment check prevents false-positive word-count matches
# (e.g. "—" occupies a text slot but produces no IPA token).
# NOTE: Do NOT include apostrophes / smart quotes here — they are
# linguistically meaningful (contractions like "I'm", "don't") and eSpeak
# handles them correctly.
re_espeakDropped = re.compile(r"[—–\-\(\)\[\]\{\}\"\u201c\u201d\u201e\u201a\u00ab\u00bb\u2039\u203a]")


# ---------------------------------------------------------------------------
# Function words
# ---------------------------------------------------------------------------

# Function words that should ALWAYS come from eSpeak's phrase output.
# These are naturally reduced in connected speech (e.g. "for" → fɚ,
# "to" → tə, "a" → ə) and should never get dictionary citation forms.
FUNCTION_WORDS = frozenset({
    # Articles / demonstratives
    "a", "an", "the", "this", "that", "these", "those",
    # Pronouns
    "i", "me", "my", "mine", "we", "us", "our", "ours",
    "you", "your", "yours", "he", "him", "his", "she", "her", "hers",
    "it", "its", "they", "them", "their", "theirs",
    "myself", "yourself", "himself", "herself", "itself",
    "ourselves", "themselves",
    # Be / have / do auxiliaries
    "am", "is", "are", "was", "were", "be", "been", "being",
    "have", "has", "had", "having",
    "do", "does", "did",
    # Modals
    "will", "would", "shall", "should", "may", "might",
    "can", "could", "must",
    # Prepositions
    "to", "for", "of", "in", "on", "at", "by", "with", "from",
    "into", "onto", "upon", "about", "after", "before",
    "between", "through", "during", "without", "within",
    "up", "down", "out", "off",
    # Conjunctions / particles
    "and", "or", "but", "nor", "not", "no", "so", "if", "as",
    "than", "that", "because", "since", "while", "when",
    "where", "whether", "although", "though",
    # Question words
    "what", "which", "who", "whom", "whose", "how", "why",
    # Quantifiers / adverbs that reduce
    "all", "each", "every", "both", "some", "any", "many",
    "much", "few", "more", "most", "other",
    "just", "very", "too", "also", "only", "even",
    "still", "already", "never", "ever", "always",
    # Common contractions
    "don't", "won't", "can't", "isn't", "aren't", "wasn't",
    "weren't", "hasn't", "haven't", "hadn't", "doesn't",
    "didn't", "wouldn't", "shouldn't", "couldn't", "mustn't",
    # Pronoun contractions — always function words
    "i'm", "i'll", "i've", "i'd",
    "you're", "you'll", "you've", "you'd",
    "we're", "we'll", "we've", "we'd",
    "he's", "he'll", "he'd", "she's", "she'll", "she'd",
    "it's", "it'll", "they're", "they'll", "they've", "they'd",
    "there's", "here's", "that's", "what's", "who's", "let's",
    "there", "here",
})


# ---------------------------------------------------------------------------
# Alignment scoring
# ---------------------------------------------------------------------------

# Quick initial-phoneme map for scoring alignment candidates.
# Maps English first-letter to plausible IPA onset characters.
_INITIAL_PHONEME_MAP = {
    'a': 'æɑəeaɐ', 'b': 'b', 'c': 'ks', 'd': 'dð', 'e': 'ɛɪie',
    'f': 'f', 'g': 'ɡgd', 'h': 'h', 'i': 'ɪa', 'j': 'dj',
    'k': 'k', 'l': 'l', 'm': 'm', 'n': 'n', 'o': 'oɑɔaʌ',
    'p': 'p', 'q': 'k', 'r': 'ɹr', 's': 'sʃ', 't': 'tθ',
    'u': 'ʌujʊ', 'v': 'v', 'w': 'w', 'x': 'zɛ', 'y': 'j', 'z': 'z',
}


def _ipaStartsOk(stripped, ipa_token):
    """Check if an IPA token could plausibly be a pronunciation of a word."""
    if not stripped:
        return True
    expected = _INITIAL_PHONEME_MAP.get(stripped[0], "")
    if not expected:
        return True
    ipa_clean = ipa_token.lstrip("\u02c8\u02cc")  # strip stress marks
    return any(ipa_clean.startswith(ch) for ch in expected)


# ---------------------------------------------------------------------------
# Core alignment
# ---------------------------------------------------------------------------

def alignWordsToIPA(words, ipaTokens):
    """Align text words to eSpeak IPA tokens, accounting for merged
    function words (e.g. "for a" → fɚɹə, "of the" → ʌvðə).

    Returns a list of (word_indices, ipa_index) tuples if alignment
    succeeds, or None if it cannot be recovered.
    *word_indices* is a list of ints — length 1 normally, length 2 when
    two function words were merged into one IPA token.
    """
    diff = len(words) - len(ipaTokens)

    # Perfect 1:1 alignment.
    if diff == 0:
        return [([i], i) for i in range(len(words))]

    # More IPA tokens than words, or huge gap → give up.
    if diff < 0 or diff > 3:
        return None

    # Find candidate merge points: adjacent function word pairs.
    candidates = []
    for i in range(len(words) - 1):
        if (re_punctStrip.sub("", words[i]).lower() in FUNCTION_WORDS and
                re_punctStrip.sub("", words[i + 1]).lower() in FUNCTION_WORDS):
            candidates.append(i)

    if len(candidates) < diff:
        return None

    # Try all combinations of `diff` merge points, pick the one where
    # the most single-word ↔ IPA-token pairs have a plausible initial
    # phoneme match.
    best_mapping = None
    best_score = -1

    for combo in combinations(candidates, diff):
        # No overlapping merges (adjacent merge points need gap ≥ 2).
        if any(combo[j + 1] - combo[j] < 2 for j in range(len(combo) - 1)):
            continue

        merge_set = set(combo)
        mapping = []
        ti = wi = 0
        while wi < len(words) and ti < len(ipaTokens):
            if wi in merge_set:
                mapping.append(([wi, wi + 1], ti))
                wi += 2
            else:
                mapping.append(([wi], ti))
                wi += 1
            ti += 1
        if ti != len(ipaTokens) or wi != len(words):
            continue

        # Score: count single-word entries with plausible initial phoneme.
        score = sum(
            1 for widxs, tidx in mapping
            if len(widxs) == 1
            and _ipaStartsOk(
                re_punctStrip.sub("", words[widxs[0]]).lower(),
                ipaTokens[tidx])
        )
        if score > best_score:
            best_mapping = mapping
            best_score = score

    return best_mapping


# ---------------------------------------------------------------------------
# Main overlay function
# ---------------------------------------------------------------------------

def overlayDictIPA(text, phraseIpa, dictLookup):
    """Apply dictionary pronunciations on top of eSpeak phrase IPA.

    Parameters
    ----------
    text : str
        Original text that was sent to eSpeak.
    phraseIpa : str
        IPA string returned by eSpeak for the full phrase.
    dictLookup : callable
        ``dictLookup(word) -> str|None`` — returns IPA for a word or
        empty/None if not found.

    Returns
    -------
    str
        IPA string with content words replaced by dictionary entries
        where possible, function words kept as eSpeak produced them.
    """
    if not text or not phraseIpa:
        return phraseIpa or ""

    # Build a "speakable words" list that matches what eSpeak actually
    # pronounces.  eSpeak silently drops em dashes, parentheses, quotes,
    # etc.  Strip those characters first, then split.
    cleaned = re_espeakDropped.sub("", text)
    words = [w for w in re_wordSplit.split(cleaned.strip()) if w]
    ipaTokens = phraseIpa.split()

    if not words:
        return phraseIpa

    # Try to align (handles both exact match and function-word merges).
    alignment = alignWordsToIPA(words, ipaTokens)
    if not alignment:
        return phraseIpa

    # Apply dict overlay on aligned content words.
    result = list(ipaTokens)
    for wordIdxs, ipaIdx in alignment:
        # Skip merged pairs — those are function words, keep eSpeak.
        if len(wordIdxs) != 1:
            continue
        word = words[wordIdxs[0]]
        stripped = re_punctStrip.sub("", word)
        if not stripped:
            continue
        if stripped.lower() in FUNCTION_WORDS:
            continue
        dictIpa = dictLookup(stripped)
        if dictIpa:
            result[ipaIdx] = dictIpa

    return " ".join(result)
