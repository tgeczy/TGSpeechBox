# -*- coding: utf-8 -*-
"""NV Speech Player - Constants and static configuration.

This module contains:
- Voice presets (voices dict)
- Language definitions
- Pause modes
- Sample rates
- Voice profile prefix
"""

from collections import OrderedDict
from synthDriverHandler import VoiceInfo


try:
    import addonHandler
    addonHandler.initTranslation()
except Exception:
    def _(s): return s


# Language choices exposed in NVDA settings.
languages = OrderedDict([
    ("en-us", VoiceInfo("en-us", "English (US)")),
    ("en-gb", VoiceInfo("en-gb", "English (UK)")),
    ("zh", VoiceInfo("zh", "Chinese")),
    ("pt", VoiceInfo("pt", "Portuguese")),
    ("hu", VoiceInfo("hu", "Hungarian")),
    ("fi", VoiceInfo("fi", "Finnish")),
    ("bg", VoiceInfo("bg", "Bulgarian")),
    ("fr", VoiceInfo("fr", "French")),
    ("es", VoiceInfo("es", "Spanish (Spain)")),
    ("es-mx", VoiceInfo("es-mx", "Spanish (México)")),
    ("it", VoiceInfo("it", "Italian")),
    ("pt-br", VoiceInfo("pt-br", "Brazilian Portuguese")),
    ("ro", VoiceInfo("ro", "Romanian")),
    ("de", VoiceInfo("de", "German")),
    ("nl", VoiceInfo("nl", "Dutch")),
    ("da", VoiceInfo("da", "Danish")),
    ("sv", VoiceInfo("sv", "Swedish")),
    ("cs", VoiceInfo("cs", "Czech")),
    ("hr", VoiceInfo("hr", "Croatian")),
    ("pl", VoiceInfo("pl", "Polish")),
    ("ru", VoiceInfo("ru", "Russian")),
    ("sk", VoiceInfo("sk", "Slovak")),
    ("uk", VoiceInfo("uk", "Ukrainian")),
])


# Punctuation pause modes exposed in NVDA settings.
pauseModes = OrderedDict(
    (
        ("off", VoiceInfo("off", _("Off"))),
        ("short", VoiceInfo("short", _("Short"))),
        ("long", VoiceInfo("long", _("Long"))),
    )
)

# Sample rates exposed in NVDA settings
sampleRates = OrderedDict(
    (
        ("11025", VoiceInfo("11025", _("11025 Hz"))),
        ("16000", VoiceInfo("16000", _("16000 Hz (default)"))),
        ("22050", VoiceInfo("22050", _("22050 Hz"))),
        ("44100", VoiceInfo("44100", _("44100 Hz"))),
    )
)


# Voice presets: multipliers/overrides on generated frames
#
# Parameters ending with "_mul" are multipliers applied to frame values.
# Parameters without "_mul" are absolute values that override the frame.
#
# Key synthesis parameters:
#   - voicePitch/endVoicePitch: fundamental frequency (Hz)
#   - glottalOpenQuotient: 0.30-0.35 pressed/sharp, 0.40 neutral, 0.45-0.50 breathier
#   - voiceTurbulenceAmplitude: breathiness (0-1)
#   - cf1-cf6: cascade formant frequencies (vowel quality)
#   - cb1-cb6: cascade formant bandwidths (narrow=buzzy, wide=smooth)
#   - fricationAmplitude: frication noise level
#   - vibratoPitchOffset/vibratoSpeed: adds human instability
#   - pa1-pa6: parallel formant amplitudes
#   - parallelBypass: noise bypass amount
#
# Voicing tone parameters (DSP-level, sent to speechPlayer.dll):
#   - voicingPeakPos: glottal pulse peak position (0.85-0.95)
#   - voicedPreEmphA: pre-emphasis coefficient
#   - voicedPreEmphMix: pre-emphasis mix amount
#   - highShelfGainDb: output high-shelf gain
#   - highShelfFcHz: output high-shelf frequency
#   - highShelfQ: output high-shelf Q
#   - voicedTiltDbPerOct: spectral tilt (positive=darker, negative=brighter)
#
voices = {
    "Adam": {
        "cb1_mul": 1.3,
        "pa6_mul": 1.3,
        "fricationAmplitude_mul": 0.85,
        "voicedTiltDbPerOct": 0.0,  # Explicit neutral tilt
    },
    "Benjamin": {
        "cf1_mul": 1.01,
        "cf2_mul": 1.02,
        "cf4": 3770,
        "cf5": 4100,
        "cf6": 5000,
        "cfNP_mul": 0.9,
        "cb1_mul": 1.3,
        "fricationAmplitude_mul": 0.7,
        "pa6_mul": 1.3,
        "voicedTiltDbPerOct": 0.0,  # Explicit neutral tilt
    },
    "Caleb": {
        "aspirationAmplitude": 1,
        "voiceAmplitude": 0,
        "voicedTiltDbPerOct": 0.0,  # Explicit neutral tilt
    },
    "David": {
        "voicePitch_mul": 0.75,
        "endVoicePitch_mul": 0.75,
        "cf1_mul": 0.75,
        "cf2_mul": 0.85,
        "cf3_mul": 0.85,
        "voicedTiltDbPerOct": 0.0,  # Explicit neutral tilt
    },
    # -------------------------------------------------------------------------
    # Robert: Eloquence-inspired voice (BRIGHT, CRISP, SYNTHETIC)
    # DRAMATICALLY different from Adam:
    # - Slightly higher base pitch for brighter character
    # - Moderately higher upper formants (not extreme - preserves consonants)
    # - Narrow bandwidths for synthetic buzzy tone
    # - Pressed glottis, minimal breathiness
    # - Frication preserved for clear C, S, F sounds
    # - Negative tilt for extra brightness
    # -------------------------------------------------------------------------
    "Robert": {
        # Slightly higher pitch for brighter character
        "voicePitch_mul": 1.10,
        "endVoicePitch_mul": 1.10,
        # Moderate formant scaling - not too extreme on highs
        "cf1_mul": 1.02,
        "cf2_mul": 1.06,
        "cf3_mul": 1.08,
        # Upper formants - moderate boost, safe for 11025 Hz
        "cf4_mul": 1.08,
        "cf5_mul": 1.10,
        "cf6_mul": 1.05,
        # Narrow bandwidths for buzzy synthetic sound (but not extreme)
        "cb1_mul": 0.65,
        "cb2_mul": 0.68,
        "cb3_mul": 0.72,
        "cb4_mul": 0.75,
        "cb5_mul": 0.78,
        "cb6_mul": 0.80,
        # Pressed glottis: sharp, precise attack
        "glottalOpenQuotient": 0.30,
        # Minimal breathiness - clean synthetic sound
        "voiceTurbulenceAmplitude_mul": 0.20,
        # INCREASED frication to preserve C, S, F consonants
        "fricationAmplitude_mul": 0.75,
        # Moderate bypass for consonant clarity
        "parallelBypass_mul": 0.70,
        # Moderate high parallel formant boost (reduced from before)
        "pa3_mul": 1.08,
        "pa4_mul": 1.15,
        "pa5_mul": 1.20,
        # Voicing tone: negative tilt for brightness
        "voicedTiltDbPerOct": -6.0,
        "pa6_mul": 1.25,
        # Moderate parallel bandwidths (not too tight)
        "pb1_mul": 0.72,
        "pb2_mul": 0.75,
        "pb3_mul": 0.78,
        "pb4_mul": 0.80,
        "pb5_mul": 0.82,
        "pb6_mul": 0.85,
        # Match parallel formants to cascade
        "pf3_mul": 1.06,
        "pf4_mul": 1.08,
        "pf5_mul": 1.10,
        "pf6_mul": 1.00,
        # No vibrato - steady synthetic pitch
        "vibratoPitchOffset": 0.0,
        "vibratoSpeed": 0.0,
    },
}


# Voice profile support: profiles defined in phonemes.yaml are discovered at runtime
# and merged into the voice combobox alongside Python presets.
# Profile voice IDs use this prefix to distinguish them from Python presets.
VOICE_PROFILE_PREFIX = "profile:"


# Say All coalescing constants
COALESCE_MAX_CHARS = 900
COALESCE_MAX_INDEXES = 48
