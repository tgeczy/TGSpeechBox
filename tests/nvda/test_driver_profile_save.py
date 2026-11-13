"""'Save Voice Profile Sliders to YAML...' saves the voice that is heard (#195).

Since #124 the voice sliders are offsets on a profile: in the middle, the
profile's stored value plays; moved, the slider adds to it or multiplies it
(voicing_tone.py, _applyVoicingTone).  The Save button still wrote the slider
positions as if they were the values:

* Beth, nothing moved: her tilt of -10 was replaced by 0, her speed quotient
  of 1.6 by 2.0, and she no longer sounded like Beth.
* Beth, tilt slider at 60: -5.2 is heard, 4.8 was written, and after the
  reload the slider's 4.8 was added again.
* a built-in voice: a profile was written that nothing listed until NVDA was
  restarted, without the voice's own shape (the preset's frame multipliers
  are not applied to a profile voice), and with a 5.5 dB high shelf where
  the built-in voice plays 4.

The button is pressed here on the real driver, with the real frontend
reading the file back.  What is compared is the voice source the DSP is
given (player.setVoicingTone), before the save and after it.
"""
from __future__ import annotations

import pathlib

import pytest

SAVE = "_onSaveVoiceProfileClick"
PROFILE = "profile:"

# The voice source: speechPlayer.VoicingTone's fields.
FIELDS = ("voicingPeakPos", "voicedPreEmphA", "voicedPreEmphMix",
          "highShelfGainDb", "highShelfFcHz", "highShelfQ",
          "voicedTiltDbPerOct", "noiseGlottalModDepth", "pitchSyncF1DeltaHz", "pitchSyncB1DeltaHz",
          "speedQuotient", "aspirationTiltDbPerOct", "cascadeBwScale", "tremorDepth",
          "nasalBwScale", "f4FreqScale", "nasalGainScale", "chorusDepth", "chorusDetuneHz")

# The sliders _applyVoicingTone composes with a profile, at rest.
NEUTRAL = {"voiceTilt": 50, "noiseGlottalMod": 0, "pitchSyncF1": 50, "pitchSyncB1": 50,
           "speedQuotient": 50, "aspirationTilt": 50, "cascadeBwScale": 50, "voiceTremor": 0,
           "headSize": 50, "chorusDepth": 0, "chorusDetune": 33}
QUALITY = ("frameExCreakiness", "frameExBreathiness", "frameExJitter", "frameExShimmer", "frameExSharpness")

MOVES = [
    {"voiceTilt": 60},                                      # the issue's example: -5.2 is heard
    {"voiceTilt": 35, "speedQuotient": 70},
    {"speedQuotient": 20},
    {"headSize": 30, "chorusDepth": 40, "chorusDetune": 60},
    {"noiseGlottalMod": 20, "pitchSyncF1": 60, "pitchSyncB1": 40, "aspirationTilt": 65,
     "cascadeBwScale": 70, "voiceTremor": 30},
    {"voiceTilt": 0, "speedQuotient": 100, "cascadeBwScale": 0, "voiceTremor": 100, "headSize": 0,
     "chorusDepth": 100, "chorusDetune": 100, "noiseGlottalMod": 100},   # into the clamps
]
_ids = ["+".join(sorted(m)) if len(m) < 4 else f"{len(m)}-sliders" for m in MOVES]


def _staged(d):
    return pathlib.Path(d._packsDir) / "phonemes.yaml"


@pytest.fixture
def heard(harness):
    """Returns heard(): the voice source the player holds for the current
    voice and sliders, applied afresh."""
    d = harness.driver
    given = []
    real = d._player.setVoicingTone

    def record(tone):
        given.append({f: getattr(tone, f) for f in FIELDS})
        return real(tone)

    d._player.setVoicingTone = record

    def now():
        del given[:]
        v = d.voice
        d._applyVoicingTone(v[len(PROFILE):] if v.startswith(PROFILE) else "")
        assert given, "the driver gave the player no voice source"
        return dict(given[-1])

    return now


def _sliders(d, names=NEUTRAL):
    return {name: getattr(d, name) for name in names}


def _same(a, b, skip=()):
    return {f: (a[f], b[f]) for f in FIELDS if f not in skip and abs(a[f] - b[f]) > 1e-6}


def _changed_lines(before: bytes, after: bytes):
    a, b = before.splitlines(), after.splitlines()
    return [x for x in a if x not in b], [x for x in b if x not in a]


# ---------------------------------------------------------------------------
# A profile voice
# ---------------------------------------------------------------------------

def test_an_untouched_profile_is_left_as_it_is(harness, panel, heard):
    d = harness.driver
    d.voice = "profile:Beth"
    file_before, tone_before = _staged(d).read_bytes(), heard()
    assert (tone_before["voicedTiltDbPerOct"], tone_before["speedQuotient"]) == (-10.0, 1.6)

    wx = panel(SAVE)

    assert _staged(d).read_bytes() == file_before, "Beth's stored voice was rewritten with no slider moved"
    assert not _same(heard(), tone_before)
    assert "Save Complete" not in wx.shown  # nothing was saved, and the listener is told so
    assert wx.messages and "Beth" in wx.messages[-1][1]


@pytest.mark.parametrize("moves", MOVES, ids=_ids)
def test_a_profile_saves_what_is_heard(harness, panel, heard, moves):
    d = harness.driver
    d.voice = "profile:Beth"
    for name, value in moves.items():
        setattr(d, name, value)
    file_before, tone_before = _staged(d).read_bytes(), heard()

    wx = panel(SAVE)

    assert "Save Complete" in wx.shown
    # The sliders are back at rest, so their offset is not applied a second time...
    assert _sliders(d) == NEUTRAL
    # ...the voice is the one that was heard...
    assert not _same(heard(), tone_before), "the voice changed with the save"
    # ...and it is the profile that holds it now (the frontend, reading the file back).
    stored = d._frontend.getVoicingTone()
    assert not _same({f: getattr(stored, f) for f in FIELDS}, tone_before)
    # Only the moved sliders' keys were written, in Beth's block.
    gone, new = _changed_lines(file_before, _staged(d).read_bytes())
    assert 1 <= len(new) <= len(moves) and len(gone) <= len(new)
    # The message says where the sliders went.
    done = next(text for title, text in wx.messages if title == "Save Complete")
    assert "Beth" in done and "neutral" in done


def test_saving_again_changes_nothing_more(harness, panel, heard):
    d = harness.driver
    d.voice = "profile:Beth"
    d.voiceTilt, d.speedQuotient = 60, 70
    tone_before = heard()
    panel(SAVE)
    file_once = _staged(d).read_bytes()

    wx = panel(SAVE)

    assert _staged(d).read_bytes() == file_once
    assert not _same(heard(), tone_before)
    assert "Save Complete" not in wx.shown


def test_nvdas_copy_of_the_sliders_follows(harness, panel, heard, nvda_replay_settings, driver_config_section):
    """NVDA keeps the sliders in its configuration and replays them: Cancel
    in the settings dialog is loadSettings(), a configuration profile switch
    replays what differs.  A stale 60 there would put the offset back on top
    of the saved value."""
    d = harness.driver
    d.voice = "profile:Beth"
    d.voiceTilt, d.headSize = 60, 40
    d.saveSettings()  # NVDA has them saved, as after OK or Apply
    assert driver_config_section()["voiceTilt"] == 60
    tone_before = heard()

    panel(SAVE)

    assert driver_config_section()["voiceTilt"] == 50 and driver_config_section()["headSize"] == 50
    d.loadSettings()  # Cancel
    assert _sliders(d) == NEUTRAL and not _same(heard(), tone_before)
    nvda_replay_settings(d)  # a configuration profile switch
    assert _sliders(d) == NEUTRAL and not _same(heard(), tone_before)
    assert d.voice == "profile:Beth"


def test_the_voice_quality_sliders_stay_the_listeners(harness, panel, heard):
    d = harness.driver
    d.voice = "profile:Beth"
    d.voiceTilt = 60
    quality = {"frameExCreakiness": 30, "frameExBreathiness": 20, "frameExJitter": 10,
               "frameExShimmer": 15, "frameExSharpness": 70}
    for name, value in quality.items():
        setattr(d, name, value)
    file_before = _staged(d).read_bytes()

    wx = panel(SAVE)

    assert "Save Complete" in wx.shown
    _, new = _changed_lines(file_before, _staged(d).read_bytes())
    assert new == [b"      voicedTiltDbPerOct: -5.2"]
    for word in (b"creakiness", b"breathiness", b"jitter", b"shimmer", b"sharpness"):
        assert _staged(d).read_bytes().count(word) == file_before.count(word)
    assert _sliders(d, QUALITY) == quality  # not written, and not reset either


def test_answering_no_saves_nothing(harness, panel, heard):
    d = harness.driver
    d.voice = "profile:Beth"
    d.voiceTilt = 60
    file_before, tone_before = _staged(d).read_bytes(), heard()

    wx = panel(SAVE, answer=8)  # wx.NO

    assert _staged(d).read_bytes() == file_before
    assert d.voiceTilt == 60 and not _same(heard(), tone_before)
    assert "Save Complete" not in wx.shown


def _restage(d, edit):
    """Rewrite the staged phonemes.yaml through `edit(text) -> text` and have
    the frontend read it."""
    path = _staged(d)
    raw = path.read_bytes()
    nl = "\r\n" if b"\r\n" in raw else "\n"
    text = edit(raw.decode("utf-8").replace(nl, "\n"))
    path.write_bytes(text.replace("\n", nl).encode("utf-8"))
    assert d.reloadLanguagePack()


def test_a_profile_the_phoneme_editor_wrote_is_saved_too(harness, panel, heard):
    """The editor writes a voice source on one line, "voicingTone: {...}".
    The frontend's own save put its thirteen keys under that line in block
    style; the pack then failed to load (reloadLanguagePack returned False)
    and the frontend went on with the one it had."""
    d = harness.driver
    block = ("    voicingTone:\n      voicedTiltDbPerOct: -10.0\n      noiseGlottalModDepth: 0.00\n"
             "      pitchSyncF1DeltaHz: 0.0\n      pitchSyncB1DeltaHz: 0.0\n      speedQuotient: 1.60\n"
             "      aspirationTiltDbPerOct: 0.00\n      nasalBwScale: 1.30\n      f4FreqScale: 1.08\n"
             "      nasalGainScale: 1.10\n")
    line = ("    voicingTone: {f4FreqScale: 1.08, nasalBwScale: 1.3, nasalGainScale: 1.1, "
            "speedQuotient: 1.6, voicedTiltDbPerOct: -10}\n")

    def edit(text):
        assert text.count(block) == 1, "Beth's voicingTone block is not as this test expects"
        return text.replace(block, line)

    _restage(d, edit)
    d.voice = "profile:Beth"
    d.voiceTilt, d.voiceTremor = 60, 30
    tone_before = heard()
    assert tone_before["voicedTiltDbPerOct"] == pytest.approx(-5.2)

    wx = panel(SAVE)

    assert "Save Complete" in wx.shown
    assert sorted(d._frontend.getVoiceProfileNames()) == ["Beth", "Bobby"]
    assert _sliders(d) == NEUTRAL and not _same(heard(), tone_before)
    text = _staged(d).read_text(encoding="utf-8")
    assert ("    voicingTone: {f4FreqScale: 1.08, nasalBwScale: 1.3, nasalGainScale: 1.1, "
            "speedQuotient: 1.6, voicedTiltDbPerOct: -5.2, tremorDepth: 0.12}") in text


def test_a_profile_without_a_voice_source_gets_the_one_heard(harness, panel, heard):
    """A profile with class scales only plays the engine's own voice source,
    and on it the sliders are the values (as on a built-in voice).  Saving
    gives it a voicingTone block; from then on the sliders are offsets, so
    they go back to rest."""
    d = harness.driver
    _restage(d, lambda text: text.rstrip("\n") + "\n  Plain:\n    classScales:\n      vowel:\n"
                                                 "        voicePitch_mul: 0.9\n")
    d.voice = "profile:Plain"
    d.voiceTilt, d.speedQuotient, d.headSize = 60, 30, 70
    tone_before = heard()
    assert tone_before["voicedTiltDbPerOct"] == pytest.approx(4.8) and tone_before["highShelfGainDb"] == 4.0

    wx = panel(SAVE)

    assert "Save Complete" in wx.shown
    assert d._frontend.hasExplicitVoicingTone()
    assert _sliders(d) == NEUTRAL
    assert not _same(heard(), tone_before, ("chorusDetuneHz",))  # no chorus: the detune is not heard


# ---------------------------------------------------------------------------
# A built-in voice
# ---------------------------------------------------------------------------

BUILT_IN = ["Adam", "Benjamin", "Caleb", "David", "Robert"]
NOT_CARRIED = {"Adam": [], "David": [], "Benjamin": ["cf4", "cf5", "cf6", "cfNP_mul"],
               "Caleb": ["aspirationAmplitude"], "Robert": ["glottalOpenQuotient", "parallelBypass_mul"]}


def _profile_lines(d, name):
    """The profile's lines in the staged file, without their indentation."""
    lines = _staged(d).read_text(encoding="utf-8").splitlines()
    at = lines.index(f"  {name}:")
    out = []
    for line in lines[at + 1:]:
        if line.strip() and len(line) - len(line.lstrip()) <= 2:
            break
        out.append(line.strip())
    return out


@pytest.mark.parametrize("voice", BUILT_IN)
@pytest.mark.parametrize("moves", [{}, {"voiceTilt": 60, "speedQuotient": 40, "chorusDepth": 25, "chorusDetune": 70}],
                         ids=["at-rest", "sliders-moved"])
def test_a_built_in_voice_becomes_a_profile_that_can_be_chosen(harness, panel, heard, voice, moves):
    d = harness.driver
    d.voice = voice
    for name, value in moves.items():
        setattr(d, name, value)
    sliders_before, tone_before = _sliders(d), heard()
    assert PROFILE + voice not in d.availableVoices

    wx = panel(SAVE)

    assert "Save Complete" in wx.shown
    # The built-in voice is as it was: it is not what was saved over.
    assert d.voice == voice and _sliders(d) == sliders_before and not _same(heard(), tone_before)
    # The profile is in the voice list now, not after a restart, under the
    # name the message gives.
    listed = d.availableVoices
    assert PROFILE + voice in listed and listed[PROFILE + voice].displayName == f"{voice} (profile)"
    done = next(text for title, text in wx.messages if title == "Save Complete")
    assert f"{voice} (profile)" in done
    # What a profile cannot hold is named, before the save and after it.
    asked = wx.messages[0][1]
    for key in NOT_CARRIED[voice]:
        assert key in asked and key in done
    if not NOT_CARRIED[voice]:
        assert "phoneme editor" not in done

    # Chosen, with its sliders at rest, it has the voice source that was heard.
    d.voice = PROFILE + voice
    assert _sliders(d) == NEUTRAL
    # (A chorus detune with no chorus depth is not heard; the slider's rest
    # position is written as the engine's 2 Hz, not the slider map's 1.985.)
    skip = () if moves else ("chorusDetuneHz",)
    assert not _same(heard(), tone_before, skip), "the profile does not have the built-in voice's source"

    # And it has the built-in voice's shape, as class scales on the two root classes.
    from synthDrivers.tgSpeechBox import constants, profile_save
    scales, _ = profile_save.presetClassScales(constants.voices[voice])
    lines = _profile_lines(d, voice)
    assert lines.count("classScales:") == 1 and lines.count("vowel:") == 1 and lines.count("consonant:") == 1
    for key, value in scales.items():
        text = ("[" + ", ".join(profile_save.formatValue(v) for v in value) + "]") if isinstance(value, list) \
            else profile_save.formatValue(value)
        assert lines.count(f"{key}: {text}") == 2, f"{key} is not on both vowel and consonant"
    assert not any(q in " ".join(lines) for q in ("creakiness", "breathiness", "jitter", "shimmer", "sharpness"))


@pytest.mark.parametrize("speed", [0.5, 1.0, 1.6, 2.0, 3.0])
@pytest.mark.parametrize("voice", BUILT_IN)
def test_the_profile_sounds_like_the_built_in_voice(harness, panel, record_frames, voice, speed):
    """The class scales reach the sound, at every rate.

    A built-in voice's preset is applied to every frame in the driver, a
    profile's class scales to each phoneme in the frontend before the frames
    are made, so the two are not frame for frame the same: transitions and
    inserted frames differ.  For each field the preset multiplies, at most a
    quarter of the frames may differ by more than 2% (measured: 16 of 109 at
    worst, Adam's and Benjamin's cb1 at rate 0.5).  Without the class scales
    every one of David's pitch frames would, by a third.

    At rate 0.5 David and Robert do not make the same number of frames as
    their profiles (109 against 103, 109 against 110), so there the frames
    cannot be compared one to one; the field's mean over the utterance,
    weighted by frame duration, must be within 3% (measured: 1.9% at worst,
    Robert's cb1).

    And the note the listener is shown is true: each preset value named as
    not carried does differ in the profile's frames."""
    from synthDrivers.tgSpeechBox import constants, speechPlayer
    d = harness.driver
    d._curRate = speed
    fields = [name for name, _ in speechPlayer.Frame._fields_]
    text = "The quick brown fox jumps over the lazy dog, she sells sea shells."

    def frames():
        log = record_frames(d)
        d.cancel()
        harness.speak([text])
        return [(dict(zip(fields, frame)), duration) for frame, _ex, duration, _fade in log if frame is not None]

    d.voice = voice
    built_in = frames()
    panel(SAVE)
    d.voice = PROFILE + voice
    profile = frames()
    assert min(len(profile), len(built_in)) > 50
    lined_up = len(profile) == len(built_in)
    assert lined_up or (speed == 0.5 and voice in ("David", "Robert")), \
        f"{voice} at {speed}: {len(built_in)} frames became {len(profile)}"

    def mean(utterance, field):
        held = [(frame[field], duration) for frame, duration in utterance if frame[field] is not None]
        return sum(value * duration for value, duration in held) / sum(duration for _, duration in held)

    not_carried = NOT_CARRIED[voice]
    checked = 0
    for key in constants.voices[voice]:
        field = key[:-4] if key.endswith("_mul") else key
        if field not in fields:
            continue
        was = [frame[field] for frame, _ in built_in if frame[field] is not None]
        now = [frame[field] for frame, _ in profile if frame[field] is not None]
        if key in not_carried:
            assert sorted(set(was)) != sorted(set(now)), \
                f"{voice}: {field} is named as not carried, but the frames have the same values"
        elif lined_up:
            pairs = [(a[field], b[field]) for (a, _), (b, _) in zip(built_in, profile)
                     if a[field] is not None and b[field] is not None]
            off = sum(1 for a, b in pairs if abs(a - b) > 0.02 * max(abs(a), abs(b), 1e-9))
            assert off <= len(pairs) // 4, f"{voice}: {field} differs in {off} of {len(pairs)} frames"
        else:
            a, b = mean(built_in, field), mean(profile, field)
            assert abs(a - b) <= 0.03 * max(abs(a), abs(b), 1e-9), f"{voice}: mean {field} {a} became {b}"
        checked += 1
    assert checked >= 1


def test_a_built_in_voice_saved_again_updates_its_profile(harness, panel, heard):
    d = harness.driver
    d.voice = "David"
    panel(SAVE)
    lines_once = _profile_lines(d, "David")
    assert "f4FreqScale: 0.85" in lines_once  # David's head size (the preset's slider default, 100)

    # The profile is used with a slider moved, then David is saved again, changed.
    d.voice = "profile:David"
    d.voiceTilt = 70
    d.voice = "David"
    d.voiceTilt, d.headSize = 30, 50
    tone_before = heard()
    wx = panel(SAVE)

    assert "Save Complete" in wx.shown
    text = _staged(d).read_text(encoding="utf-8")
    assert text.count("\n  David:") == 1, "a second David block was added"
    lines = _profile_lines(d, "David")
    assert "voicedTiltDbPerOct: -9.6" in lines and "f4FreqScale: 0.85" not in lines
    # Its class scales are left as they are.
    assert lines[lines.index("classScales:"):] == lines_once[lines_once.index("classScales:"):]
    # The profile's own slider offsets from before would be added to the new
    # values; they are back at rest, so the profile plays what was saved.
    d.voice = "profile:David"
    assert _sliders(d) == NEUTRAL
    assert not _same(heard(), tone_before, ("chorusDetuneHz",))


def test_other_profiles_and_the_rest_of_the_file_are_untouched(harness, panel):
    d = harness.driver
    before = _staged(d).read_bytes()
    d.voice = "Robert"
    panel(SAVE)
    after = _staged(d).read_bytes()
    nl = b"\r\n" if b"\r\n" in before else b"\n"
    old, new = before.split(nl), after.split(nl)
    added = len(new) - len(old)
    assert added > 0
    # The new block sits after the last profile; every line of the file as it
    # was is still there, in order.
    at = next(i for i, (a, b) in enumerate(zip(old, new)) if a != b) if old != new[:len(old)] else len(old)
    assert new[:at] + new[at + added:] == old
    assert new[at] == b"  Robert:"
    assert after.count(b"\r\n") - before.count(b"\r\n") == (added if nl == b"\r\n" else 0)
