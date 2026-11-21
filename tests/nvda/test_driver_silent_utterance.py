"""An utterance that says nothing still reports its index.

NVDA waits for an utterance's last index before it speaks again.  Its WASAPI
player calls a feed's callback only while the stream is playing, and a feed
of no audio does not start a stopped stream (follow_wasapi_rule in
conftest.py has the lines of NVDA's source).  A driver that gives an index
as an empty feed therefore loses it whenever nothing audible went before:
after a cancel, or once NVDA's idle check has closed the stream.  In the
maintainer's SSI-263 project that left say all, live regions and
notifications unspoken until the next key press, for any text the unit was
silent on.

This driver does not have the fault.  An index is a marker frame in the DSP,
which renders one sample for it, and the audio thread puts 3 ms of silence
before the first block after a stop (audio.py, _applyFadeInEnvelope).  So
even an utterance of nothing but its index feeds 134 bytes of audio at
22,050 Hz, and the index rides on that.  Checked by running the driver
against a player that follows NVDA's rule; this test keeps it checked,
because nothing else says that those 67 samples are what NVDA's speech
depends on.

The texts are ones NVDA does hand over: sentence marks are kept at every
symbol level, and a zero-width space is not white space to NVDA.
"""
from __future__ import annotations

import itertools
import time

import pytest

from conftest import follow_wasapi_rule

SILENT = {
    "three dots": "...",
    "a question mark": "?",
    "a dash": "-",
    "a closing bracket": ")",
    "a zero-width space": "​",
}
_index = itertools.count(8100)


def _speak_and_collect(harness, text):
    """Speak `text` with an index after it; returns (the indexes reported, done was sent)."""
    from speech.commands import IndexCommand
    number = next(_index)
    seen = []
    real = harness.index.notify

    def notify(**kw):
        seen.append(kw.get("index"))
        return real(**kw)

    harness.index.notify = notify
    try:
        done_before = harness.done.count
        harness.driver.speak([text, IndexCommand(number)])
        done = harness.done.wait_for_count(done_before + 1, 5.0)
        time.sleep(0.25)
    finally:
        harness.index.notify = real
    return seen, number, done


@pytest.mark.parametrize("name", list(SILENT))
def test_after_a_cancel_a_silent_utterance_reports_its_index(harness, name):
    """Every key press cancels: this is the usual state of the stream."""
    harness.speak(["Desktop"])
    follow_wasapi_rule(harness.player)
    harness.driver.cancel()
    time.sleep(0.1)
    seen, number, done = _speak_and_collect(harness, SILENT[name])
    assert done, "no synthDoneSpeaking"
    assert seen == [number], "NVDA would wait here for an index that never comes"


@pytest.mark.parametrize("name", list(SILENT))
def test_after_the_stream_was_closed_for_idleness_a_silent_utterance_reports_its_index(harness, name):
    """Ten seconds without speech and NVDA closes the stream; a live region
    that then says "..." must not silence what follows it."""
    harness.speak(["Desktop"])
    close_stream = follow_wasapi_rule(harness.player)
    harness.speak(["Documents"])
    close_stream()
    seen, number, done = _speak_and_collect(harness, SILENT[name])
    assert done, "no synthDoneSpeaking"
    assert seen == [number]


def test_while_the_stream_plays_a_silent_utterance_reports_its_index(harness):
    harness.speak(["Desktop"])
    follow_wasapi_rule(harness.player)
    harness.speak(["Documents"])
    seen, number, done = _speak_and_collect(harness, "...")
    assert done and seen == [number]


def test_the_rule_is_really_in_force(harness):
    """The stand-in does drop an index given with no audio to a stopped
    stream: without that, the tests above would pass for any driver."""
    harness.speak(["Desktop"])
    follow_wasapi_rule(harness.player)
    harness.driver.cancel()
    called = []
    harness.player.feed(b"", onDone=lambda: called.append("empty feed"))
    harness.player.idle()
    assert called == []
    harness.player.feed(b"\x00\x00", onDone=lambda: called.append("audio"))
    harness.player.idle()
    assert called == ["audio"]
