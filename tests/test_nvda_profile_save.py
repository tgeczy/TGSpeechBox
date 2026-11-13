"""What the NVDA add-on writes into a voice profile, and how (#195).

"Save Voice Profile Sliders to YAML..." used to hand thirteen slider values
to the frontend, which wrote all of them over the profile's voicingTone
block.  The add-on now writes the profile itself (profile_save.py), because
three things the frontend's function cannot do are needed: leave alone what
the listener did not move, keep the voice quality sliders out of the profile,
and give a profile made from a built-in voice that voice's shape as class
scales.

These tests need no NVDA and no DLL.  They hold:

* which values are written: only the moved sliders for a profile that has a
  voice source of its own, the whole voice source otherwise;
* the class scales a built-in voice's preset (constants.py) becomes, the way
  the phoneme editor's Save to Profile maps it
  (tools/tgsbPhonemeEditorWin32/profile_tone.cpp, seedClassScalesFromPreset),
  and that every preset key is either carried or named as not carried;
* the edit of phonemes.yaml: nothing outside the profile's own lines changes,
  line endings are kept, the phoneme editor's one-line voicingTone is
  understood, and a file the writer cannot read safely is refused untouched.

tests/nvda/test_driver_profile_save.py runs the button on the real driver.
"""
from __future__ import annotations

import importlib.util
import pathlib
import sys
import types

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
ADDON = REPO / "nvdaAddon" / "synthDrivers" / "tgSpeechBox"


def _load(name):
    spec = importlib.util.spec_from_file_location("_tgsb_" + name, ADDON / (name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


@pytest.fixture(scope="module")
def ps():
    return _load("profile_save")


@pytest.fixture(scope="module")
def voices():
    """The built-in voices' presets, from the add-on's constants.py."""
    saved = {k: sys.modules.get(k) for k in ("synthDriverHandler", "addonHandler")}
    sdh = types.ModuleType("synthDriverHandler")
    sdh.VoiceInfo = lambda *a, **k: None
    addon_handler = types.ModuleType("addonHandler")
    addon_handler.initTranslation = lambda: sys._getframe(1).f_globals.__setitem__("_", lambda s: s)
    sys.modules["synthDriverHandler"], sys.modules["addonHandler"] = sdh, addon_handler
    try:
        return _load("constants").voices
    finally:
        for k, v in saved.items():
            if v is None:
                sys.modules.pop(k, None)
            else:
                sys.modules[k] = v


# ---------------------------------------------------------------------------
# Which values are written
# ---------------------------------------------------------------------------

NEUTRAL = {"voiceTilt": 50, "noiseGlottalMod": 0, "pitchSyncF1": 50, "pitchSyncB1": 50,
           "speedQuotient": 50, "aspirationTilt": 50, "cascadeBwScale": 50, "voiceTremor": 0,
           "headSize": 50, "chorusDepth": 0, "chorusDetune": 33}


def _heard(ps, **over):
    """A built-in voice at neutral sliders, as the driver sends it to the
    player (speechPlayer.VoicingTone.defaults(), chorus detune 1.985 Hz from
    the slider's 33)."""
    tone = dict(ps.TONE_BLOCK_DEFAULTS, highShelfGainDb=4.0, chorusDetuneHz=1.985)
    tone.update(over)
    return tone


def test_the_sliders_are_the_ones_the_driver_composes(ps):
    # voicing_tone.py _applyVoicingTone composes exactly these eleven.
    assert {s[0]: s[2] for s in ps.SLIDERS} == NEUTRAL
    assert [s[1] for s in ps.SLIDERS] == [
        "voicedTiltDbPerOct", "noiseGlottalModDepth", "pitchSyncF1DeltaHz", "pitchSyncB1DeltaHz",
        "speedQuotient", "aspirationTiltDbPerOct", "cascadeBwScale", "tremorDepth",
        "f4FreqScale", "chorusDepth", "chorusDetuneHz"]
    assert all(key in ps.TONE_KEYS for _, key, _, _ in ps.SLIDERS)
    assert len(ps.TONE_KEYS) == 19 and set(ps.TONE_KEYS) == set(ps.TONE_BLOCK_DEFAULTS)


def test_no_voice_quality_slider_is_ever_written(ps):
    # Creakiness, breathiness, jitter, shimmer and sharpness are the
    # listener's, as in the phoneme editor (README-phoneme-editor.md).
    for name in ("creakiness", "breathiness", "jitter", "shimmer", "sharpness"):
        assert name not in ps.TONE_KEYS
        assert all(name.lower() not in s[0].lower() for s in ps.SLIDERS)


def test_an_untouched_profile_has_nothing_to_write(ps):
    beth = _heard(ps, voicedTiltDbPerOct=-10.0, speedQuotient=1.6, highShelfGainDb=5.5, chorusDetuneHz=2.0)
    assert ps.toneChanges(beth, NEUTRAL, True) == ({}, [])


def test_a_profile_gets_only_the_sliders_that_moved(ps):
    # Beth, tilt slider at 60: -10 + 4.8 is heard.  Speed quotient is where
    # it was, so its stored text ("1.60") is not rewritten.
    heard = _heard(ps, voicedTiltDbPerOct=-5.2, speedQuotient=1.6, highShelfGainDb=5.5, f4FreqScale=1.08)
    to_set, to_remove = ps.toneChanges(heard, dict(NEUTRAL, voiceTilt=60), True)
    assert to_set == {"voicedTiltDbPerOct": -5.2} and to_remove == []
    to_set, _ = ps.toneChanges(heard, dict(NEUTRAL, voiceTilt=60, headSize=40, chorusDepth=10), True)
    assert list(to_set) == ["voicedTiltDbPerOct", "f4FreqScale", "chorusDepth"]


def test_a_built_in_voice_gets_its_whole_voice_source(ps):
    # Every key is settled: written when it differs from what the frontend
    # fills in for a voicingTone block that leaves it out, removed when not.
    # The high shelf is the one that always differs: a block without it
    # plays 5.5 dB, a built-in voice 4 (nvspFrontend_getVoicingTone).
    to_set, to_remove = ps.toneChanges(_heard(ps), NEUTRAL, False)
    assert to_set == {"highShelfGainDb": 4.0}
    assert sorted(to_remove) == sorted(k for k in ps.TONE_KEYS if k != "highShelfGainDb")
    to_set, to_remove = ps.toneChanges(_heard(ps, voicedTiltDbPerOct=4.8, f4FreqScale=0.85),
                                       dict(NEUTRAL, voiceTilt=60, headSize=100), False)
    assert to_set == {"highShelfGainDb": 4.0, "voicedTiltDbPerOct": 4.8, "f4FreqScale": 0.85}
    assert "voicedTiltDbPerOct" not in to_remove and "f4FreqScale" not in to_remove


def test_a_resting_chorus_detune_slider_is_the_engines_two_hertz(ps):
    # The slider rests at 33, which its map makes 1.985 Hz; the engine's own
    # value is 2.  As in the phoneme editor, an untouched slider does not put
    # "chorusDetuneHz: 1.985" into a profile.
    to_set, to_remove = ps.toneChanges(_heard(ps), NEUTRAL, False)
    assert "chorusDetuneHz" not in to_set and "chorusDetuneHz" in to_remove
    to_set, _ = ps.toneChanges(_heard(ps, chorusDetuneHz=3.2), dict(NEUTRAL, chorusDetune=60), False)
    assert to_set["chorusDetuneHz"] == 3.2


@pytest.mark.parametrize("value,text", [
    (-10.0, "-10"), (1.6, "1.6"), (-5.2, "-5.2"), (4.0, "4"), (0.0, "0"), (-0.0, "0"),
    (1.985, "1.985"), (0.1 + 0.2, "0.3"), (2.0 * (1.6 / 2.0), "1.6"), (1.0 / 3.0, "0.333333"),
    (-0.0000001, "0"), (2000.0, "2000"),
])
def test_a_value_is_written_short_and_reads_back(ps, value, text):
    assert ps.formatValue(value) == text
    assert abs(float(text) - value) < 1e-6


# ---------------------------------------------------------------------------
# A built-in voice's shape as class scales
# ---------------------------------------------------------------------------

# The phoneme editor's seedClassScalesFromPreset, voice by voice; where the
# two differ the add-on's own preset (constants.py) decides, because that is
# the voice NVDA's listener hears: Robert's pf6 is 1.00 there.
EXPECTED_SCALES = {
    "Adam": {"cb_mul": [1.3, 1, 1, 1, 1, 1], "pa_mul": [1, 1, 1, 1, 1, 1.3],
             "fricationAmplitude_mul": 0.85},
    "Benjamin": {"cf_mul": [1.01, 1.02, 1, 1, 1, 1], "cb_mul": [1.3, 1, 1, 1, 1, 1],
                 "pa_mul": [1, 1, 1, 1, 1, 1.3], "fricationAmplitude_mul": 0.7},
    "Caleb": {"voiceAmplitude_mul": 0.0},
    "David": {"voicePitch_mul": 0.75, "endVoicePitch_mul": 0.75, "cf_mul": [0.90, 0.93, 0.95, 1, 1, 1]},
    "Robert": {"voicePitch_mul": 1.10, "endVoicePitch_mul": 1.10,
               "cf_mul": [1.02, 1.06, 1.08, 1.08, 1.10, 1.05],
               "cb_mul": [0.65, 0.68, 0.72, 0.75, 0.78, 0.80],
               "pf_mul": [1, 1, 1.06, 1.08, 1.10, 1.00],
               "pb_mul": [0.72, 0.75, 0.78, 0.80, 0.82, 0.85],
               "pa_mul": [1, 1, 1.08, 1.15, 1.20, 1.25],
               "voiceTurbulenceAmplitude_mul": 0.20, "fricationAmplitude_mul": 0.75,
               "vibratoPitchOffset_mul": 0.0, "vibratoSpeed_mul": 0.0},
}
# What has no class-scale form: absolute values (other than 0, which is the
# multiplier 0) and multipliers of fields the class scales do not have.
EXPECTED_NOT_CARRIED = {
    "Adam": [], "David": [],
    "Benjamin": ["cf4", "cf5", "cf6", "cfNP_mul"],
    "Caleb": ["aspirationAmplitude"],
    "Robert": ["glottalOpenQuotient", "parallelBypass_mul"],
}


def test_every_built_in_voice_is_covered(voices):
    assert sorted(voices) == sorted(EXPECTED_SCALES) == sorted(EXPECTED_NOT_CARRIED)


@pytest.mark.parametrize("voice", sorted(EXPECTED_SCALES))
def test_a_built_in_voices_shape_becomes_class_scales(ps, voices, voice):
    scales, not_carried = ps.presetClassScales(voices[voice])
    assert scales == EXPECTED_SCALES[voice]
    assert sorted(not_carried) == sorted(EXPECTED_NOT_CARRIED[voice])


@pytest.mark.parametrize("voice", sorted(EXPECTED_SCALES))
def test_every_preset_key_is_carried_or_named(ps, voices, voice):
    # Nothing in a preset may vanish silently: a key is in the class scales,
    # in the list the listener is shown, or part of the voice source (which
    # is saved from what is heard, not from the preset).
    import re
    scales, not_carried = ps.presetClassScales(voices[voice])
    for key, value in voices[voice].items():
        if key in ps.PRESET_VOICE_SOURCE_KEYS:
            continue
        formant = re.fullmatch(r"(cf|pf|cb|pb|pa)([1-6])_mul", key)
        if formant:
            carried = scales.get(formant[1] + "_mul", [None] * 6)[int(formant[2]) - 1] == value
        elif key.endswith("_mul"):
            carried = scales.get(key) == value
        else:
            carried = value == 0 and scales.get(key + "_mul") == 0
        assert carried != (key in not_carried), f"{voice}: {key} is neither carried nor named, or both"


# ---------------------------------------------------------------------------
# The edit of phonemes.yaml
# ---------------------------------------------------------------------------

YAML = """\
phonemes:
  a:
    cf1: 700
    voicingTone: 1   # not a profile: must never be touched

voiceProfiles:
  Beth:
    voicingTone:
      voicedTiltDbPerOct: -10.0   # bright
      speedQuotient: 1.60
      nasalBwScale: 1.30
    classScales:
      vowel:
        voicePitch_mul: 1.80
        cf_mul: [1.10, 1.15, 1.10, 0.94, 0.88, 0.82]
    phonemeOverrides: {}
  Edit:
    inflectionScale: 1.3
    voicingTone: {speedQuotient: 2.2, voicedTiltDbPerOct: -3}
    classScales:
      vowel:
        voicePitch_mul: 1.4
  Bare:
    classScales:
      vowel:
        voicePitch_mul: 0.9

afterProfiles:
  keep: this
"""


def _write(tmp_path, text=YAML, newline="\n"):
    path = tmp_path / "phonemes.yaml"
    path.write_bytes(text.replace("\n", newline).encode("utf-8"))
    return path


def _changed(before: bytes, after: bytes, newline=b"\n"):
    """(lines only in before, lines only in after), in file order."""
    a, b = before.split(newline), after.split(newline)
    return [x for x in a if x not in b], [x for x in b if x not in a]


@pytest.mark.parametrize("newline", ["\n", "\r\n"], ids=["lf", "crlf"])
def test_a_moved_slider_changes_one_line(ps, tmp_path, newline):
    path = _write(tmp_path, newline=newline)
    before = path.read_bytes()
    assert ps.saveProfile(str(path), "Beth", {"voicedTiltDbPerOct": -5.2}) is False  # not a new profile
    after = path.read_bytes()
    gone, new = _changed(before, after, newline.encode())
    assert gone == [b"      voicedTiltDbPerOct: -10.0   # bright"]
    assert new == [b"      voicedTiltDbPerOct: -5.2   # bright"]
    assert after.count(b"\r\n") == before.count(b"\r\n") and after.count(b"\n") == before.count(b"\n")


@pytest.mark.parametrize("newline", ["\n", "\r\n"], ids=["lf", "crlf"])
def test_a_key_the_profile_did_not_have_joins_its_block(ps, tmp_path, newline):
    path = _write(tmp_path, newline=newline)
    before = path.read_bytes()
    ps.saveProfile(str(path), "Beth", {"f4FreqScale": 1.08, "speedQuotient": 1.92})
    nl = newline.encode()
    gone, new = _changed(before, path.read_bytes(), nl)
    assert gone == [b"      speedQuotient: 1.60"]
    assert new == [b"      speedQuotient: 1.92", b"      f4FreqScale: 1.08"]
    lines = path.read_bytes().split(nl)
    # ...after the block's last key, before classScales.
    assert lines[lines.index(b"      nasalBwScale: 1.30") + 1] == b"      f4FreqScale: 1.08"
    assert lines[lines.index(b"      f4FreqScale: 1.08") + 1] == b"    classScales:"


def test_nothing_to_write_leaves_the_file_alone(ps, tmp_path):
    path = _write(tmp_path, newline="\r\n")
    before = path.read_bytes()
    assert ps.saveProfile(str(path), "Beth", {}) is False
    assert path.read_bytes() == before


def test_the_phoneme_editors_one_line_voice_source_is_updated_in_place(ps, tmp_path):
    # The editor writes "voicingTone: {a: 1, b: 2}".  The frontend's own
    # save put indented keys under that line, and the pack no longer loaded.
    path = _write(tmp_path)
    before = path.read_bytes()
    ps.saveProfile(str(path), "Edit", {"voicedTiltDbPerOct": 1.8, "tremorDepth": 0.12}, ["speedQuotient"])
    gone, new = _changed(before, path.read_bytes())
    assert gone == [b"    voicingTone: {speedQuotient: 2.2, voicedTiltDbPerOct: -3}"]
    assert new == [b"    voicingTone: {voicedTiltDbPerOct: 1.8, tremorDepth: 0.12}"]


def test_a_profile_without_a_voice_source_gets_one(ps, tmp_path):
    path = _write(tmp_path)
    before = path.read_bytes()
    ps.saveProfile(str(path), "Bare", {"highShelfGainDb": 4.0, "voicedTiltDbPerOct": 4.8},
                   classScales={"vowel": {"voicePitch_mul": 2.0}})
    gone, new = _changed(before, path.read_bytes())
    assert gone == []
    assert new == [b"      highShelfGainDb: 4", b"      voicedTiltDbPerOct: 4.8"]
    lines = path.read_bytes().split(b"\n")
    at = lines.index(b"  Bare:")
    assert lines[at + 1:at + 4] == [b"    voicingTone:", b"      highShelfGainDb: 4", b"      voicedTiltDbPerOct: 4.8"]
    # An existing profile keeps the class scales it has.
    assert lines[at + 4:at + 7] == [b"    classScales:", b"      vowel:", b"        voicePitch_mul: 0.9"]


def test_removed_keys_go_and_an_emptied_block_stays_a_block(ps, tmp_path):
    path = _write(tmp_path)
    ps.saveProfile(str(path), "Beth", {}, ["voicedTiltDbPerOct", "speedQuotient", "nasalBwScale", "tremorDepth"])
    lines = path.read_text(encoding="utf-8").split("\n")
    at = lines.index("  Beth:")
    # "voicingTone:" with nothing under it is no voice source to the frontend
    # (5.5 dB shelf becomes 4); "{}" keeps it one.
    assert lines[at + 1:at + 3] == ["    voicingTone: {}", "    classScales:"]


NEW_PROFILE = [
    "  David:",
    "    voicingTone:",
    "      highShelfGainDb: 4",
    "      f4FreqScale: 0.85",
    "    classScales:",
    "      vowel:",
    "        voicePitch_mul: 0.75",
    "        endVoicePitch_mul: 0.75",
    "        cf_mul: [0.9, 0.93, 0.95, 1, 1, 1]",
    "      consonant:",
    "        voicePitch_mul: 0.75",
    "        endVoicePitch_mul: 0.75",
    "        cf_mul: [0.9, 0.93, 0.95, 1, 1, 1]",
]


def _david(ps, voices):
    scales, _ = ps.presetClassScales(voices["David"])
    return {cls: scales for cls in ps.ROOT_CLASSES}


@pytest.mark.parametrize("newline", ["\n", "\r\n"], ids=["lf", "crlf"])
def test_a_new_profile_goes_at_the_end_of_the_profiles(ps, voices, tmp_path, newline):
    path = _write(tmp_path, newline=newline)
    before = path.read_bytes().decode("utf-8")
    created = ps.saveProfile(str(path), "David", {"highShelfGainDb": 4.0, "f4FreqScale": 0.85},
                             classScales=_david(ps, voices))
    assert created is True
    after = path.read_bytes().decode("utf-8")
    old, new = before.split(newline), after.split(newline)
    at = old.index("        voicePitch_mul: 0.9") + 1  # the last line of the last profile
    assert new == old[:at] + NEW_PROFILE + old[at:]
    assert after.count("\r") == (len(new) - 1 if newline == "\r\n" else 0)


@pytest.mark.parametrize("ending", ["", "\n", "\n\n"], ids=["no-final-newline", "final-newline", "blank-line"])
def test_a_new_profile_when_the_profiles_end_the_file(ps, voices, tmp_path, ending):
    text = YAML.split("\nafterProfiles:")[0].rstrip("\n") + ending
    path = _write(tmp_path, text)
    ps.saveProfile(str(path), "David", {"highShelfGainDb": 4.0, "f4FreqScale": 0.85},
                   classScales=_david(ps, voices))
    assert path.read_text(encoding="utf-8") == text.rstrip("\n") + "\n" + "\n".join(NEW_PROFILE) + ending


def test_a_file_without_profiles_gets_the_section(ps, voices, tmp_path):
    text = "phonemes:\n  a:\n    cf1: 700\n"
    path = _write(tmp_path, text)
    ps.saveProfile(str(path), "David", {"highShelfGainDb": 4.0, "f4FreqScale": 0.85},
                   classScales=_david(ps, voices))
    assert path.read_text(encoding="utf-8") == text + "\nvoiceProfiles:\n" + "\n".join(NEW_PROFILE) + "\n"


def test_saving_twice_writes_the_same_file(ps, voices, tmp_path):
    path = _write(tmp_path, newline="\r\n")
    args = ("David", {"highShelfGainDb": 4.0, "f4FreqScale": 0.85})
    assert ps.saveProfile(str(path), *args, classScales=_david(ps, voices)) is True
    once = path.read_bytes()
    assert ps.saveProfile(str(path), *args, classScales=_david(ps, voices)) is False
    assert path.read_bytes() == once


@pytest.mark.parametrize("bad,why", [
    ("  Beth: {voicingTone: {speedQuotient: 2}}\n", "a whole profile on one line"),
    ("  Beth:\n    voicingTone: {speedQuotient: 2,\n      voicedTiltDbPerOct: 1}\n", "a voice source over several lines"),
    ("  Beth:\n    voicingTone: &tone\n      speedQuotient: 2\n", "an anchor"),
])
def test_what_cannot_be_edited_safely_is_refused_untouched(ps, tmp_path, bad, why):
    path = _write(tmp_path, "voiceProfiles:\n" + bad)
    before = path.read_bytes()
    with pytest.raises(ps.ProfileSaveError):
        ps.saveProfile(str(path), "Beth", {"voicedTiltDbPerOct": -5.2})
    assert path.read_bytes() == before, why
    assert sorted(p.name for p in tmp_path.iterdir()) == ["phonemes.yaml"]  # no temp file left


@pytest.mark.parametrize("name", ["", "a: b", "two\nlines", " padded"])
def test_a_name_that_is_not_a_plain_key_is_refused(ps, tmp_path, name):
    path = _write(tmp_path)
    before = path.read_bytes()
    with pytest.raises(ps.ProfileSaveError):
        ps.saveProfile(str(path), name, {"voicedTiltDbPerOct": 1.0})
    assert path.read_bytes() == before


@pytest.mark.parametrize("profiles,name,code,detail", [
    ("voiceProfiles:\n  Beth: {voicingTone: {speedQuotient: 2}}\n", "Beth", "profileOnOneLine", "Beth"),
    ("voiceProfiles: {Beth: {}}\n", "Beth", "profilesNotABlock", ""),
    ("voiceProfiles:\n  Beth:\n    voicingTone: {speedQuotient: 2,\n      voicedTiltDbPerOct: 1}\n", "Beth",
     "toneNotEditable", "voicingTone: {speedQuotient: 2,"),
    ("voiceProfiles:\n  Beth:\n    voicingTone: {speedQuotient}\n", "Beth",
     "toneUnreadable", "voicingTone: {speedQuotient}"),
    ("voiceProfiles:\n  Beth:\n    inflectionScale: 1.2\n", "a: b", "badName", "a: b"),
], ids=["a-profile-on-one-line", "profiles-not-a-block", "a-voice-source-over-several-lines",
        "a-voice-source-that-cannot-be-read", "a-name-that-is-not-a-key"])
def test_a_refusal_says_which_one_in_a_code(ps, tmp_path, profiles, name, code, detail):
    """The reason is shown in a dialog.  This module has no NVDA and no _(),
    so a refusal carries a code and what it is about, and the settings panel
    has the sentence in the listener's language
    (tests/nvda/test_driver_profile_save.py).  str() stays English, for a log."""
    path = _write(tmp_path, profiles)
    with pytest.raises(ps.ProfileSaveError) as refused:
        ps.saveProfile(str(path), name, {"voicedTiltDbPerOct": -5.2})
    assert (refused.value.code, refused.value.detail) == (code, detail)
    assert str(refused.value) and (not detail or detail in str(refused.value))


def test_every_refusal_has_a_code(ps):
    assert sorted(ps.REFUSALS) == ["badName", "profileOnOneLine", "profilesNotABlock",
                                   "toneNotEditable", "toneUnreadable"]


def test_a_file_is_put_back_whole(ps, tmp_path):
    # The undo of a save the frontend could not load (the panel's).
    path = _write(tmp_path, newline="\r\n")
    before = path.read_bytes()
    ps.saveProfile(str(path), "Beth", {"voicedTiltDbPerOct": -5.2})
    assert path.read_bytes() != before
    ps.writeFile(str(path), before)
    assert path.read_bytes() == before
    assert sorted(p.name for p in tmp_path.iterdir()) == ["phonemes.yaml"]


@pytest.mark.parametrize("voice", ["Adam", "Benjamin", "Caleb", "David", "Robert"])
def test_only_a_whisper_voice_is_one_a_profile_cannot_be(ps, voices, voice):
    """Caleb is no voicing and full aspiration on every frame.  Class scales
    hold neither safely (tests/nvda/test_driver_profile_save.py measures what
    the profile becomes), so the panel says so before it saves."""
    assert ps.isWhisper(voices[voice]) is (voice == "Caleb")
    assert ps.isWhisper({}) is False and ps.isWhisper({"voiceAmplitude_mul": 0.0}) is False


def test_the_repositorys_own_profiles_are_found_and_kept(ps, tmp_path):
    # The real file: CRLF or LF as checked out, Beth's block as shipped.
    src = (REPO / "packs" / "phonemes.yaml").read_bytes()
    path = tmp_path / "phonemes.yaml"
    path.write_bytes(src)
    nl = b"\r\n" if b"\r\n" in src else b"\n"
    ps.saveProfile(str(path), "Beth", {"voicedTiltDbPerOct": -5.2})
    gone, new = _changed(src, path.read_bytes(), nl)
    assert gone == [b"      voicedTiltDbPerOct: -10.0"] and new == [b"      voicedTiltDbPerOct: -5.2"]
    assert len(path.read_bytes().split(nl)) == len(src.split(nl))
