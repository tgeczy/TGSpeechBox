"""Text the driver cannot take as it comes never leaves NVDA waiting.

NVDA waits for an utterance's last index before it speaks again
(speech/manager.py).  An utterance that the driver drops without reporting
its indexes therefore silences everything that comes without a key press
after it: say all stops, live regions, terminal output and notifications
queue unspoken, until the next key press cancels.

Half a surrogate pair did that.  Windows text is UTF-16, and a program that
cuts a string in the middle of an emoji, or NVDA moving by character inside
one, hands over a lone surrogate.  The driver encoded its text as strict
UTF-8 on the way to the frontend, the encode raised, the background thread
logged an error, and the utterance was gone: no audio, no index, no done.
Found by checking this repository against the audits of the maintainer's
other speech projects (an utterance ended by a fault there too).

Two things are held here.  The broken unit is dropped and the rest is spoken
as if it had not been there.  And whatever else may go wrong inside one
utterance, its indexes and its done are still reported and the next
utterance speaks.
"""
from __future__ import annotations

import time

import pytest

from conftest import follow_wasapi_rule, voiced_frames

LONE_HIGH = "\ud83d"   # the first half of an emoji
LONE_LOW = "\ude00"    # the second half


def _watch_indexes(harness):
    seen = []
    real = harness.index.notify

    def notify(**kw):
        seen.append(kw.get("index"))
        return real(**kw)

    harness.index.notify = notify
    return seen, lambda: setattr(harness.index, "notify", real)


def _errors():
    import logHandler
    return [m for level, m in logHandler.log.messages if level == "error"]


@pytest.mark.parametrize("broken,clean", [
    (f"alpha {LONE_HIGH} bravo", "alpha  bravo"),
    (f"alpha{LONE_LOW} bravo", "alpha bravo"),
    (f"{LONE_HIGH}alpha bravo", "alpha bravo"),
    (f"alpha bravo{LONE_HIGH}", "alpha bravo"),
    (f"alpha {LONE_LOW}{LONE_HIGH} bravo", "alpha  bravo"),   # the two halves the wrong way round
], ids=["high in the middle", "low inside a word", "at the start", "at the end", "reversed pair"])
def test_half_an_emoji_is_dropped_and_the_rest_is_spoken(harness, speak_recorded, broken, clean):
    from speech.commands import IndexCommand
    harness.speak(["Desktop"])
    want, _ = speak_recorded([clean, IndexCommand(7000)])
    assert len(voiced_frames(want)) > 10, "the comparison utterance said nothing"
    seen, restore = _watch_indexes(harness)
    errors_before = len(_errors())
    try:
        got, pcm = speak_recorded([broken, IndexCommand(7001)])   # raises if no done comes
        time.sleep(0.2)
    finally:
        restore()
    assert seen == [7001], "the utterance's index was not reported exactly once"
    assert got == want, "the frames are not those of the same text without the broken unit"
    assert len(pcm) > 4000, "nothing was played"
    assert _errors()[errors_before:] == [], "the driver logged an error for it"


def test_a_lone_surrogate_and_nothing_else_still_reports_its_index(harness):
    from speech.commands import IndexCommand
    harness.speak(["Desktop"])
    seen, restore = _watch_indexes(harness)
    try:
        harness.speak([LONE_HIGH, IndexCommand(7002)])
        time.sleep(0.2)
    finally:
        restore()
    assert seen == [7002]


def test_two_halves_in_the_right_order_are_the_character_they_stand_for(harness, speak_recorded):
    """A pair that arrives as two units is one character, and is spoken as
    that character: it must not be thrown away with the lone ones.  The
    character here is a mathematical bold A (U+1D400), which is read as the
    letter; a bare emoji says nothing in this harness (NVDA names emoji
    before the synthesizer sees them), so it could not show the difference."""
    harness.speak(["Desktop"])
    want, _ = speak_recorded(["alpha \U0001D400 bravo"])
    got, _ = speak_recorded(["alpha \ud835\udc00 bravo"])
    assert got == want
    plain, _ = speak_recorded(["alpha  bravo"])
    assert len(voiced_frames(want)) > len(voiced_frames(plain)), "the character itself is not spoken, so this test shows nothing"


# ---------------------------------------------------------------------------
# Whatever goes wrong inside one utterance
# ---------------------------------------------------------------------------

def _fail_once(obj, name, when=1):
    """obj.name raises on its `when`-th call from now, and works otherwise."""
    real = getattr(obj, name)
    calls = [0]

    def wrapper(*a, **k):
        calls[0] += 1
        if calls[0] == when:
            raise RuntimeError("a fault injected by the test")
        return real(*a, **k)

    setattr(obj, name, wrapper)
    return lambda: setattr(obj, name, real)


def test_a_fault_before_anything_is_queued_still_reports_index_and_done(harness):
    from speech.commands import IndexCommand
    d = harness.driver
    harness.speak(["Desktop"])
    seen, restore_index = _watch_indexes(harness)
    done_before = harness.done.count
    restore = _fail_once(d._frontend, "prepareText")
    try:
        d.speak(["alpha bravo", IndexCommand(7101)])
        assert harness.done.wait_for_count(done_before + 1, 10.0), "NVDA would wait here until a key press"
        time.sleep(0.3)
    finally:
        restore()
        restore_index()
    assert seen == [7101]
    assert harness.done.count == done_before + 1, "done was sent more than once"
    assert any("fault injected" in m or "utterance" in m.lower() for m in _errors()), "the fault was not logged"


def test_a_fault_after_the_first_clause_plays_what_there_is_and_reports_once(harness):
    """The first clause is already playing when the second fails."""
    from speech.commands import IndexCommand
    d = harness.driver
    harness.speak(["Desktop"])
    seen, restore_index = _watch_indexes(harness)
    done_before = harness.done.count
    feeds_before = len(harness.player.feeds)
    restore = _fail_once(d._frontend, "prepareText", when=2)
    try:
        d.speak(["Alpha is first. Bravo is second.", IndexCommand(7102)])
        assert harness.done.wait_for_count(done_before + 1, 15.0), "NVDA would wait here until a key press"
        time.sleep(0.3)
    finally:
        restore()
        restore_index()
    assert seen == [7102]
    assert harness.done.count == done_before + 1
    assert sum(len(data) for _, data in harness.player.feeds[feeds_before:]) > 4000, "the first clause was not played"


def test_a_fault_after_a_cancel_reports_its_index_on_nvdas_own_player_rule(harness):
    """NVDA's WASAPI player calls an index back only while its stream is
    playing, and a cancel stops the stream (follow_wasapi_rule).  A failed
    utterance has queued nothing but its index: it must reach NVDA all the
    same, or the safety net would hold only on a player kinder than NVDA's."""
    from speech.commands import IndexCommand
    d = harness.driver
    harness.speak(["Desktop"])
    follow_wasapi_rule(harness.player)
    d.cancel()
    time.sleep(0.1)
    seen, restore_index = _watch_indexes(harness)
    done_before = harness.done.count
    restore = _fail_once(d._frontend, "prepareText")
    try:
        d.speak(["alpha bravo", IndexCommand(7104)])
        assert harness.done.wait_for_count(done_before + 1, 10.0), "NVDA would wait here until a key press"
        time.sleep(0.3)
    finally:
        restore()
        restore_index()
    assert seen == [7104]
    assert harness.done.count == done_before + 1


def test_the_utterance_after_a_fault_speaks_as_usual(harness, speak_recorded):
    d = harness.driver
    harness.speak(["Desktop"])
    want, _ = speak_recorded(["Charlie"])
    assert len(voiced_frames(want)) > 5
    done_before = harness.done.count
    restore = _fail_once(d._frontend, "prepareText")
    try:
        d.speak(["alpha bravo"])
        assert harness.done.wait_for_count(done_before + 1, 10.0)
    finally:
        restore()
    got, pcm = speak_recorded(["Charlie"])
    assert got == want
    assert len(pcm) > 4000, "nothing was played"


def test_a_fault_in_a_cancelled_utterance_reports_nothing(harness):
    """A cancelled utterance gets no done (#197), fault or no fault."""
    from speech.commands import IndexCommand
    d = harness.driver
    harness.speak(["Desktop"])
    seen, restore_index = _watch_indexes(harness)
    done_before = harness.done.count
    real = d._frontend.prepareText

    def cancel_then_fail(*a, **k):
        d.cancel()
        raise RuntimeError("a fault injected by the test")

    d._frontend.prepareText = cancel_then_fail
    try:
        d.speak(["alpha bravo", IndexCommand(7103)])
        time.sleep(1.0)
    finally:
        d._frontend.prepareText = real
        restore_index()
    assert seen == []
    assert harness.done.count == done_before
