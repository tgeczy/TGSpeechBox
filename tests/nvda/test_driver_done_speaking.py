"""Speech cancelled while its audio is playing is not reported as done (#197).

NVDA paces speech on synthDoneSpeaking.  The driver renders much faster
than the audio plays, so for most of an utterance its audio thread is
waiting in WavePlayer.idle() for the tail to play out.  A cancel stops the
player, which ends that wait at once, and the thread went on to send
synthDoneSpeaking for the utterance that had just been cut off.  The check
for a cancel ran only before the wait.

The player here blocks in idle() until stop(), as NVDA's does while audio
is still playing.
"""
from __future__ import annotations

import threading
import time

TEXT = "The quick brown fox jumps over the lazy dog."


def _hold_idle_until_stop(player):
    """The next idle() blocks until stop(): the audio is still playing.
    Returns the event set when the audio thread enters that idle()."""
    entered, stopped = threading.Event(), threading.Event()
    real_idle, real_stop = player.idle, player.stop

    def idle():
        if entered.is_set():
            return real_idle()
        entered.set()
        assert stopped.wait(10.0), "nothing stopped the player"

    def stop():
        real_stop()
        stopped.set()

    player.idle, player.stop = idle, stop
    return entered


def _settle(seconds=0.25):
    time.sleep(seconds)


def test_a_cancel_while_the_tail_plays_sends_no_done(harness):
    d = harness.driver
    harness.speak(["Desktop"])  # the player exists and is idle
    waiting = _hold_idle_until_stop(harness.player)
    done_before = harness.done.count

    d.speak([TEXT])
    assert waiting.wait(10.0), "the audio thread never waited for the audio to finish"
    _settle()
    assert harness.done.count == done_before  # still playing
    d.cancel()
    _settle()
    assert harness.done.count == done_before, "synthDoneSpeaking was sent for speech that was cancelled"


def test_speech_that_finishes_sends_done_once(harness):
    d = harness.driver
    harness.speak(["Desktop"])
    done_before = harness.done.count
    d.speak([TEXT])
    assert harness.done.wait_for_count(done_before + 1, 15.0), "no synthDoneSpeaking for speech that finished"
    _settle()
    assert harness.done.count == done_before + 1


def test_a_cancel_then_new_speech_sends_done_for_the_new_speech_only(harness):
    """NVDA cancels and speaks the new focus straight away."""
    d = harness.driver
    harness.speak(["Desktop"])
    waiting = _hold_idle_until_stop(harness.player)
    done_before = harness.done.count

    d.speak([TEXT])
    assert waiting.wait(10.0)
    feeds_before = len(harness.player.feeds)
    d.cancel()
    d.speak(["Documents"])
    assert harness.done.wait_for_count(done_before + 1, 15.0), "no synthDoneSpeaking for the new speech"
    _settle()
    assert harness.done.count == done_before + 1, "the cancelled speech was reported as done too"
    assert any(data for _, data in harness.player.feeds[feeds_before:]), "the new speech was not played"
