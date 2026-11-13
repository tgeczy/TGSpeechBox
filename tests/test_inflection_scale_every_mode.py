"""The inflection scale works in every pitch mode (#177).

It was written for the classic ("legacy") mode and also happened to act in
the Fujisaki mode, where people came to rely on it to widen or flatten the
melody without touching anything else.  In the default mode and in the Klatt,
impulse and Arató modes the slider did nothing.

Every mode multiplies its pitch movements by the inflection the host passes,
so the scale is one more factor on that value.  Its normal position is 0.58,
not 1 (it was tuned for the classic mode), so in the four modes that did not
have it the factor is scale / 0.58: at the normal position nothing changes,
half the scale is half the movement, twice is twice.  Classic and Fujisaki
read the scale themselves, as before.
"""
from __future__ import annotations

import ctypes
import pathlib

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
NORMAL = 0.58
NEW_MODES = ["espeak_style", "klatt_style", "impulse_style", "arato_style"]
OLD_MODES = ["legacy", "fujisaki_style"]
SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]
LANGS = sorted(p.stem for p in (REPO / "packs" / "lang").glob("*.yaml") if p.stem != "default")

# IPA as eSpeak NG writes it, with tie bars.
SENTENCES = {
    "en-us": "ðə kwˈɪk bɹˈa͡ʊn fˈɑːks d͡ʒˈʌmps ˌo͡ʊvɚ ðə lˈe͡ɪzi dˈɔːɡ",
    "hu": "ɒ ɟˈorʃ bˈɒrnɒ rˈoːkɒ ˈaːtuɡorjɒ ɒ lˈuʃtɒ kˈucaːt",
}


def _set_mode(fe, h, mode):
    fn = fe._dll.nvspFrontend_setPitchMode
    fn.argtypes, fn.restype = [ctypes.c_void_p, ctypes.c_char_p], ctypes.c_int
    assert fn(h, mode.encode("utf-8")) == 1


def _set_scale(fe, h, scale):
    fn = fe._dll.nvspFrontend_setLegacyPitchInflectionScale
    fn.argtypes, fn.restype = [ctypes.c_void_p, ctypes.c_double], None
    fn(h, ctypes.c_double(scale))


def _frames(fe, pack_dir, lang, mode, scale, ipa, speed=1.0, inflection=0.5, clause="."):
    h = fe.create_handle(pack_dir)
    try:
        fe.set_language(h, lang)
        _set_mode(fe, h, mode)
        if scale is not None:
            _set_scale(fe, h, scale)
        return fe.capture_frames(h, ipa, speed=speed, base_pitch=110.0, inflection=inflection, clause_type=clause)
    finally:
        fe.destroy_handle(h)


def _key(records, digits=4):
    return [(r.is_silence, round(r.duration_ms, 3), round(r.fade_ms, 3),
             None if r.frame_dict is None else tuple(round(v, digits) for v in r.frame_dict.values()))
            for r in records]


def _pitches(records):
    out = []
    for r in records:
        if r.frame_dict and r.frame_dict["voiceAmplitude"] > 0:
            out += [r.frame_dict["voicePitch"], r.frame_dict["endVoicePitch"]]
    return out


def _range(records):
    p = _pitches(records)
    return max(p) - min(p)


@pytest.mark.parametrize("lang", LANGS)
def test_every_pack_ships_the_normal_scale(lang, frontend, pack_dir):
    # 0.56 in five packs was the NVDA slider's rounding (#196) written back
    # into the files.  With the scale acting in every mode, a pack off the
    # normal value would move its default melody.
    import re
    text = (REPO / "packs" / "lang" / f"{lang}.yaml").read_text(encoding="utf-8")
    found = re.findall(r"^\s*legacyPitchInflectionScale:\s*([0-9.]+)", text, re.M)
    assert all(float(v) == NORMAL for v in found), f"{lang}: {found}"


@pytest.mark.parametrize("mode", NEW_MODES + OLD_MODES)
@pytest.mark.parametrize("lang", LANGS)
def test_at_the_normal_position_nothing_changes(frontend, pack_dir, lang, mode):
    ipa = SENTENCES.get(lang, SENTENCES["en-us"])
    for clause in ".?!,":
        untouched = _frames(frontend, pack_dir, lang, mode, None, ipa, clause=clause)
        normal = _frames(frontend, pack_dir, lang, mode, NORMAL, ipa, clause=clause)
        assert _key(untouched) == _key(normal), f"{lang} {mode} {clause!r}"


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("mode", NEW_MODES + ["legacy"])
@pytest.mark.parametrize("lang", ["en-us", "hu"])
def test_the_melody_widens_with_the_scale(frontend, pack_dir, lang, mode, speed):
    # (The Fujisaki mode's melody is made in the DSP from commands the frames
    # carry, not in the frames' pitch; the test after this one covers it.)
    ipa = SENTENCES[lang]
    for clause in ".?!":
        narrow, normal, wide = (_range(_frames(frontend, pack_dir, lang, mode, s, ipa, speed=speed, clause=clause))
                                for s in (NORMAL / 2, NORMAL, NORMAL * 2))
        assert narrow < normal < wide, f"{lang} {mode} {clause!r} at {speed}: {narrow:.1f} {normal:.1f} {wide:.1f} Hz"


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang", ["en-us", "hu"])
def test_the_fujisaki_mode_still_follows_the_scale(frontend, pack_dir, lang, speed):
    # As before #177: people use the scale there to flatten or widen the melody.
    ipa = SENTENCES[lang]
    low, normal, high = (_key(_frames(frontend, pack_dir, lang, "fujisaki_style", s, ipa, speed=speed))
                         for s in (NORMAL / 2, NORMAL, NORMAL * 2))
    assert low != normal and normal != high


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("mode", NEW_MODES)
@pytest.mark.parametrize("lang", ["en-us", "hu"])
def test_the_scale_is_one_more_factor_on_the_inflection(frontend, pack_dir, lang, mode, speed):
    # Twice the scale at inflection 0.4 is the normal scale at inflection 0.8.
    ipa = SENTENCES[lang]
    for clause in ".?":
        a = _frames(frontend, pack_dir, lang, mode, NORMAL * 2, ipa, speed=speed, inflection=0.4, clause=clause)
        b = _frames(frontend, pack_dir, lang, mode, NORMAL, ipa, speed=speed, inflection=0.8, clause=clause)
        assert _key(a, digits=2) == _key(b, digits=2), f"{lang} {mode} {clause!r} at {speed}"


@pytest.mark.parametrize("mode", NEW_MODES)
@pytest.mark.parametrize("lang", ["en-us", "hu"])
def test_zero_is_flat_and_the_extremes_stay_a_voice(frontend, pack_dir, lang, mode):
    ipa = SENTENCES[lang]
    flat = _pitches(_frames(frontend, pack_dir, lang, mode, 0.0, ipa))
    if mode != "arato_style":  # the Arató melodies keep their own fixed steps
        assert max(flat) - min(flat) < 0.01 * 110.0, f"{lang} {mode}: scale 0 is not flat"
    for clause in ".?!":
        for inflection in (0.5, 1.0):
            for scale in (0.02, 2.0):
                p = _pitches(_frames(frontend, pack_dir, lang, mode, scale, ipa, inflection=inflection, clause=clause))
                assert p and all(v == v and 30.0 <= v <= 500.0 for v in p), \
                    f"{lang} {mode} {clause!r} scale {scale} inflection {inflection}: {min(p):.1f} to {max(p):.1f} Hz"
