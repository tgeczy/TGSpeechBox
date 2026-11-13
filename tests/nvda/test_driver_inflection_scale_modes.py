"""The inflection scale slider is called what it is, and moves the melody
in the default pitch mode (#177).

The scale was made for the classic pitch mode, and the voice panel's slider
said so: "Legacy pitch inflection scale (only active when pitch mode is
classic)".  Since #177 the engine applies it in every pitch mode
(tests/test_inflection_scale_every_mode.py), so the label told people the
slider did nothing where it now does.  It is "Inflection scale", in the
voice panel and in the TGSpeechBox panel's list of settings.  The setting's
id, its 0 to 100 range and its default of 29 (0.58 in the language file) are
as they were: NVDA's saved configurations name the id.

The second half is the label's claim, on the real driver: in the default
pitch mode (espeak_style) the slider widens and narrows the melody, leaves
the timing alone, and at 29 the frames are those of a slider nobody touched.
"""
from __future__ import annotations

import ast
import pathlib

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
ADDON = REPO / "nvdaAddon"
KEY = "legacyPitchInflectionScale"
LABEL = "Inflection scale"

SENTENCES = {
    "en-us": "The quick brown fox jumps over the lazy dog.",
    # The sentence of tests/test_inflection_scale_every_mode.py, as text.
    "hu": "A gyors barna róka átugorja a lusta kutyát.",
}


# ---------------------------------------------------------------------------
# The label
# ---------------------------------------------------------------------------

def test_the_voice_panel_slider_is_the_inflection_scale(harness):
    d = harness.driver
    setting = next(s for s in d.supportedSettings if s.id == KEY)
    assert setting.displayName == LABEL
    # What NVDA's saved configurations and the language files rest on.
    assert setting.defaultVal == 29 and d.legacyPitchInflectionScale == 29
    d.legacyPitchInflectionScale = 0
    assert d.legacyPitchInflectionScale == 0
    d.legacyPitchInflectionScale = 100
    assert d.legacyPitchInflectionScale == 100


def test_the_settings_panel_lists_it_by_the_same_name(harness):
    from synthDrivers.tgSpeechBox import langPackSettingsPanel as panel
    assert panel._displayNameForKey(KEY) == LABEL
    # The list shows names and edits keys: the name leads back to the key.
    keys = ["legacyPitchMode", KEY, "primaryStressDiv"]
    assert panel._keyForDisplayName(LABEL, keys) == KEY
    assert panel._displayNameForKey("legacyPitchMode") == "Legacy Pitch Mode"  # the others are made as before


def test_nothing_the_add_on_shows_says_it_is_for_the_classic_mode_only():
    said = []
    for path in sorted(ADDON.rglob("*.py")):
        for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"))):
            if isinstance(node, ast.Constant) and isinstance(node.value, str):
                text = node.value.lower()
                if "inflection scale" in text and ("legacy" in text or "classic" in text):
                    said.append(f"{path.relative_to(REPO)}:{node.lineno}  {node.value!r}")
    assert not said, "\n".join(said)


@pytest.mark.parametrize("po", sorted(ADDON.glob("locale/*/LC_MESSAGES/nvda.po")), ids=lambda p: p.parts[-3])
def test_the_old_labels_are_not_left_for_translators(po):
    text = po.read_text(encoding="utf-8")
    assert f'msgid "{LABEL}"' in text
    assert "Legacy pitch inflection scale" not in text and "Legacy Pitch Inflection Scale" not in text


# ---------------------------------------------------------------------------
# What the label says
# ---------------------------------------------------------------------------

def _recorder(harness, record_frames, lang):
    """Returns spoken(): the frames of the language's sentence, as
    ({field: value} of the frame, the rest of what was queued)."""
    from synthDrivers.tgSpeechBox import speechPlayer
    d = harness.driver
    fields = [name for name, _ in speechPlayer.Frame._fields_]

    def spoken():
        log = record_frames(d)
        d.cancel()
        harness.speak([SENTENCES[lang]])
        return [(None if frame is None else dict(zip(fields, frame)), rest) for frame, *rest in log]

    return spoken


def _pitch_range(frames):
    pitches = [f[name] for f, _ in frames if f and f["voiceAmplitude"] for name in ("voicePitch", "endVoicePitch")]
    return max(pitches) - min(pitches)


def _length_ms(frames):
    return sum(duration for _frame, (_frame_ex, duration, _fade) in frames)


def _pauses(frames):
    return sum(1 for frame, _ in frames if frame is None)


# The driver gives the frontend at most speed 2.0 and time-stretches the audio
# above it (speech_pipeline.py), so 3.0 asked of the driver makes the speech
# frames of 2.0; the last test holds that.
@pytest.mark.parametrize("speed", [0.5, 1.0, 1.6, 2.0, 3.0])
@pytest.mark.parametrize("lang", ["en-us", "hu"])
def test_the_slider_moves_the_melody_in_the_default_pitch_mode(harness, record_frames, lang, speed):
    d = harness.driver
    d.language = lang
    d._curRate = speed
    assert d.legacyPitchMode == "espeak_style"
    assert d.legacyPitchInflectionScale == 29  # as shipped: 0.58 in the language's file
    spoken = _recorder(harness, record_frames, lang)

    untouched = spoken()
    d.legacyPitchInflectionScale = 15
    narrow = spoken()
    d.legacyPitchInflectionScale = 58
    wide = spoken()
    d.legacyPitchInflectionScale = 29
    back = spoken()

    assert len(untouched) > 20
    # At 29 the voice is the one nobody touched, frame for frame.
    assert back == untouched
    # About half the scale is a narrower melody, twice is a wider one...
    assert _pitch_range(narrow) < _pitch_range(untouched) < _pitch_range(wide), \
        f"{_pitch_range(narrow):.1f} {_pitch_range(untouched):.1f} {_pitch_range(wide):.1f} Hz"
    # ...and the timing stays: the utterance is as long, with the same pauses.
    # (Not the same frames with another pitch: the frontend cuts a diphthong
    # into steps as long as the pitch period allows, and a fade is at least
    # two glottal cycles, frame_emit.cpp; both follow the pitch.)
    for moved in (narrow, wide):
        assert abs(_length_ms(moved) - _length_ms(untouched)) < 0.05
        assert _pauses(moved) == _pauses(untouched)


@pytest.mark.parametrize("scale", [15, 29, 58])
@pytest.mark.parametrize("lang", ["en-us", "hu"])
def test_above_speed_2_the_speech_frames_are_those_of_speed_2(harness, record_frames, lang, scale):
    """The frames the frontend makes.  The silence the driver itself puts
    after a clause is as long as the rate says, so it is not the same."""
    d = harness.driver
    d.language = lang
    d.legacyPitchInflectionScale = scale
    spoken = _recorder(harness, record_frames, lang)
    d._curRate = 2.0
    at_2 = spoken()
    d._curRate = 3.0
    at_3 = spoken()
    assert len(at_3) == len(at_2) > 20
    assert [x for x in at_3 if x[0] is not None] == [x for x in at_2 if x[0] is not None]
