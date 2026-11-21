"""NVDA's BreakCommand is a pause of its length.

With "Delayed descriptions for characters on cursor movement" on (NVDA's
Speech settings, off by default), NVDA speaks a letter and then sends a
second utterance that starts with BreakCommand(1000): the description
("Bravo") is meant to come a second later, so that it is heard only when the
user stops on a character.  NVDA does not ask whether the synthesizer
supports the command; making the pause is the synthesizer's job, and eSpeak,
SAPI 5 and OneCore all make it.

The driver kept text, indexes, pitch and language changes and let every
other command fall through, so the description came straight after the
letter, on every character moved over.  Found by checking this repository
against the audit of the maintainer's SSI-263 project (the same fault there).

The pause is NVDA's number of milliseconds as heard, at every rate.  Above
2x the driver synthesizes at 2x and gives the rest to the DSP's time stretch,
which shortens a queued silence too, so the frame queued is longer by the
stretch (as for the clause pauses, test_driver_clause_pauses.py).
"""
from __future__ import annotations

import time

import pytest

SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]
BYTES_PER_SAMPLE = 2


def _stretch(rate):
    """The DSP time stretch the driver sets: what is left above 2x."""
    return max(rate / 2.0, 1.0)


def _seconds(harness, pcm):
    return len(pcm) / BYTES_PER_SAMPLE / harness.player.rate


def _silence_before_speech(frames):
    """Milliseconds of silence frames queued before the first frame with a voice in it."""
    total = 0.0
    for frame, _frame_ex, min_d, _fade in frames:
        if frame is not None:
            return total
        total += float(min_d)
    raise AssertionError("nothing was spoken at all")


def _at_rate(harness, speak_recorded, sequence, speed=1.0):
    """(frames, pcm) of `sequence` in English at `speed`."""
    harness.driver.language = "en-us"
    harness.driver._curRate = speed
    return speak_recorded(sequence)


def test_the_pause_is_made_without_being_declared(harness):
    """NVDA sends the command for delayed descriptions whether or not the
    synthesizer lists it.  Listing it would change something else: NVDA's
    maths reader (mathPres/MathCAT/speech.py) asks supportedCommands and
    would begin to put pauses into maths speech.  That is left as it is;
    this test fails the day somebody lists the command, so that it is a
    decision and not a side effect."""
    from speech.commands import BreakCommand
    assert BreakCommand not in harness.driver.supportedCommands


@pytest.mark.parametrize("speed", SPEEDS)
def test_a_break_before_the_text_is_heard_for_its_length_at_every_rate(harness, speak_recorded, speed):
    """What NVDA sends for a delayed character description."""
    from speech.commands import BreakCommand
    plain_frames, plain_pcm = _at_rate(harness, speak_recorded, ["Bravo"], speed)
    frames, pcm = _at_rate(harness, speak_recorded, [BreakCommand(1000), "Bravo"], speed)

    queued = _silence_before_speech(frames) - _silence_before_speech(plain_frames)
    assert queued == pytest.approx(1000.0 * _stretch(speed), abs=0.01), "the silence queued before the word"
    heard = _seconds(harness, pcm) - _seconds(harness, plain_pcm)
    assert heard == pytest.approx(1.0, abs=0.012), "the pause as it comes out of the DSP, in seconds"


def test_the_word_after_the_break_is_the_same_word(harness, speak_recorded):
    """The pause is added in front; the speech itself is not touched."""
    from speech.commands import BreakCommand
    plain_frames, _ = _at_rate(harness, speak_recorded, ["Bravo"])
    frames, _ = _at_rate(harness, speak_recorded, [BreakCommand(1000), "Bravo"])

    from conftest import voiced_frames
    assert len(voiced_frames(plain_frames)) > 5, "the comparison utterance said nothing"
    assert voiced_frames(frames) == voiced_frames(plain_frames)


@pytest.mark.parametrize("speed", SPEEDS)
def test_a_break_between_two_texts_separates_them(harness, speak_recorded, speed):
    from speech.commands import BreakCommand
    frames, _ = _at_rate(harness, speak_recorded, ["Alpha", BreakCommand(400), "Bravo"], speed)
    want = round(400.0 * _stretch(speed), 3)
    at = [n for n, (frame, _fx, min_d, _fd) in enumerate(frames) if frame is None and round(float(min_d), 3) == want]
    assert len(at) == 1, f"one silence frame of {want} ms was expected, found {len(at)}"
    assert any(f is not None for f, *_ in frames[:at[0]]), "nothing was spoken before the break"
    assert any(f is not None for f, *_ in frames[at[0] + 1:]), "nothing was spoken after the break"


@pytest.mark.parametrize("length", [0, -5, None])
def test_a_break_of_no_length_adds_nothing(harness, speak_recorded, length):
    from speech.commands import BreakCommand
    plain_frames, _ = _at_rate(harness, speak_recorded, ["Bravo"])
    frames, _ = _at_rate(harness, speak_recorded, [BreakCommand(length), "Bravo"])
    assert _silence_before_speech(frames) == _silence_before_speech(plain_frames)


def test_the_index_after_a_break_and_its_text_is_reported(harness):
    from speech.commands import BreakCommand, IndexCommand
    harness.speak(["Desktop"])
    seen = []
    real = harness.index.notify
    harness.index.notify = lambda **kw: (seen.append(kw.get("index")), real(**kw))[1]
    try:
        harness.speak([BreakCommand(200), "Bravo", IndexCommand(4321)])
        time.sleep(0.2)
    finally:
        harness.index.notify = real
    assert seen == [4321]


def test_a_cancel_during_a_long_break_ends_it(harness):
    """Moving on to the next character cancels the description that is
    still waiting out its second: nothing of it may be heard afterwards, and
    the next speech starts at once."""
    from speech.commands import BreakCommand
    d = harness.driver
    harness.speak(["Desktop"])
    done_before = harness.done.count
    d.speak([BreakCommand(5000), "Bravo"])
    time.sleep(0.3)
    d.cancel()
    t0 = time.perf_counter()
    _pcm, latency, _feeds = harness.speak(["Charlie"])
    assert latency is not None and latency < 1.0, "the next speech waited for the cancelled break"
    assert time.perf_counter() - t0 < 4.0
    time.sleep(0.2)
    assert harness.done.count == done_before + 1, "the cancelled break was reported as done"
