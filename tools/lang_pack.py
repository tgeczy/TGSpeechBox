#!/usr/bin/env python3
"""
lang_pack.py - Complete NV Speech Player Language Pack Parser

Comprehensive YAML language pack parser that mirrors the C++ LanguagePack
structure from pack.h. Provides full parsing of all ~120 settings.

Usage:
    from lang_pack import load_pack_set, PackSet
    pack = load_pack_set("/path/to/packs", "en-us")
    print(pack.lang.coarticulation_strength)
"""

from __future__ import annotations
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional

# Use our lenient YAML parser that handles unquoted IPA symbols
from simple_yaml import load_yaml_file, get_bool, get_number, get_string

# =============================================================================
# Constants (mirror pack.h)
# =============================================================================

FRAME_FIELD_COUNT = 47

FIELD_NAMES = [
    "voicePitch", "vibratoPitchOffset", "vibratoSpeed", "voiceTurbulenceAmplitude",
    "glottalOpenQuotient", "voiceAmplitude", "aspirationAmplitude",
    "cf1", "cf2", "cf3", "cf4", "cf5", "cf6", "cfN0", "cfNP",
    "cb1", "cb2", "cb3", "cb4", "cb5", "cb6", "cbN0", "cbNP", "caNP",
    "fricationAmplitude",
    "pf1", "pf2", "pf3", "pf4", "pf5", "pf6",
    "pb1", "pb2", "pb3", "pb4", "pb5", "pb6",
    "pa1", "pa2", "pa3", "pa4", "pa5", "pa6",
    "parallelBypass", "preFormantGain", "outputGain", "endVoicePitch",
]

FIELD_ID = {name: idx for idx, name in enumerate(FIELD_NAMES)}

PHONEME_FLAGS = {
    "_isAfricate": 1 << 0, "_isLiquid": 1 << 1, "_isNasal": 1 << 2,
    "_isSemivowel": 1 << 3, "_isStop": 1 << 4, "_isTap": 1 << 5,
    "_isTrill": 1 << 6, "_isVoiced": 1 << 7, "_isVowel": 1 << 8,
    "_copyAdjacent": 1 << 9,
}

# =============================================================================
# Data Classes
# =============================================================================

@dataclass
class PhonemeDef:
    """Phoneme definition from phonemes.yaml"""
    key: str
    flags: int = 0
    set_mask: int = 0
    fields: List[float] = field(default_factory=lambda: [0.0] * FRAME_FIELD_COUNT)

    # Convenience properties
    @property
    def is_vowel(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isVowel"])
    @property
    def is_voiced(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isVoiced"])
    @property
    def is_stop(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isStop"])
    @property
    def is_affricate(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isAfricate"])
    @property
    def is_nasal(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isNasal"])
    @property
    def is_liquid(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isLiquid"])
    @property
    def is_semivowel(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isSemivowel"])
    @property
    def is_tap(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isTap"])
    @property
    def is_trill(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isTrill"])
    @property
    def copy_adjacent(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_copyAdjacent"])

    def get_field(self, name: str) -> float:
        idx = FIELD_ID.get(name)
        return self.fields[idx] if idx is not None else 0.0

    def has_field(self, name: str) -> bool:
        idx = FIELD_ID.get(name)
        return bool(self.set_mask & (1 << idx)) if idx is not None else False


@dataclass
class RuleWhen:
    at_word_start: bool = False
    at_word_end: bool = False
    before_class: str = ""
    after_class: str = ""


@dataclass
class ReplacementRule:
    from_str: str
    to_list: List[str]
    when: RuleWhen = field(default_factory=RuleWhen)


@dataclass
class TransformRule:
    is_vowel: int = -1
    is_voiced: int = -1
    is_stop: int = -1
    is_affricate: int = -1
    is_nasal: int = -1
    is_liquid: int = -1
    is_semivowel: int = -1
    is_tap: int = -1
    is_trill: int = -1
    is_fricative_like: int = -1
    set_ops: Dict[int, float] = field(default_factory=dict)
    scale_ops: Dict[int, float] = field(default_factory=dict)
    add_ops: Dict[int, float] = field(default_factory=dict)


@dataclass
class IntonationClause:
    pre_head_start: int = 46
    pre_head_end: int = 57
    head_extend_from: int = 4
    head_start: int = 80
    head_end: int = 50
    head_steps: List[int] = field(default_factory=lambda: [100, 75, 50, 25, 0, 63, 38, 13, 0])
    head_stress_end_delta: int = -16
    head_unstressed_run_start_delta: int = -8
    head_unstressed_run_end_delta: int = -5
    nucleus0_start: int = 64
    nucleus0_end: int = 8
    nucleus_start: int = 70
    nucleus_end: int = 18
    tail_start: int = 24
    tail_end: int = 8


@dataclass
class LanguagePack:
    """Complete language pack - mirrors pack.h LanguagePack with all ~120 settings."""
    lang_tag: str = ""

    # Stress timing
    primary_stress_div: float = 1.4
    secondary_stress_div: float = 1.1

    # Legacy pitch
    legacy_pitch_mode: bool = False
    legacy_pitch_inflection_scale: float = 0.58

    # Post-stop aspiration
    post_stop_aspiration_enabled: bool = False
    post_stop_aspiration_phoneme: str = "h"

    # Stop closure
    stop_closure_mode: str = "vowel-and-cluster"
    stop_closure_cluster_gaps_enabled: bool = True
    stop_closure_after_nasals_enabled: bool = False
    stop_closure_vowel_gap_ms: float = 41.0
    stop_closure_vowel_fade_ms: float = 10.0
    stop_closure_cluster_gap_ms: float = 22.0
    stop_closure_cluster_fade_ms: float = 4.0
    stop_closure_word_boundary_cluster_gap_ms: float = 0.0
    stop_closure_word_boundary_cluster_fade_ms: float = 0.0

    # Segment boundary
    segment_boundary_gap_ms: float = 0.0
    segment_boundary_fade_ms: float = 0.0
    segment_boundary_skip_vowel_to_vowel: bool = True
    segment_boundary_skip_vowel_to_liquid: bool = False

    # Diphthongs
    auto_tie_diphthongs: bool = False
    auto_diphthong_offglide_to_semivowel: bool = False
    semivowel_offglide_scale: float = 1.0

    # Trill
    trill_modulation_ms: float = 0.0
    trill_modulation_fade_ms: float = 0.0

    # Vowel hiatus
    stressed_vowel_hiatus_gap_ms: float = 0.0
    stressed_vowel_hiatus_fade_ms: float = 0.0
    spelling_diphthong_mode: str = "none"

    # Duration
    lengthened_scale: float = 1.05
    lengthened_scale_hu: float = 1.3
    apply_lengthened_scale_to_vowels_only: bool = True
    lengthened_vowel_final_coda_scale: float = 1.0

    # Length contrast
    length_contrast_enabled: bool = False
    length_contrast_short_vowel_ceiling_ms: float = 80.0
    length_contrast_long_vowel_floor_ms: float = 120.0
    length_contrast_geminate_closure_scale: float = 1.8
    length_contrast_geminate_release_scale: float = 0.9
    length_contrast_pre_geminate_vowel_scale: float = 0.85

    # Coarticulation
    coarticulation_enabled: bool = True
    coarticulation_strength: float = 0.25
    coarticulation_transition_extent: float = 0.35
    coarticulation_fade_into_consonants: bool = True
    coarticulation_word_initial_fade_scale: float = 1.0
    coarticulation_graduated: bool = True
    coarticulation_adjacency_max_consonants: float = 2.0
    coarticulation_labial_f2_locus: float = 800.0
    coarticulation_alveolar_f2_locus: float = 1800.0
    coarticulation_velar_f2_locus: float = 2200.0
    coarticulation_velar_pinch_enabled: bool = True
    coarticulation_velar_pinch_threshold: float = 1800.0
    coarticulation_velar_pinch_f2_scale: float = 0.9
    coarticulation_velar_pinch_f3: float = 2400.0

    # Boundary smoothing
    boundary_smoothing_enabled: bool = False
    boundary_smoothing_vowel_to_stop_fade_ms: float = 12.0
    boundary_smoothing_stop_to_vowel_fade_ms: float = 10.0
    boundary_smoothing_vowel_to_fric_fade_ms: float = 6.0

    # Trajectory limiting
    trajectory_limit_enabled: bool = False
    trajectory_limit_apply_mask: int = (1 << FIELD_ID["cf2"]) | (1 << FIELD_ID["cf3"])
    trajectory_limit_max_hz_per_ms: List[float] = field(default_factory=lambda: _default_traj_rates())
    trajectory_limit_window_ms: float = 25.0
    trajectory_limit_apply_across_word_boundary: bool = False

    # Liquid dynamics
    liquid_dynamics_enabled: bool = False
    liquid_dynamics_lateral_onglide_f1_delta: float = -50.0
    liquid_dynamics_lateral_onglide_f2_delta: float = 200.0
    liquid_dynamics_lateral_onglide_duration_pct: float = 0.30
    liquid_dynamics_rhotic_f3_dip_enabled: bool = False
    liquid_dynamics_rhotic_f3_minimum: float = 1600.0
    liquid_dynamics_rhotic_f3_dip_duration_pct: float = 0.50
    liquid_dynamics_labial_glide_transition_enabled: bool = False
    liquid_dynamics_labial_glide_start_f1: float = 300.0
    liquid_dynamics_labial_glide_start_f2: float = 700.0
    liquid_dynamics_labial_glide_transition_pct: float = 0.60

    # Phrase-final lengthening
    phrase_final_lengthening_enabled: bool = False
    phrase_final_lengthening_final_syllable_scale: float = 1.4
    phrase_final_lengthening_penultimate_syllable_scale: float = 1.15
    phrase_final_lengthening_statement_scale: float = 1.0
    phrase_final_lengthening_question_scale: float = 0.9
    phrase_final_lengthening_nucleus_only_mode: bool = True

    # Microprosody
    microprosody_enabled: bool = False
    microprosody_voiceless_f0_raise_enabled: bool = True
    microprosody_voiceless_f0_raise_hz: float = 15.0
    microprosody_voiceless_f0_raise_end_hz: float = 0.0
    microprosody_voiced_f0_lower_enabled: bool = True
    microprosody_voiced_f0_lower_hz: float = 8.0
    microprosody_min_vowel_ms: float = 25.0

    # Rate reduction
    rate_reduction_enabled: bool = False
    rate_reduction_schwa_reduction_threshold: float = 2.5
    rate_reduction_schwa_min_duration_ms: float = 15.0
    rate_reduction_schwa_scale: float = 0.8

    # Nasalization
    nasalization_anticipatory_enabled: bool = False
    nasalization_anticipatory_amplitude: float = 0.4
    nasalization_anticipatory_blend: float = 0.5

    # Positional allophones
    positional_allophones_enabled: bool = False
    positional_allophones_stop_aspiration_word_initial_stressed: float = 0.8
    positional_allophones_stop_aspiration_word_initial: float = 0.5
    positional_allophones_stop_aspiration_intervocalic: float = 0.2
    positional_allophones_stop_aspiration_word_final: float = 0.1
    positional_allophones_lateral_darkness_pre_vocalic: float = 0.2
    positional_allophones_lateral_darkness_post_vocalic: float = 0.8
    positional_allophones_lateral_darkness_syllabic: float = 0.9
    positional_allophones_lateral_dark_f2_target_hz: float = 900.0
    positional_allophones_glottal_reinforcement_enabled: bool = False
    positional_allophones_glottal_reinforcement_contexts: List[str] = field(default_factory=lambda: ["V_#"])
    positional_allophones_glottal_reinforcement_duration_ms: float = 18.0

    # Language-specific
    hu_short_a_vowel_enabled: bool = True
    hu_short_a_vowel_key: str = "ᴒ"
    hu_short_a_vowel_scale: float = 0.85
    english_long_u_shorten_enabled: bool = True
    english_long_u_key: str = "u"
    english_long_u_word_final_scale: float = 0.80

    # Output defaults
    default_pre_formant_gain: float = 1.0
    default_output_gain: float = 1.5
    default_vibrato_pitch_offset: float = 0.0
    default_vibrato_speed: float = 0.0
    default_voice_turbulence_amplitude: float = 0.0
    default_glottal_open_quotient: float = 0.0

    # Normalization
    strip_allophone_digits: bool = True
    strip_hyphen: bool = True
    aliases: Dict[str, str] = field(default_factory=dict)
    pre_replacements: List[ReplacementRule] = field(default_factory=list)
    replacements: List[ReplacementRule] = field(default_factory=list)
    classes: Dict[str, List[str]] = field(default_factory=dict)

    # Transforms
    transforms: List[TransformRule] = field(default_factory=list)

    # Intonation
    intonation: Dict[str, IntonationClause] = field(default_factory=dict)

    # Tonal
    tonal: bool = False
    tone_contours: Dict[str, List[int]] = field(default_factory=dict)
    tone_digits_enabled: bool = True
    tone_contours_absolute: bool = True


def _default_traj_rates() -> List[float]:
    rates = [0.0] * FRAME_FIELD_COUNT
    rates[FIELD_ID["cf2"]] = 18.0
    rates[FIELD_ID["cf3"]] = 22.0
    return rates


@dataclass
class PackSet:
    """Complete pack set with phonemes and language settings."""
    phonemes: Dict[str, PhonemeDef] = field(default_factory=dict)
    lang: LanguagePack = field(default_factory=LanguagePack)

    def has_phoneme(self, key: str) -> bool:
        return key in self.phonemes

    def get_phoneme(self, key: str) -> Optional[PhonemeDef]:
        return self.phonemes.get(key)


# =============================================================================
# Parsing helpers
# =============================================================================

def _parse_bool(val: Any) -> bool:
    if isinstance(val, bool): return val
    if isinstance(val, str): return val.lower().strip() in ("1", "true", "yes", "on")
    return bool(val)


def _parse_phoneme(key: str, data: dict) -> PhonemeDef:
    pdef = PhonemeDef(key=key)
    for fname, val in data.items():
        if fname.startswith("_"):
            if fname in PHONEME_FLAGS and _parse_bool(val):
                pdef.flags |= PHONEME_FLAGS[fname]
        elif fname in FIELD_ID:
            try:
                pdef.fields[FIELD_ID[fname]] = float(val)
                pdef.set_mask |= (1 << FIELD_ID[fname])
            except: pass
    return pdef


def _parse_intonation(data: dict) -> IntonationClause:
    ic = IntonationClause()
    def gi(k, d): return int(data.get(k, d)) if k in data else d
    ic.pre_head_start = gi("preHeadStart", ic.pre_head_start)
    ic.pre_head_end = gi("preHeadEnd", ic.pre_head_end)
    ic.head_extend_from = gi("headExtendFrom", ic.head_extend_from)
    ic.head_start = gi("headStart", ic.head_start)
    ic.head_end = gi("headEnd", ic.head_end)
    ic.head_stress_end_delta = gi("headStressEndDelta", ic.head_stress_end_delta)
    ic.head_unstressed_run_start_delta = gi("headUnstressedRunStartDelta", ic.head_unstressed_run_start_delta)
    ic.head_unstressed_run_end_delta = gi("headUnstressedRunEndDelta", ic.head_unstressed_run_end_delta)
    ic.nucleus0_start = gi("nucleus0Start", ic.nucleus0_start)
    ic.nucleus0_end = gi("nucleus0End", ic.nucleus0_end)
    ic.nucleus_start = gi("nucleusStart", ic.nucleus_start)
    ic.nucleus_end = gi("nucleusEnd", ic.nucleus_end)
    ic.tail_start = gi("tailStart", ic.tail_start)
    ic.tail_end = gi("tailEnd", ic.tail_end)
    if "headSteps" in data and isinstance(data["headSteps"], list):
        ic.head_steps = [int(x) for x in data["headSteps"]]
    return ic


def _apply_defaults(lp: LanguagePack):
    """Apply default intonation contours."""
    lp.intonation["."] = IntonationClause(46,57,4,80,50,[100,75,50,25,0,63,38,13,0],-16,-8,-5,64,8,70,18,24,8)
    lp.intonation[","] = IntonationClause(46,57,4,80,60,[100,75,50,25,0,63,38,13,0],-16,-8,-5,34,52,78,34,34,52)
    lp.intonation["?"] = IntonationClause(45,56,3,75,43,[100,75,50,20,60,35,11,0],-16,-7,0,34,68,86,21,34,68)
    lp.intonation["!"] = IntonationClause(46,57,3,90,50,[100,75,50,16,82,50,32,16],-16,-9,0,92,4,92,80,76,4)


def _merge_settings(lp: LanguagePack, s: dict):
    """Merge settings section into LanguagePack."""
    def gn(k, d): return float(s.get(k, d)) if k in s else d
    def gb(k, d): return _parse_bool(s[k]) if k in s else d
    def gs(k, d): return str(s.get(k, d)) if k in s else d

    # All flat keys (matching pack.cpp mergeSettings)
    lp.primary_stress_div = gn("primaryStressDiv", lp.primary_stress_div)
    lp.secondary_stress_div = gn("secondaryStressDiv", lp.secondary_stress_div)
    lp.legacy_pitch_mode = gb("legacyPitchMode", lp.legacy_pitch_mode)
    lp.legacy_pitch_inflection_scale = gn("legacyPitchInflectionScale", lp.legacy_pitch_inflection_scale)
    lp.post_stop_aspiration_enabled = gb("postStopAspirationEnabled", lp.post_stop_aspiration_enabled)
    lp.post_stop_aspiration_phoneme = gs("postStopAspirationPhoneme", lp.post_stop_aspiration_phoneme)
    lp.stop_closure_mode = gs("stopClosureMode", lp.stop_closure_mode)
    lp.stop_closure_cluster_gaps_enabled = gb("stopClosureClusterGapsEnabled", lp.stop_closure_cluster_gaps_enabled)
    lp.stop_closure_after_nasals_enabled = gb("stopClosureAfterNasalsEnabled", lp.stop_closure_after_nasals_enabled)
    lp.stop_closure_vowel_gap_ms = gn("stopClosureVowelGapMs", lp.stop_closure_vowel_gap_ms)
    lp.stop_closure_vowel_fade_ms = gn("stopClosureVowelFadeMs", lp.stop_closure_vowel_fade_ms)
    lp.stop_closure_cluster_gap_ms = gn("stopClosureClusterGapMs", lp.stop_closure_cluster_gap_ms)
    lp.stop_closure_cluster_fade_ms = gn("stopClosureClusterFadeMs", lp.stop_closure_cluster_fade_ms)
    lp.stop_closure_word_boundary_cluster_gap_ms = gn("stopClosureWordBoundaryClusterGapMs", lp.stop_closure_word_boundary_cluster_gap_ms)
    lp.stop_closure_word_boundary_cluster_fade_ms = gn("stopClosureWordBoundaryClusterFadeMs", lp.stop_closure_word_boundary_cluster_fade_ms)
    lp.segment_boundary_gap_ms = gn("segmentBoundaryGapMs", lp.segment_boundary_gap_ms)
    lp.segment_boundary_fade_ms = gn("segmentBoundaryFadeMs", lp.segment_boundary_fade_ms)
    lp.segment_boundary_skip_vowel_to_vowel = gb("segmentBoundarySkipVowelToVowel", lp.segment_boundary_skip_vowel_to_vowel)
    lp.segment_boundary_skip_vowel_to_liquid = gb("segmentBoundarySkipVowelToLiquid", lp.segment_boundary_skip_vowel_to_liquid)
    lp.auto_tie_diphthongs = gb("autoTieDiphthongs", lp.auto_tie_diphthongs)
    lp.auto_diphthong_offglide_to_semivowel = gb("autoDiphthongOffglideToSemivowel", lp.auto_diphthong_offglide_to_semivowel)
    lp.semivowel_offglide_scale = gn("semivowelOffglideScale", lp.semivowel_offglide_scale)
    lp.trill_modulation_ms = gn("trillModulationMs", lp.trill_modulation_ms)
    lp.trill_modulation_fade_ms = gn("trillModulationFadeMs", lp.trill_modulation_fade_ms)
    lp.stressed_vowel_hiatus_gap_ms = gn("stressedVowelHiatusGapMs", lp.stressed_vowel_hiatus_gap_ms)
    lp.stressed_vowel_hiatus_fade_ms = gn("stressedVowelHiatusFadeMs", lp.stressed_vowel_hiatus_fade_ms)
    mode = gs("spellingDiphthongMode", "").lower()
    if mode in ("none", "monophthong"): lp.spelling_diphthong_mode = mode
    lp.lengthened_scale = gn("lengthenedScale", lp.lengthened_scale)
    lp.lengthened_scale_hu = gn("lengthenedScaleHu", lp.lengthened_scale_hu)
    lp.apply_lengthened_scale_to_vowels_only = gb("applyLengthenedScaleToVowelsOnly", lp.apply_lengthened_scale_to_vowels_only)
    lp.lengthened_vowel_final_coda_scale = gn("lengthenedVowelFinalCodaScale", lp.lengthened_vowel_final_coda_scale)
    lp.length_contrast_enabled = gb("lengthContrastEnabled", lp.length_contrast_enabled)
    lp.length_contrast_short_vowel_ceiling_ms = gn("lengthContrastShortVowelCeilingMs", lp.length_contrast_short_vowel_ceiling_ms)
    lp.length_contrast_long_vowel_floor_ms = gn("lengthContrastLongVowelFloorMs", lp.length_contrast_long_vowel_floor_ms)
    lp.length_contrast_geminate_closure_scale = gn("lengthContrastGeminateClosureScale", lp.length_contrast_geminate_closure_scale)
    lp.length_contrast_geminate_release_scale = gn("lengthContrastGeminateReleaseScale", lp.length_contrast_geminate_release_scale)
    lp.length_contrast_pre_geminate_vowel_scale = gn("lengthContrastPreGeminateVowelScale", lp.length_contrast_pre_geminate_vowel_scale)
    lp.coarticulation_enabled = gb("coarticulationEnabled", lp.coarticulation_enabled)
    lp.coarticulation_strength = gn("coarticulationStrength", lp.coarticulation_strength)
    lp.coarticulation_transition_extent = gn("coarticulationTransitionExtent", lp.coarticulation_transition_extent)
    lp.coarticulation_fade_into_consonants = gb("coarticulationFadeIntoConsonants", lp.coarticulation_fade_into_consonants)
    lp.coarticulation_word_initial_fade_scale = gn("coarticulationWordInitialFadeScale", lp.coarticulation_word_initial_fade_scale)
    lp.coarticulation_graduated = gb("coarticulationGraduated", lp.coarticulation_graduated)
    lp.coarticulation_adjacency_max_consonants = gn("coarticulationAdjacencyMaxConsonants", lp.coarticulation_adjacency_max_consonants)
    lp.coarticulation_labial_f2_locus = gn("coarticulationLabialF2Locus", lp.coarticulation_labial_f2_locus)
    lp.coarticulation_alveolar_f2_locus = gn("coarticulationAlveolarF2Locus", lp.coarticulation_alveolar_f2_locus)
    lp.coarticulation_velar_f2_locus = gn("coarticulationVelarF2Locus", lp.coarticulation_velar_f2_locus)
    lp.coarticulation_velar_pinch_enabled = gb("coarticulationVelarPinchEnabled", lp.coarticulation_velar_pinch_enabled)
    lp.coarticulation_velar_pinch_threshold = gn("coarticulationVelarPinchThreshold", lp.coarticulation_velar_pinch_threshold)
    lp.coarticulation_velar_pinch_f2_scale = gn("coarticulationVelarPinchF2Scale", lp.coarticulation_velar_pinch_f2_scale)
    lp.coarticulation_velar_pinch_f3 = gn("coarticulationVelarPinchF3", lp.coarticulation_velar_pinch_f3)
    lp.boundary_smoothing_enabled = gb("boundarySmoothingEnabled", lp.boundary_smoothing_enabled)
    lp.boundary_smoothing_vowel_to_stop_fade_ms = gn("boundarySmoothingVowelToStopFadeMs", lp.boundary_smoothing_vowel_to_stop_fade_ms)
    lp.boundary_smoothing_stop_to_vowel_fade_ms = gn("boundarySmoothingStopToVowelFadeMs", lp.boundary_smoothing_stop_to_vowel_fade_ms)
    lp.boundary_smoothing_vowel_to_fric_fade_ms = gn("boundarySmoothingVowelToFricFadeMs", lp.boundary_smoothing_vowel_to_fric_fade_ms)
    lp.trajectory_limit_enabled = gb("trajectoryLimitEnabled", lp.trajectory_limit_enabled)
    lp.trajectory_limit_window_ms = gn("trajectoryLimitWindowMs", lp.trajectory_limit_window_ms)
    lp.trajectory_limit_apply_across_word_boundary = gb("trajectoryLimitApplyAcrossWordBoundary", lp.trajectory_limit_apply_across_word_boundary)
    lp.liquid_dynamics_enabled = gb("liquidDynamicsEnabled", lp.liquid_dynamics_enabled)
    lp.liquid_dynamics_lateral_onglide_f1_delta = gn("liquidDynamicsLateralOnglideF1Delta", lp.liquid_dynamics_lateral_onglide_f1_delta)
    lp.liquid_dynamics_lateral_onglide_f2_delta = gn("liquidDynamicsLateralOnglideF2Delta", lp.liquid_dynamics_lateral_onglide_f2_delta)
    lp.liquid_dynamics_lateral_onglide_duration_pct = gn("liquidDynamicsLateralOnglideDurationPct", lp.liquid_dynamics_lateral_onglide_duration_pct)
    lp.liquid_dynamics_rhotic_f3_dip_enabled = gb("liquidDynamicsRhoticF3DipEnabled", lp.liquid_dynamics_rhotic_f3_dip_enabled)
    lp.liquid_dynamics_rhotic_f3_minimum = gn("liquidDynamicsRhoticF3Minimum", lp.liquid_dynamics_rhotic_f3_minimum)
    lp.liquid_dynamics_rhotic_f3_dip_duration_pct = gn("liquidDynamicsRhoticF3DipDurationPct", lp.liquid_dynamics_rhotic_f3_dip_duration_pct)
    lp.liquid_dynamics_labial_glide_transition_enabled = gb("liquidDynamicsLabialGlideTransitionEnabled", lp.liquid_dynamics_labial_glide_transition_enabled)
    lp.liquid_dynamics_labial_glide_start_f1 = gn("liquidDynamicsLabialGlideStartF1", lp.liquid_dynamics_labial_glide_start_f1)
    lp.liquid_dynamics_labial_glide_start_f2 = gn("liquidDynamicsLabialGlideStartF2", lp.liquid_dynamics_labial_glide_start_f2)
    lp.liquid_dynamics_labial_glide_transition_pct = gn("liquidDynamicsLabialGlideTransitionPct", lp.liquid_dynamics_labial_glide_transition_pct)
    lp.phrase_final_lengthening_enabled = gb("phraseFinalLengtheningEnabled", lp.phrase_final_lengthening_enabled)
    lp.phrase_final_lengthening_final_syllable_scale = gn("phraseFinalLengtheningFinalSyllableScale", lp.phrase_final_lengthening_final_syllable_scale)
    lp.phrase_final_lengthening_penultimate_syllable_scale = gn("phraseFinalLengtheningPenultimateSyllableScale", lp.phrase_final_lengthening_penultimate_syllable_scale)
    lp.phrase_final_lengthening_statement_scale = gn("phraseFinalLengtheningStatementScale", lp.phrase_final_lengthening_statement_scale)
    lp.phrase_final_lengthening_question_scale = gn("phraseFinalLengtheningQuestionScale", lp.phrase_final_lengthening_question_scale)
    lp.phrase_final_lengthening_nucleus_only_mode = gb("phraseFinalLengtheningNucleusOnlyMode", lp.phrase_final_lengthening_nucleus_only_mode)
    lp.microprosody_enabled = gb("microprosodyEnabled", lp.microprosody_enabled)
    lp.microprosody_voiceless_f0_raise_enabled = gb("microprosodyVoicelessF0RaiseEnabled", lp.microprosody_voiceless_f0_raise_enabled)
    lp.microprosody_voiceless_f0_raise_hz = gn("microprosodyVoicelessF0RaiseHz", lp.microprosody_voiceless_f0_raise_hz)
    lp.microprosody_voiceless_f0_raise_end_hz = gn("microprosodyVoicelessF0RaiseEndHz", lp.microprosody_voiceless_f0_raise_end_hz)
    lp.microprosody_voiced_f0_lower_enabled = gb("microprosodyVoicedF0LowerEnabled", lp.microprosody_voiced_f0_lower_enabled)
    lp.microprosody_voiced_f0_lower_hz = gn("microprosodyVoicedF0LowerHz", lp.microprosody_voiced_f0_lower_hz)
    lp.microprosody_min_vowel_ms = gn("microprosodyMinVowelMs", lp.microprosody_min_vowel_ms)
    lp.rate_reduction_enabled = gb("rateReductionEnabled", lp.rate_reduction_enabled)
    lp.rate_reduction_schwa_reduction_threshold = gn("rateReductionSchwaReductionThreshold", lp.rate_reduction_schwa_reduction_threshold)
    lp.rate_reduction_schwa_min_duration_ms = gn("rateReductionSchwaMinDurationMs", lp.rate_reduction_schwa_min_duration_ms)
    lp.rate_reduction_schwa_scale = gn("rateReductionSchwaScale", lp.rate_reduction_schwa_scale)
    lp.nasalization_anticipatory_enabled = gb("nasalizationAnticipatoryEnabled", lp.nasalization_anticipatory_enabled)
    lp.nasalization_anticipatory_amplitude = gn("nasalizationAnticipatoryAmplitude", lp.nasalization_anticipatory_amplitude)
    lp.nasalization_anticipatory_blend = gn("nasalizationAnticipatoryBlend", lp.nasalization_anticipatory_blend)
    lp.positional_allophones_enabled = gb("positionalAllophonesEnabled", lp.positional_allophones_enabled)
    lp.positional_allophones_stop_aspiration_word_initial_stressed = gn("positionalAllophonesStopAspirationWordInitialStressed", lp.positional_allophones_stop_aspiration_word_initial_stressed)
    lp.positional_allophones_stop_aspiration_word_initial = gn("positionalAllophonesStopAspirationWordInitial", lp.positional_allophones_stop_aspiration_word_initial)
    lp.positional_allophones_stop_aspiration_intervocalic = gn("positionalAllophonesStopAspirationIntervocalic", lp.positional_allophones_stop_aspiration_intervocalic)
    lp.positional_allophones_stop_aspiration_word_final = gn("positionalAllophonesStopAspirationWordFinal", lp.positional_allophones_stop_aspiration_word_final)
    lp.positional_allophones_lateral_darkness_pre_vocalic = gn("positionalAllophonesLateralDarknessPreVocalic", lp.positional_allophones_lateral_darkness_pre_vocalic)
    lp.positional_allophones_lateral_darkness_post_vocalic = gn("positionalAllophonesLateralDarknessPostVocalic", lp.positional_allophones_lateral_darkness_post_vocalic)
    lp.positional_allophones_lateral_darkness_syllabic = gn("positionalAllophonesLateralDarknessSyllabic", lp.positional_allophones_lateral_darkness_syllabic)
    lp.positional_allophones_lateral_dark_f2_target_hz = gn("positionalAllophonesLateralDarkF2TargetHz", lp.positional_allophones_lateral_dark_f2_target_hz)
    lp.positional_allophones_glottal_reinforcement_enabled = gb("positionalAllophonesGlottalReinforcementEnabled", lp.positional_allophones_glottal_reinforcement_enabled)
    lp.positional_allophones_glottal_reinforcement_duration_ms = gn("positionalAllophonesGlottalReinforcementDurationMs", lp.positional_allophones_glottal_reinforcement_duration_ms)
    lp.hu_short_a_vowel_enabled = gb("huShortAVowelEnabled", lp.hu_short_a_vowel_enabled)
    lp.hu_short_a_vowel_key = gs("huShortAVowelKey", lp.hu_short_a_vowel_key)
    lp.hu_short_a_vowel_scale = gn("huShortAVowelScale", lp.hu_short_a_vowel_scale)
    lp.english_long_u_shorten_enabled = gb("englishLongUShortenEnabled", lp.english_long_u_shorten_enabled)
    lp.english_long_u_key = gs("englishLongUKey", lp.english_long_u_key)
    lp.english_long_u_word_final_scale = gn("englishLongUWordFinalScale", lp.english_long_u_word_final_scale)
    lp.default_pre_formant_gain = gn("defaultPreFormantGain", lp.default_pre_formant_gain)
    lp.default_output_gain = gn("defaultOutputGain", lp.default_output_gain)
    lp.default_vibrato_pitch_offset = gn("defaultVibratoPitchOffset", lp.default_vibrato_pitch_offset)
    lp.default_vibrato_speed = gn("defaultVibratoSpeed", lp.default_vibrato_speed)
    lp.default_voice_turbulence_amplitude = gn("defaultVoiceTurbulenceAmplitude", lp.default_voice_turbulence_amplitude)
    lp.default_glottal_open_quotient = gn("defaultGlottalOpenQuotient", lp.default_glottal_open_quotient)
    lp.strip_allophone_digits = gb("stripAllophoneDigits", lp.strip_allophone_digits)
    lp.strip_hyphen = gb("stripHyphen", lp.strip_hyphen)
    lp.tonal = gb("tonal", lp.tonal)
    lp.tone_digits_enabled = gb("toneDigitsEnabled", lp.tone_digits_enabled)
    mode = gs("toneContoursMode", "").lower()
    if mode == "relative": lp.tone_contours_absolute = False
    elif mode == "absolute": lp.tone_contours_absolute = True
    lp.tone_contours_absolute = gb("toneContoursAbsolute", lp.tone_contours_absolute)

    # Nested blocks
    if "trajectoryLimit" in s and isinstance(s["trajectoryLimit"], dict):
        tl = s["trajectoryLimit"]
        lp.trajectory_limit_enabled = _parse_bool(tl.get("enabled", lp.trajectory_limit_enabled))
        if "windowMs" in tl: lp.trajectory_limit_window_ms = float(tl["windowMs"])
        if "applyTo" in tl and isinstance(tl["applyTo"], list):
            mask = 0
            for n in tl["applyTo"]:
                if str(n) in FIELD_ID: mask |= (1 << FIELD_ID[str(n)])
            if mask: lp.trajectory_limit_apply_mask = mask
        if "maxHzPerMs" in tl and isinstance(tl["maxHzPerMs"], dict):
            for fn, v in tl["maxHzPerMs"].items():
                if fn in FIELD_ID:
                    lp.trajectory_limit_max_hz_per_ms[FIELD_ID[fn]] = float(v)

    if "liquidDynamics" in s and isinstance(s["liquidDynamics"], dict):
        ld = s["liquidDynamics"]
        lp.liquid_dynamics_enabled = _parse_bool(ld.get("enabled", lp.liquid_dynamics_enabled))
        if "lateralOnglide" in ld and isinstance(ld["lateralOnglide"], dict):
            lo = ld["lateralOnglide"]
            if "f1Delta" in lo: lp.liquid_dynamics_lateral_onglide_f1_delta = float(lo["f1Delta"])
            if "f2Delta" in lo: lp.liquid_dynamics_lateral_onglide_f2_delta = float(lo["f2Delta"])
            if "durationPct" in lo: lp.liquid_dynamics_lateral_onglide_duration_pct = float(lo["durationPct"])
        if "rhoticF3Dip" in ld and isinstance(ld["rhoticF3Dip"], dict):
            rd = ld["rhoticF3Dip"]
            lp.liquid_dynamics_rhotic_f3_dip_enabled = _parse_bool(rd.get("enabled", lp.liquid_dynamics_rhotic_f3_dip_enabled))
            if "f3Minimum" in rd: lp.liquid_dynamics_rhotic_f3_minimum = float(rd["f3Minimum"])
        if "labialGlideTransition" in ld and isinstance(ld["labialGlideTransition"], dict):
            wg = ld["labialGlideTransition"]
            lp.liquid_dynamics_labial_glide_transition_enabled = _parse_bool(wg.get("enabled", lp.liquid_dynamics_labial_glide_transition_enabled))

    if "positionalAllophones" in s and isinstance(s["positionalAllophones"], dict):
        pa = s["positionalAllophones"]
        lp.positional_allophones_enabled = _parse_bool(pa.get("enabled", lp.positional_allophones_enabled))


def _merge_norm(lp: LanguagePack, n: dict):
    if "aliases" in n and isinstance(n["aliases"], dict):
        for k, v in n["aliases"].items(): lp.aliases[str(k)] = str(v)
    if "classes" in n and isinstance(n["classes"], dict):
        for cn, items in n["classes"].items():
            if isinstance(items, list): lp.classes[str(cn)] = [str(x) for x in items]
    if "stripAllophoneDigits" in n: lp.strip_allophone_digits = _parse_bool(n["stripAllophoneDigits"])
    if "stripHyphen" in n: lp.strip_hyphen = _parse_bool(n["stripHyphen"])


def _merge_intonation(lp: LanguagePack, data: dict):
    for k, v in data.items():
        if k and k[0] in ".?!," and isinstance(v, dict):
            lp.intonation[k[0]] = _parse_intonation(v)


def _merge_tones(lp: LanguagePack, data: dict):
    for k, v in data.items():
        pts = [int(x) for x in v] if isinstance(v, list) else [int(v)] if isinstance(v, (int, float)) else []
        if pts: lp.tone_contours[str(k)] = pts


# =============================================================================
# Main loading functions
# =============================================================================

def find_packs_root(pack_dir: str) -> Path:
    p = Path(pack_dir)
    if (p / "phonemes.yaml").exists(): return p
    if (p / "packs" / "phonemes.yaml").exists(): return p / "packs"
    raise FileNotFoundError(f"phonemes.yaml not found under {pack_dir}")


def load_pack_set(pack_dir: str, lang_tag: str = "default") -> PackSet:
    """Load complete pack set with phonemes and merged language settings."""
    root = find_packs_root(pack_dir)
    pack = PackSet()

    # Load phonemes
    data = load_yaml_file(root / "phonemes.yaml")
    if data and "phonemes" in data:
        for k, v in data["phonemes"].items():
            if isinstance(v, dict): pack.phonemes[k] = _parse_phoneme(k, v)

    # Initialize language pack
    pack.lang = LanguagePack()
    pack.lang.lang_tag = lang_tag.lower().replace("_", "-")
    _apply_defaults(pack.lang)

    # Build chain: default -> base -> base-region
    chain = ["default"]
    parts = pack.lang.lang_tag.split("-")
    cur = ""
    for p in parts:
        cur = f"{cur}-{p}" if cur else p
        if cur not in chain: chain.append(cur)

    # Load each file
    for name in chain:
        lf = root / "lang" / f"{name}.yaml"
        if lf.exists():
            data = load_yaml_file(lf)
            if not data:
                continue
            if "settings" in data: _merge_settings(pack.lang, data["settings"])
            if "normalization" in data: _merge_norm(pack.lang, data["normalization"])
            if "intonation" in data: _merge_intonation(pack.lang, data["intonation"])
            if "toneContours" in data: _merge_tones(pack.lang, data["toneContours"])
            if "phonemes" in data:
                for k, v in data["phonemes"].items():
                    if isinstance(v, dict): pack.phonemes[k] = _parse_phoneme(k, v)

    return pack


# =============================================================================
# Utilities
# =============================================================================

def format_pack_summary(pack: PackSet) -> str:
    """Return a human-readable summary of the pack."""
    lp = pack.lang
    return f"""=== Pack: {lp.lang_tag} ===
Phonemes: {len(pack.phonemes)}

Timing:
  primaryStressDiv: {lp.primary_stress_div}
  secondaryStressDiv: {lp.secondary_stress_div}

Stop Closure:
  mode: {lp.stop_closure_mode}
  vowelGapMs: {lp.stop_closure_vowel_gap_ms}
  clusterGapMs: {lp.stop_closure_cluster_gap_ms}

Coarticulation:
  enabled: {lp.coarticulation_enabled}
  strength: {lp.coarticulation_strength}
  labialF2Locus: {lp.coarticulation_labial_f2_locus}
  alveolarF2Locus: {lp.coarticulation_alveolar_f2_locus}
  velarF2Locus: {lp.coarticulation_velar_f2_locus}

Trajectory Limiting:
  enabled: {lp.trajectory_limit_enabled}
  windowMs: {lp.trajectory_limit_window_ms}

Defaults:
  preFormantGain: {lp.default_pre_formant_gain}
  outputGain: {lp.default_output_gain}
"""


if __name__ == "__main__":
    import sys
    if len(sys.argv) < 2:
        print("Usage: python lang_pack.py <packs_dir> [lang_tag]")
        sys.exit(1)
    lang = sys.argv[2] if len(sys.argv) > 2 else "default"
    pack = load_pack_set(sys.argv[1], lang)
    print(format_pack_summary(pack))
