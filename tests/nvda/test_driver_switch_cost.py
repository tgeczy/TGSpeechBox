"""Switching back to a language already spoken is instant (#131).

Edu, with NVDA's automatic language switching on, moving between an English
page and a Spanish one: "the delay happens every time the language changes,
not just the first time", next to eSpeak and OneCore, which switch at once.
Every switch reloaded the language pack from YAML (~110 ms into en-us,
~25 ms into most others) and re-selected the eSpeak voice.

The property: after a language has been spoken once, a switch costs no more
than eSpeak's own voice change (which NVDA's eSpeak driver pays on every
switch too: eSpeak holds one voice at a time and loads its dictionary on
each change, ~10 ms here) plus a few milliseconds.  Latency is speak() to
the first audio handed to the WavePlayer.
"""
from __future__ import annotations

import statistics
import time

import pytest

#: What a switch may add beyond eSpeak's own voice change.
ALLOWED_MS = 5.0


def _espeak_switch_ms(driver, a, b):
    """Median time eSpeak takes to change voice between a and b, measured
    here, on this machine."""
    times = []
    for _ in range(6):
        for want in (a, b):
            t0 = time.perf_counter()
            assert driver._setEspeakLangForSwitch(want)
            times.append((time.perf_counter() - t0) * 1000.0)
    driver._activeSpeechLang = None  # the engines no longer agree; the next block re-applies
    return statistics.median(times)


def _latency_ms(harness, sequence):
    harness.driver.cancel()
    _, latency, _ = harness.speak(sequence)
    assert latency is not None, f"no audio for {sequence!r}"
    return latency * 1000.0


@pytest.mark.parametrize("own,other", [("pt-br", "en"), ("en-us", "es"), ("es", "en")])
def test_switching_back_to_a_language_already_spoken_is_instant(harness, nvda_sequence, own, other):
    harness.driver.language = own
    word = {"pt-br": "agora", "en-us": "garden", "es": "gracias"}

    def in_own():
        return nvda_sequence(harness.driver, [word[own]])

    def in_other():
        return nvda_sequence(harness.driver, [word["en-us" if other == "en" else other]], doc_lang=other)

    # Both languages spoken once: the loads that may be slow are behind us.
    _latency_ms(harness, in_own())
    _latency_ms(harness, in_other())
    _latency_ms(harness, in_own())

    plain, switched = [], []
    for _ in range(4):
        plain.append(_latency_ms(harness, in_own()))
        plain.append(_latency_ms(harness, in_own()))
        switched.append(_latency_ms(harness, in_other()))
        switched.append(_latency_ms(harness, in_own()))
    extra = statistics.median(switched) - statistics.median(plain)
    espeak = _espeak_switch_ms(harness.driver, own, "en-us" if other == "en" else other)
    assert extra <= espeak + ALLOWED_MS, (
        f"switching between {own} and {other} adds {extra:.0f} ms per switch "
        f"(median {statistics.median(switched):.0f} ms against {statistics.median(plain):.0f} ms without a switch); "
        f"eSpeak's own voice change is {espeak:.0f} ms of that")
