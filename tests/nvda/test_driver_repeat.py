"""Speaking the same thing again sounds the same again (#127).

Edu, on the NVDA add-on only: typing "agota" and having NVDA read it back,
"the first time it was fine ... after that, it went back to how it was
before: everything sounds the same, all the consonants blur together again."

Two things made a word depend on what was spoken before it, and both are
pinned here:

* the language a read-back was switched into (test_driver_dialect.py);
* the frontend's stream state.  It puts a short gap between chunks of one
  utterance, and remembered across utterances whether it had spoken and how
  the last chunk ended, so after the first utterance every word started with
  an extra 20 ms of silence, or not, depending on the word before it.  An
  interruption (NVDA's cancel) now starts a new stream.

"The same" means what the listener gets: the same frames reach the DSP, and
the audio has the same length and spectrum.  It is not byte-identical: the
voice source runs on from one utterance into the next (its phase), which no
one can hear and a synth should not reset.

The eSpeak is NVDA's own, from TGSB_NVDA_DIR: point that at another NVDA
(a beta, a portable copy) to check its eSpeak the same way.
"""
from __future__ import annotations

import numpy as np
import pytest


def _say(harness, frames, sequence, interrupt):
    """One utterance as NVDA sends it.  `interrupt` is NVDA cancelling what
    was speaking before it speaks something new (focus moves, typed
    characters with "interrupt speech while typing" on)."""
    if interrupt:
        harness.driver.cancel()
    start = len(frames)
    pcm, _, _ = harness.speak(sequence)
    return pcm, frames[start:]


def _trimmed(pcm, level=100):
    x = np.frombuffer(pcm, dtype=np.int16).astype(float)
    loud = np.flatnonzero(np.abs(x) >= level)
    return x[loud[0]:loud[-1] + 1] if len(loud) else x[:0]


def _band_db(x, rate=22050):
    """Energy in octave bands from 125 Hz up, in dB."""
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1.0 / rate)
    edges = [125, 250, 500, 1000, 2000, 4000, 8000, 11025]
    return np.array([10 * np.log10(spec[(f >= lo) & (f < hi)].sum() + 1e-9) for lo, hi in zip(edges, edges[1:])])


def _assert_same(word, label, first, now):
    (pcm0, frames0), (pcm, frames) = first, now
    assert frames == frames0, (
        f"{label} of {word!r} reached the DSP as different frames from the first reading "
        f"({len(frames)} frames, first had {len(frames0)}; first difference at "
        f"{next((i for i, (a, b) in enumerate(zip(frames0, frames)) if a != b), min(len(frames0), len(frames)))})")
    a, b = _trimmed(pcm0), _trimmed(pcm)
    # Timing is the frames' business (a stream gap is 20 ms of frames); the
    # audio's audible length may differ by one pitch period, where the last
    # pulse of a fading vowel happens to fall.
    ms = abs(len(a) - len(b)) * 1000.0 / 22050
    assert ms <= 12.0, f"{label} of {word!r} is {ms:.1f} ms longer or shorter than the first reading"
    # The voiced bands must agree within the just-noticeable difference for
    # level (about 1 dB); the voice source's phase at onset moves them a
    # little.  Above 2 kHz the frication and aspiration noise is random, a new
    # draw every time, so a few dB there is the noise, not a different sound.
    n = min(len(a), len(b))
    diff = np.abs(_band_db(a[:n]) - _band_db(b[:n]))
    low, high = float(diff[:4].max()), float(diff[4:].max())
    assert low <= 1.0, f"{label} of {word!r} differs from the first reading by {low:.1f} dB below 2 kHz"
    assert high <= 3.0, f"{label} of {word!r} differs from the first reading by {high:.1f} dB above 2 kHz"


WORDS = [("pt-br", "agota"), ("pt-br", "agora"), ("pt-br", "fora"), ("en-us", "garden")]


@pytest.mark.parametrize("lang,word", WORDS)
def test_typing_a_word_then_reading_it_back(harness, record_frames, lang, word):
    """Edu's sequence: the word, its letters echoed as typed, then the word
    read back three times, NVDA interrupting before each."""
    harness.driver.language = lang
    frames = record_frames(harness.driver)

    first = _say(harness, frames, [word], interrupt=True)
    assert first[0], "the first reading produced no audio"
    for ch in word:
        _say(harness, frames, [ch], interrupt=True)
    for n in range(3):
        _assert_same(word, f"read-back {n + 1}", first, _say(harness, frames, [word], interrupt=True))


@pytest.mark.parametrize("lang,word", [("pt-br", "agota"), ("en-us", "garden")])
def test_after_an_interruption_a_word_does_not_depend_on_the_last_one(harness, record_frames, lang, word):
    """Arrowing through a list: each item follows a different one."""
    harness.driver.language = lang
    frames = record_frames(harness.driver)
    _say(harness, frames, ["casa" if lang.startswith("pt") else "idea"], interrupt=True)
    after_vowel = _say(harness, frames, [word], interrupt=True)
    _say(harness, frames, ["sol" if lang.startswith("pt") else "stop"], interrupt=True)
    after_consonant = _say(harness, frames, [word], interrupt=True)
    _assert_same(word, "the reading after a consonant-final word", after_vowel, after_consonant)


@pytest.mark.parametrize("lang,word", WORDS)
def test_repeating_a_word_without_interrupting(harness, record_frames, lang, word):
    """The same word spoken back to back, each finishing before the next, as
    in say-all or with interruption off.  Without an interruption the chunks
    are one stream, so each reading follows the one before it: warm up with
    the word itself."""
    harness.driver.language = lang
    frames = record_frames(harness.driver)
    _say(harness, frames, [word], interrupt=False)

    first = _say(harness, frames, [word], interrupt=False)
    for n in range(3):
        _assert_same(word, f"repeat {n + 1}", first, _say(harness, frames, [word], interrupt=False))
