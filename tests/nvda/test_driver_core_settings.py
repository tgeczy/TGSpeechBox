"""Rate, volume and inflection read back as the value that was set (#199).

The setters keep a float (the rate as a speed factor, the volume as a gain,
the inflection as a fraction) and the getters turned it back into the
slider's whole number with int(), which cuts the decimals off.  For some
values the float comes out a hair under the whole number: rate 57 read back
as 56.  NVDA saves what the getter returns, so the lower value went into the
configuration and was what the next start spoke with.
"""
from __future__ import annotations

import pytest

#: the slider values the issue lists as reading back one lower
ONE_LOWER = {"rate": [2, 3, 6, 11, 12, 14, 29, 42, 57, 84],
             "volume": [29, 57, 58],
             "inflection": [29, 58, 59]}


def _engine_value(driver, setting):
    return {"rate": driver._curRate, "volume": driver._curVolume,
            "inflection": driver._curInflection, "pitch": driver._curPitch}[setting]


@pytest.mark.parametrize("setting", ["rate", "volume", "inflection", "pitch"])
def test_every_value_reads_back_as_set(harness, setting):
    d = harness.driver
    wrong = {}
    for v in range(101):
        setattr(d, setting, v)
        if getattr(d, setting) != v:
            wrong[v] = getattr(d, setting)
    assert not wrong, f"{setting}: set -> read back: {wrong}"


@pytest.mark.parametrize("setting", ["rate", "volume", "inflection", "pitch"])
def test_what_the_engine_is_given_for_each_value_is_unchanged(harness, setting):
    """The fix is in reading the sliders back.  What each slider value means
    to the engine stays what it was."""
    d = harness.driver
    meaning = {"rate": lambda v: 0.25 * (2 ** (float(v) / 25.0)),
               "volume": lambda v: float(v) / 100.0,
               "inflection": lambda v: float(v) * 0.01,
               "pitch": lambda v: v}[setting]
    for v in range(101):
        setattr(d, setting, v)
        assert _engine_value(d, setting) == meaning(v), f"{setting} {v}"


@pytest.mark.parametrize("setting,value", [(s, v) for s in sorted(ONE_LOWER) for v in ONE_LOWER[s]])
def test_a_saved_value_is_the_value_spoken_with_after_a_reload(harness, record_frames, driver_config_section,
                                                               setting, value):
    """Set in the voice panel and saved with OK; then NVDA applies its saved
    settings again, as at the next start."""
    d = harness.driver
    d.language = "en-us"
    setattr(d, setting, value)
    chosen = _engine_value(d, setting)

    def frames():
        log = record_frames(d)
        d.cancel()
        harness.speak(["agota"])
        return list(log)

    spoken = frames()
    d.saveSettings()
    assert driver_config_section()[setting] == value, f"NVDA saved {setting} one step lower than chosen"
    d.loadSettings()
    assert getattr(d, setting) == value
    assert _engine_value(d, setting) == chosen
    assert frames() == spoken
