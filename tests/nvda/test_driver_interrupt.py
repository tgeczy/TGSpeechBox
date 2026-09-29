"""An interrupted utterance leaves nothing behind for the next one (#135).

Edu, on SAPI first, then "this is also happening in the NVDA add-on":
tabbing fast, a small residue of the previous item at the start of the next
one.  29-Bloo: under NVDA "a very slight delay, although there are not
always audio residues as in SAPI".

NVDA cancels whatever is speaking before it speaks the new focus.  The
property, the same one tests/sapi holds the SAPI engine to: the next item
starts exactly as it does when nothing was interrupted.  Before its own
onset there is only its own lead-in, and the onset lands where an
uninterrupted one does.

On a fast machine the leftover was a few milliseconds of timing.  On a slow
one, where a synthesize() call takes a while, it was the old voice itself:
the audio thread came back from a call it started before the cancel and fed
17 to 25 ms of the interrupted item ahead of the next one.  The slow case
stretches every synthesize() call by 40 ms to hold that window open.
"""
from __future__ import annotations

import time

import numpy as np
import pytest

LONG = ("The quick brown fox jumps over the lazy dog, and then it runs all the "
        "way back home again, slowly, through the long grass of the meadow.")
LEVEL = 400  # about -38 dBFS


def _pcm(data):
    return np.frombuffer(data, dtype=np.int16).astype(int)


def _onset(x):
    loud = np.flatnonzero(np.abs(x) >= LEVEL)
    return int(loud[0]) if len(loud) else len(x)


def _cut_mid_speech(harness, cut_ms):
    """Speak LONG and cancel it once `cut_ms` of its audio has gone to the
    WavePlayer, the way NVDA does when focus moves on."""
    player = harness.player
    before = len(player.feeds)
    harness.driver.speak([LONG])
    want = int(player.rate * 2 * cut_ms / 1000.0)
    deadline = time.perf_counter() + 10.0
    while sum(len(d) for _, d in player.feeds[before:]) < want:
        assert time.perf_counter() < deadline, "the long item never got that far"
        time.sleep(0.001)
    harness.driver.cancel()


def _slow_synthesis(player, seconds=0.04):
    fast, fast_aware = player.synthesize, player.synthesizeIndexAware

    def synthesize(n):
        out = fast(n)
        time.sleep(seconds)
        return out

    def synthesize_index_aware(n):
        out = fast_aware(n)
        time.sleep(seconds)
        return out

    player.synthesize, player.synthesizeIndexAware = synthesize, synthesize_index_aware


@pytest.mark.parametrize("slow", [False, True], ids=["fast-machine", "slow-machine"])
@pytest.mark.parametrize("rate", [20, 50, 80])
def test_an_interrupted_item_leaves_nothing_for_the_next(harness, rate, slow):
    if slow:
        _slow_synthesis(harness.driver._player)
    harness.driver.rate = rate
    rate_hz = harness.player.rate

    # Reference: an item spoken after another one that finished, with NVDA's
    # usual cancel in between.
    harness.driver.cancel()
    harness.speak(["Desktop"])
    harness.driver.cancel()
    ref, _, _ = harness.speak(["Documents"])
    ref = _pcm(ref)
    ref_onset = _onset(ref)
    assert ref_onset < len(ref)

    two_ms = int(rate_hz * 0.002)
    for cut_ms in (60, 150, 400):
        _cut_mid_speech(harness, cut_ms)
        nxt, _, _ = harness.speak(["Documents"])
        nxt = _pcm(nxt)
        nxt_onset = _onset(nxt)
        lead_in = max(ref_onset - two_ms, 0)
        leak = int(np.abs(nxt[:lead_in]).max()) if lead_in else 0
        shift_ms = (nxt_onset - ref_onset) * 1000.0 / rate_hz
        print(f"rate {rate} cut {cut_ms} ms: onset ref {ref_onset} next {nxt_onset} samples, "
              f"lead-in peak {leak}, shift {shift_ms:+.1f} ms")
        assert leak < LEVEL, f"audio from the interrupted item leaks into the next one (cut at {cut_ms} ms)"
        assert abs(shift_ms) <= 2.0, f"the next item starts {shift_ms:+.1f} ms off where it should (cut at {cut_ms} ms)"
