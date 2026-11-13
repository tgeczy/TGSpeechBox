"""The packs' phoneme timing settings are read (#172).

Sixteen settings give the base length of each kind of phoneme, in ms at speed
1.0: how long the puff after p, t, k lasts, how long a tap is, how long the
second vowel of a tied diphthong is, and so on.  src/frontend/ipa_engine.cpp
used all of them, src/frontend/pack.h gave them defaults, the NVDA panel
offered them, and four packs set two of them, but mergeSettings in
src/frontend/pack.cpp had no line that read them: the engine always used the
defaults.

No pack moves with this.  Four packs had values for two of the settings
(Danish 45 ms and Brazilian Portuguese 15 ms of aspiration, en-us 35, Spanish
a 30 ms diphthong end), and each of those languages was tuned and approved by
ear while the engine played the 20 ms default.  Those lines are out of the
packs (#178), with a comment where each stood, so every language sounds as it
did; a pack that sets one of the sixteen from now on is heard.
"""
from __future__ import annotations

import ctypes
import os
import pathlib
import re
import shutil
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
_ARCH = "x86" if sys.maxsize <= 2 ** 32 else "x64"
os.environ.setdefault("TGSB_LIB_DIR", str(REPO / f"build-{_ARCH}-nvda" / "MinSizeRel"))
os.environ.setdefault("TGSB_PACKS_ROOT", str(REPO))
sys.path.insert(0, str(REPO / "python"))

from tgspeechbox.engine import Frontend  # noqa: E402

RATES = [0.5, 1.0, 1.6, 2.0, 3.0]

# The settings and their defaults in src/frontend/pack.h (ms at speed 1.0).
DEFAULTS = {
    "defaultVowelDurationMs": 60.0,
    "defaultFadeMs": 10.0,
    "postStopAspirationDurationMs": 20.0,
    "stopDurationMs": 6.0,
    "affricateDurationMs": 24.0,
    "voicelessFricativeDurationMs": 45.0,
    "voicedConsonantDurationMs": 30.0,
    "tapDurationMs": 14.0,
    "trillFallbackDurationMs": 40.0,
    "tiedVowelDurationMs": 40.0,
    "tiedFromVowelDurationMs": 20.0,
    "tiedFromVowelFadeMs": 20.0,
    "vowelBeforeLiquidDurationMs": 30.0,
    "vowelBeforeNasalDurationMs": 40.0,
    "fadeAfterLiquidMs": 25.0,
    "liquidFadeMs": 20.0,
}

# One phrase with every kind of phoneme the sixteen settings time: a stop, a
# tap, a trill, an affricate, a voiceless fricative, a nasal, a liquid, a tied
# diphthong, a vowel after a liquid, and an unstressed vowel before a liquid
# and before a nasal.
PHRASE = "pˈeɾo pˈero tʃˈiko sˈopa mˈano rˈei lˈa ˈala ˈapal ˈapan"


def _override(fe, snippet):
    lib = fe._lib
    lib.nvspFrontend_applySettingOverrides.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.nvspFrontend_applySettingOverrides.restype = ctypes.c_int
    assert lib.nvspFrontend_applySettingOverrides(fe._h, snippet.encode("utf-8")) == 1


def _comparable(values):
    # An unset field is NaN, and NaN is not equal to itself.
    return None if values is None else {k: (None if v != v else v) for k, v in values.items()}


def _frames(lang, ipa, speed, snippet="", packs_root=None):
    with Frontend(lang, packs_root) as fe:
        if snippet:
            _override(fe, snippet)
        return [(_comparable(f.params), _comparable(f.ex), f.duration_ms, f.fade_ms)
                for f in fe.frames(ipa, speed)]


def _phoneme_ms(lang, ipa, speed, key, snippet="", packs_root=None):
    """How long the last phoneme with this key lasts: its frames, up to the next phoneme."""
    with Frontend(lang, packs_root) as fe:
        if snippet:
            _override(fe, snippet)
        frames = fe.frames(ipa, speed)
        trace = fe.frame_trace()
    for n in range(len(trace) - 1, -1, -1):
        if trace[n]["phonemeKey"] == key:
            end = trace[n + 1]["frameIndex"] if n + 1 < len(trace) else len(frames)
            return sum(f.duration_ms for f in frames[trace[n]["frameIndex"]:end])
    raise AssertionError(f"{lang} {ipa}: no {key} in {[e['phonemeKey'] for e in trace]}")


# ---------------------------------------------------------------------------
# Each setting is read
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("key", sorted(DEFAULTS))
def test_each_timing_setting_changes_the_frames(key, speed):
    # Spanish is the base (a trill, auto-tied diphthongs).  The puff needs
    # aspiration switched on, the fallback trill length needs
    # trillModulationMs unset, and the two halves of a diphthong stay two
    # phonemes with the collapse off (collapsed, a fast diphthong sits on its
    # own floor whatever its halves were given).
    base = "postStopAspirationEnabled: true\ntrillModulationMs: 0\ndiphthongCollapseEnabled: false\n"
    values = dict(DEFAULTS)
    at_defaults = _frames("es", PHRASE, speed, base + "".join(f"{k}: {v}\n" for k, v in values.items()))
    values[key] *= 2
    doubled = _frames("es", PHRASE, speed, base + "".join(f"{k}: {v}\n" for k, v in values.items()))
    assert doubled != at_defaults, f"{key}: {values[key]} gave the same frames as {DEFAULTS[key]}"


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang", ["en-us", "da", "pt-br"])
def test_the_puff_follows_post_stop_aspiration_duration(lang, speed):
    ipa = {"en-us": "pˈɪn", "da": "tˈalə", "pt-br": "kˈazɐ"}[lang]
    at40 = _phoneme_ms(lang, ipa, speed, "h", "postStopAspirationDurationMs: 40\n")
    at80 = _phoneme_ms(lang, ipa, speed, "h", "postStopAspirationDurationMs: 80\n")
    assert at80 == pytest.approx(2 * at40, rel=0.02), f"{lang} {ipa} @{speed}: {at40:.1f} then {at80:.1f} ms"


@pytest.mark.parametrize("speed", RATES)
def test_the_diphthong_end_follows_tied_from_vowel_duration(speed):
    # Spanish "rei": e and i are tied and come out as one glide, so the
    # setting's 40 ms more (at 1.0, in a stressed syllable: times 1.25) are
    # 40 ms more of that glide, less what diphthongRateCompensation holds back.
    at20 = _phoneme_ms("es", "rˈei", speed, "e_es", "tiedFromVowelDurationMs: 20\n")
    at60 = _phoneme_ms("es", "rˈei", speed, "e_es", "tiedFromVowelDurationMs: 60\n")
    assert at60 > at20 + 20.0 / speed, f"@{speed}: {at20:.1f} then {at60:.1f} ms"


def test_a_pack_file_is_read_too(tmp_path):
    # The same through a pack on disk, as a pack author meets it.
    packs = tmp_path / "packs"
    (packs / "lang").mkdir(parents=True)
    shutil.copy(REPO / "packs" / "phonemes.yaml", packs / "phonemes.yaml")
    for f in (REPO / "packs" / "lang").glob("*.yaml"):
        shutil.copy(f, packs / "lang" / f.name)
    stock = _phoneme_ms("en-us", "pˈɪn", 1.0, "h", packs_root=str(tmp_path))
    en_us = packs / "lang" / "en-us.yaml"
    text = en_us.read_text(encoding="utf-8")
    assert text.count("\nsettings:\n") == 1
    en_us.write_text(text.replace("\nsettings:\n", "\nsettings:\n  postStopAspirationDurationMs: 60\n"),
                     encoding="utf-8")
    edited = _phoneme_ms("en-us", "pˈɪn", 1.0, "h", packs_root=str(tmp_path))
    assert edited == pytest.approx(3 * stock, rel=0.02), f"{stock:.1f} then {edited:.1f} ms"


# ---------------------------------------------------------------------------
# What the shipped packs give
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang, ipa", [("da", "tˈalə"), ("pt-br", "kˈazɐ")])
def test_danish_and_brazilian_puffs_are_what_they_were(lang, ipa, speed):
    # Their packs once asked for 45 and 15 ms and were never read; the
    # listeners approved what played, the 20 ms default, and that stays until
    # a speaker has heard another length.
    assert _frames(lang, ipa, speed) == _frames(lang, ipa, speed, "postStopAspirationDurationMs: 20\n")
    assert _frames(lang, ipa, speed) != _frames(lang, ipa, speed, "postStopAspirationDurationMs: 45\n")


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang", ["es", "es-mx"])
@pytest.mark.parametrize("ipa", ["rˈei", "aβlˈais", "komˈeis"])  # rei; habláis, coméis as eSpeak NG writes them
def test_spanish_diphthong_end_is_what_it_was(lang, ipa, speed):
    # The pack once asked for 30 ms and was never read: 20 ms is what Spanish
    # has always played.
    assert _frames(lang, ipa, speed) == _frames(lang, ipa, speed, "tiedFromVowelDurationMs: 20\n")


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang", ["es", "es-mx"])
@pytest.mark.parametrize("ipa", ["rˈe͡ɪ", "ˈa͡ʊnke", "fˈa͡ɪl", "rˈeɪna"])  # rey, aunque, fail, reina
def test_spanish_diphthongs_as_espeak_writes_them_are_what_they_were(lang, ipa, speed):
    # ɪ_es and ʊ_es are semivowels: the tied-from vowel length never timed them.
    shipped = _frames(lang, ipa, speed)
    for ms in (20, 60):
        assert shipped == _frames(lang, ipa, speed, f"tiedFromVowelDurationMs: {ms}\n")


@pytest.mark.parametrize("speed", RATES)
@pytest.mark.parametrize("lang", ["en-us", "en-gb", "en-au", "en-ca"])
def test_english_puff_is_what_it_was(lang, speed):
    # 20 ms at 1.0, stretched by the stressed syllable (1.05; en-ca 1.4):
    # the length every English user has heard.
    at_1x = 28.0 if lang == "en-ca" else 21.0
    puff = _phoneme_ms(lang, "pˈɪn", speed, "h")
    assert puff == pytest.approx(at_1x / speed, abs=0.05), f"{lang} @{speed}: {puff:.2f} ms"
    for ipa in ["pˈɪn", "tˈɑːp", "hˈæpi", "ɐpˈɪɹ"]:
        assert _frames(lang, ipa, speed) == _frames(lang, ipa, speed, "postStopAspirationDurationMs: 20\n")


def _settings_in(pack: pathlib.Path) -> dict:
    found = {}
    for line in pack.read_text(encoding="utf-8").splitlines():
        m = re.match(r"\s+([A-Za-z]+Ms):\s*([0-9.]+)", line)
        if m and m.group(1) in DEFAULTS:
            found[m.group(1)] = float(m.group(2))
    return found


def test_no_pack_moves_off_the_defaults():
    # Before #172 every language ran on the defaults, whatever its file said,
    # and each was tuned on them.  A pack that sets one of the sixteen now
    # changes how its language sounds, so none does until a speaker of that
    # language has heard the new value.
    off_default = {}
    for pack in sorted((REPO / "packs" / "lang").glob("*.yaml")):
        moved = {k: v for k, v in _settings_in(pack).items() if v != DEFAULTS[k]}
        if moved:
            off_default[pack.stem] = moved
    assert off_default == {}


# ---------------------------------------------------------------------------
# Guard: a field the engine times phonemes with has a reader
# ---------------------------------------------------------------------------

def test_every_field_the_timing_code_uses_has_a_reader():
    engine = (REPO / "src" / "frontend" / "ipa_engine.cpp").read_text(encoding="utf-8")
    pack_cpp = (REPO / "src" / "frontend" / "pack.cpp").read_text(encoding="utf-8")
    used = sorted(set(re.findall(r"\blang\.([A-Za-z0-9_]+Ms)\b", engine)))
    assert len(used) > 20  # a regex that finds nothing would pass
    unread = [f for f in used if not re.search(r'get(?:Num|Bool|Str)\(\s*"' + f + r'",\s*lp\.' + f + r"\)", pack_cpp)]
    assert unread == [], f"ipa_engine.cpp uses these LanguagePack fields, but pack.cpp never reads them: {unread}"


def test_the_defaults_here_are_the_ones_in_pack_h():
    pack_h = (REPO / "src" / "frontend" / "pack.h").read_text(encoding="utf-8")
    for key, value in DEFAULTS.items():
        m = re.search(r"\bdouble " + key + r" = ([0-9.]+);", pack_h)
        assert m and float(m.group(1)) == value, key
