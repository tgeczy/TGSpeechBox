"""'.', '!', '?' and ',' in the Fujisaki, Klatt, classic and impulse modes (#155).

Two users reported it: outside the default mode "!" was nearly monotone and
"?" barely rose.  Measured before this work (110 Hz, inflection 0.5):

  fujisaki_style  the accent command lasted 340 ms whatever the rate (on a
                  32 ms vowel at rate 3.0), and the phrase component peaks
                  193 ms after the first frame, so a clause ended on both:
                  "manˈa." ended at 111 Hz, a sentence ending in a stressed
                  syllable at 115 Hz (122 Hz at rate 3.0), above where it
                  started, and with "!" at rate 3.0 it ended at 151 Hz, going
                  up.  After a question's rise a last consonant went back
                  down 30 Hz.
  klatt_style     "!" was "." to the last digit; the "?" and "," targets sat
                  below the hat plateau, so "ˈmana?" went from 123 to 121 Hz.
  legacy          "!" peaked 2 Hz above "." and had no shape of its own; a
                  one-word statement stressed on its last vowel ended on its
                  accent peak (100 Hz), and a question ending in a stressed
                  open syllable went from 130 to 128 Hz.
  impulse_style   "?" rose 0 to 6 Hz, "," not at all.

What a clause should do (the sizes are checked roughly, at five rates):
  statement and exclamation fall to about 77% of the baseline (110 to 85 Hz);
  a yes/no question rises on its last syllable to about 20% above the
    highest earlier peak;
  a comma has a continuation rise of 8 to 16 Hz on the last syllable.
O'Shaughnessy (1976), "Modelling fundamental frequency, and its relationship
to syntax, semantics, and phonetics", PhD thesis, MIT; Allen, Hunnicutt &
Klatt (1987), "From Text to Speech: The MITalk System", Cambridge University
Press.  That an exclamation starts higher and has stronger accents than a
statement is not from those: it is how this engine's default mode, which the
reporters call right, reads one, and the default mode must not change.

What the tests below do not ask for, because the modes do not do it:
  the comma of the classic and the Fujisaki mode is exactly what it was
    (held to recorded frames): it stays up, without a rise of its own;
  the classic question rises over its last word as it always did and ends
    about level with its accents; only a question that ends on a stressed
    vowel was changed;
  the questions rise in every language.  Hungarian, Greek and Romanian
    yes/no questions are a rise-fall (Ladd 2008, "Intonational Phonology",
    2nd ed., Cambridge University Press), which these four modes do not
    have yet.

The contour is computed from the frames: the voicePitch to endVoicePitch
ramp times the two Fujisaki filters of src/pitchModel.h, in closed form.
test_the_contour_is_what_the_dsp_plays holds that computation to the F0 of
the rendered audio, at two sample rates.

After the first fix for this issue a review found what the later sections
hold: the accents inside a Fujisaki sentence had shrunk, its comma had
changed, a word-initial last vowel started its fall twice, and nothing had
been run at another pitch or inflection than 110 Hz and 0.5.
"""
from __future__ import annotations

import ctypes
import functools
import math
import os
import pathlib
import shutil
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
_ARCH = "x86" if sys.maxsize <= 2 ** 32 else "x64"
os.environ.setdefault("TGSB_LIB_DIR", str(REPO / f"build-{_ARCH}-nvda" / "MinSizeRel"))
os.environ.setdefault("TGSB_PACKS_ROOT", str(REPO))
sys.path.insert(0, str(REPO / "python"))

from tgspeechbox.engine import Frontend, Player  # noqa: E402

MODES = ["fujisaki_style", "klatt_style", "legacy", "impulse_style"]
UNTOUCHED_MODES = ["espeak_style", "arato_style"]
SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]
# The reporters listen in Spanish and Portuguese; the maintainer in English and Hungarian.
LANGS = ["en-us", "es", "pt-br", "hu"]
# Sonorants only, so every pack can say them and the whole clause is voiced.
UTTERANCES = {
    "word, first syllable stressed": "ˈmana",
    "word, last syllable stressed": "manˈa",
    "sentence": "ˈmana ˈnama manˈa ˈlimo ˈmana",
    "sentence ending in a stressed open syllable": "ˈmana ˈnama ˈlimo manˈa",
    "sentence ending in a stressed closed syllable": "ˈmana ˈnama ˈlimo manˈan",
}
BASE = 110.0
REF_SR = 22050.0  # the Fujisaki lengths in a frame are samples at this rate


def _set_pitch_mode(fe, mode):
    fn = fe._lib.nvspFrontend_setPitchMode
    fn.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    fn.restype = ctypes.c_int
    assert fn(fe._h, mode.encode("utf-8")), mode


@functools.lru_cache(maxsize=None)
def _frontend(lang, mode, packs_root):
    """One frontend per language, mode and pack folder for the whole run:
    loading a pack takes far longer than any clause here, and every request
    for frames starts a new utterance."""
    fe = Frontend(lang, packs_root)
    _set_pitch_mode(fe, mode)
    return fe


def _frames(lang, mode, ipa, clause, speed, inflection=0.5, packs_root=None, pitch=BASE, scale=None):
    if scale is None:
        fe = _frontend(lang, mode, packs_root)
        frames = fe.frames(ipa, speed=speed, pitch=pitch, inflection=inflection, clause=clause)
        return frames, fe.frame_trace()
    with Frontend(lang, packs_root) as fe:  # its own: a scale stays set on a frontend
        _set_pitch_mode(fe, mode)
        fn = fe._lib.nvspFrontend_setLegacyPitchInflectionScale
        fn.argtypes, fn.restype = [ctypes.c_void_p, ctypes.c_double], None
        fn(fe._h, ctypes.c_double(scale))
        frames = fe.frames(ipa, speed=speed, pitch=pitch, inflection=inflection, clause=clause)
        trace = fe.frame_trace()
    return frames, trace


def _played_ms(f):
    """How long the DSP stays on a frame: a frame is not left before the fade
    into it is over (src/frame.cpp counts both from the same sample)."""
    return max(f.duration_ms, f.fade_ms)


class Contour:
    """F0 over a clause, from its frames, as src/voiceGenerator.h computes it."""

    def __init__(self, frames):
        self.segments = []  # (start ms, end ms, voicePitch, endVoicePitch)
        self.phrases = []   # (start ms, amplitude, samples to the peak)
        self.accents = []   # [start ms, amplitude, command samples, samples to the peak]
        self.frame_start = []  # ms, one per frame
        self._pieces = None
        t = 0.0
        last_reset = last_phrase = last_accent = 0.0
        for f in frames:
            self.frame_start.append(t)
            if f.is_silence:
                t += _played_ms(f)
                continue
            ex = f.ex or {}
            if ex.get("fujisakiEnabled", 0.0) > 0.5:
                # Commands fire on a rising edge (voiceGenerator.h).
                reset = ex["fujisakiReset"]
                if reset > 0.5 and last_reset <= 0.5:
                    self.phrases, self.accents = [], []
                    last_phrase = last_accent = 0.0
                last_reset = reset
                amp = ex["fujisakiPhraseAmp"]
                if amp > 0.0 and last_phrase <= 0.0:
                    self.phrases.append((t, amp, ex["fujisakiPhraseLen"] or 4250.0))
                last_phrase = amp
                amp = ex["fujisakiAccentAmp"]
                if amp > 0.0 and last_accent <= 0.0:
                    if self.accents:  # a new command replaces one still running
                        prev = self.accents[-1]
                        prev[2] = min(prev[2], (t - prev[0]) * REF_SR / 1000.0)
                    self.accents.append([t, amp, ex["fujisakiAccentDur"] or 7500.0,
                                         ex["fujisakiAccentLen"] or 1024.0])
                last_accent = amp
            # A pitch ramp ends on endVoicePitch, also when the fade into the
            # frame is longer than the frame (src/frame.cpp, #222).
            self.segments.append((t, t + _played_ms(f), f.params["voicePitch"], f.params["endVoicePitch"]))
            t += _played_ms(f)
        self.start_ms, self.end_ms = self.segments[0][0], self.segments[-1][1]

    def base(self, t):
        for t0, t1, p0, p1 in self.segments:
            if t < t1:  # a boundary belongs to the frame that starts there
                return p0 + (p1 - p0) * (t - t0) / (t1 - t0) if t1 > t0 else p1
        return self.segments[-1][3]

    def at(self, t):
        log_f0 = 0.0
        for t0, amp, length in self.phrases:
            n = (t - t0) * REF_SR / 1000.0
            if n >= 0.0:  # pitchModel.h designPhrase: a (n+1) r^n
                r = math.exp(-1.0 / length)
                p = math.exp(-math.e / length)
                log_f0 += amp * (1.0 - 2.0 * r * p + r * r * p) * (n + 1.0) * r ** n
        return self.base(t) * math.exp(log_f0 + self.accent_log(t))

    def _accent_pieces(self):
        """The accent filter of pitchModel.h as stretches of constant input:
        (first sample, r, input, A, B), where the output n samples in is
        input + (A + B n) r^n (a double pole at r, gain 1).  It is one
        filter, and every command designs it anew: what is left of the
        command before then decays with the new command's rise time."""
        if self._pieces is None:
            self._pieces = []
            events = []
            for t0, amp, dur, length in self.accents:
                n0 = t0 * REF_SR / 1000.0
                events += [(n0, amp, math.exp(-1.0 / length)), (n0 + dur, 0.0, None)]
            piece = None
            for n0, x, r in events:
                y1 = y2 = 0.0  # the two outputs before this stretch
                if piece is not None:
                    y1, y2 = self._piece_at(piece, n0 - 1.0), self._piece_at(piece, n0 - 2.0)
                    r = piece[1] if r is None else r
                b = (y1 - x) * r - (y2 - x) * r * r
                piece = (n0, r, x, (y1 - x) * r + b, b)
                self._pieces.append(piece)
        return self._pieces

    @staticmethod
    def _piece_at(piece, n):
        n0, r, x, a, b = piece
        return x + (a + b * (n - n0)) * r ** (n - n0)

    def accent_log(self, t):
        """The accent commands' share of log F0."""
        n = t * REF_SR / 1000.0
        current = None
        for piece in self._accent_pieces():
            if piece[0] > n:
                break
            current = piece
        return 0.0 if current is None else self._piece_at(current, n)

    def peak(self, t0, t1):
        n = max(1, int((t1 - t0) / 2.0))
        return max(self.at(t0 + (t1 - t0) * i / n) for i in range(n + 1))


@functools.lru_cache(maxsize=None)
def _measure(lang, mode, ipa, clause, speed):
    """start, the highest point before the last syllable's vowel, the pitch
    where that vowel starts, the end, and the highest point of all (Hz)."""
    frames, trace = _frames(lang, mode, ipa, clause, speed)
    c = Contour(frames)
    vowel_frames = [e["frameIndex"] for e in trace if e["phonemeKey"][0] not in "mnl"]
    last_vowel = c.frame_start[vowel_frames[-1]]
    return {"start": c.at(c.start_ms),
            "peak_before": c.peak(c.start_ms, last_vowel),
            "last_syllable": c.at(last_vowel),
            "end": c.at(c.end_ms),
            "peak": c.peak(c.start_ms, c.end_ms)}


def _show(m):
    return ("starts %.0f, peaks %.0f before the last syllable, %.0f where its vowel starts, ends %.0f Hz"
            % (m["start"], m["peak_before"], m["last_syllable"], m["end"]))


_matrix = [pytest.mark.parametrize("speed", SPEEDS),
           pytest.mark.parametrize("name", list(UTTERANCES)),
           pytest.mark.parametrize("lang", LANGS),
           pytest.mark.parametrize("mode", MODES)]


def _apply(marks):
    def deco(fn):
        for m in marks:
            fn = m(fn)
        return fn
    return deco


@_apply(_matrix)
def test_a_statement_falls(mode, lang, name, speed):
    m = _measure(lang, mode, UTTERANCES[name], ".", speed)
    where = f"{mode} {lang} @{speed} '{UTTERANCES[name]}.': {_show(m)}"
    assert 0.62 * BASE <= m["end"] <= 0.90 * BASE, where      # about 77% of the baseline
    assert m["end"] <= m["last_syllable"] - 3.0, where        # and the last syllable goes down


@_apply(_matrix)
def test_an_exclamation_falls_from_higher_up(mode, lang, name, speed):
    m = _measure(lang, mode, UTTERANCES[name], "!", speed)
    stop = _measure(lang, mode, UTTERANCES[name], ".", speed)
    where = f"{mode} {lang} @{speed} '{UTTERANCES[name]}!': {_show(m)}; the statement peaks at {stop['peak']:.0f} Hz"
    # The Fujisaki mode's exclamation falls further than a statement and than
    # the 77% (its own declination and last-vowel drop, as they were); a long
    # one ends on the floor all four modes share, 0.6 of the pitch.
    assert 0.6 * BASE <= m["end"] <= 0.92 * BASE, where
    # (the impulse mode's exclamation is on its floor before the last syllable)
    assert m["end"] <= m["last_syllable"] + 1.0, where
    assert m["peak"] >= 1.04 * stop["peak"], where            # higher start, stronger accents


@_apply(_matrix)
def test_a_question_ends_high(mode, lang, name, speed):
    m = _measure(lang, mode, UTTERANCES[name], "?", speed)
    where = f"{mode} {lang} @{speed} '{UTTERANCES[name]}?': {_show(m)}"
    assert m["end"] >= m["last_syllable"] + 10.0, where       # the last syllable rises
    assert m["end"] >= 1.10 * BASE, where
    # About 20% above the highest earlier peak is the target; the classic
    # mode's own question (unchanged here) ends level with its accents.
    floor = 0.95 if mode == "legacy" else 1.05
    assert m["end"] >= floor * m["peak_before"], where
    assert m["end"] <= 2.1 * BASE, where


@_apply(_matrix)
def test_a_comma_stays_up(mode, lang, name, speed):
    m = _measure(lang, mode, UTTERANCES[name], ",", speed)
    stop = _measure(lang, mode, UTTERANCES[name], ".", speed)
    where = f"{mode} {lang} @{speed} '{UTTERANCES[name]},': {_show(m)}; the statement ends at {stop['end']:.0f} Hz"
    assert m["end"] >= 1.10 * stop["end"], where
    assert m["end"] >= 0.90 * BASE, where
    if mode in ("klatt_style", "impulse_style"):
        # These two have a continuation-rise setting (8 to 16 Hz at full inflection).
        assert 3.0 <= m["end"] - m["last_syllable"] <= 20.0, where


# ---------------------------------------------------------------------------
# The comma of the Fujisaki and the classic mode is what it was
# ---------------------------------------------------------------------------

# Real phrases (IPA from eSpeak NG 1.53).  A reporter of #155 said the
# Fujisaki comma was right as it was, so these two modes' commas are held to
# the frames they gave before this work: clause_final_pitch_commas.json was
# recorded from the build without it (every frame's timing and pitch ramp
# and every Fujisaki field).
COMMA_PHRASES = {
    "es": {"Sí": "sˈi", "Hola": "ˈola", "No lo sé": "nˈo lo sˈe"},
    "pt-br": {"Obrigado": "ˌobɾiɡˈadʊ", "Você está aqui": "vosˈe estˌa akˈi"},
    "en-us": {"Hello world": "həlˈo͡ʊ wˈɜːld", "Can you help me with this": "kæn juː hˈɛlp mˌiː wɪð ðˈɪs"},
    "hu": {"Jó napot": "jˈoː nˈɑpot"},
}
COMMA_MODES = ["fujisaki_style", "legacy"]
FUJISAKI_FIELDS = ("fujisakiEnabled", "fujisakiReset", "fujisakiPhraseAmp", "fujisakiPhraseLen",
                   "fujisakiAccentAmp", "fujisakiAccentDur", "fujisakiAccentLen")
COMMAS_FILE = pathlib.Path(__file__).with_name("clause_final_pitch_commas.json")


def _comma_cases():
    """(mode, lang, phrase, pitch, inflection): every phrase at 110 Hz and
    0.5, each language's first phrase also low and wide, and high and narrow."""
    out = []
    for mode in COMMA_MODES:
        for lang, phrases in COMMA_PHRASES.items():
            for n, text in enumerate(phrases):
                out.append((mode, lang, text, BASE, 0.5))
                if n == 0:
                    out += [(mode, lang, text, 60.0, 1.0), (mode, lang, text, 220.0, 0.25)]
    return out


def _comma_key(mode, lang, text, pitch, inflection, speed):
    return f"{mode}|{lang}|{text}|{pitch:g}|{inflection:g}|{speed:g}"


def _comma_record(frames):
    """Per frame: duration, fade, and for a sound (not a silence) its pitch
    ramp and the Fujisaki fields, without the zeros they end in."""
    rows = []
    for f in frames:
        row = [round(f.duration_ms, 4), round(f.fade_ms, 4)]
        if not f.is_silence:
            ex = f.ex or {}
            row += [round(f.params["voicePitch"], 4), round(f.params["endVoicePitch"], 4)]
            row += [round(ex.get(k, 0.0), 4) for k in FUJISAKI_FIELDS]
            while row[-1] == 0.0:
                row.pop()
        rows.append(row)
    return rows


@functools.lru_cache(maxsize=None)
def _recorded_commas():
    import json
    return json.loads(COMMAS_FILE.read_text(encoding="utf-8"))


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("mode,lang,text,pitch,inflection", _comma_cases())
def test_the_fujisaki_and_the_classic_comma_are_what_they_were(mode, lang, text, pitch, inflection, speed):
    recorded = _recorded_commas()[_comma_key(mode, lang, text, pitch, inflection, speed)]
    frames, _ = _frames(lang, mode, COMMA_PHRASES[lang][text], ",", speed, inflection, pitch=pitch)
    now = _comma_record(frames)
    where = f"{mode} {lang} '{text},' at {pitch:g} Hz, inflection {inflection:g}, rate {speed}"
    assert len(now) == len(recorded), where
    for i, (a, b) in enumerate(zip(now, recorded)):
        assert a == pytest.approx(b, abs=2e-4), f"{where}, frame {i}: {a}, was {b}"


@pytest.mark.parametrize("speed", [0.5, 1.0])  # (the F0 probe needs frames of 45 ms)
@pytest.mark.parametrize("lang", LANGS)
def test_a_comma_plays_the_same_after_a_short_statement(lang, speed):
    """The comma's frames leave the filter lengths to the DSP (0 = its
    default).  The DSP kept the last length a frame had given it, so a comma
    after a short statement, which sends its own lengths, would have played
    with those.  tests/cpp/test_fujisaki_default_lengths.cpp holds the DSP
    to this sample by sample."""
    first, _ = _frames(lang, "fujisaki_style", UTTERANCES["word, first syllable stressed"], ".", 3.0)
    sent = [f.ex for f in first if f.ex and (f.ex["fujisakiPhraseLen"] or f.ex["fujisakiAccentLen"])]
    assert sent, "the statement before it sends no length of its own"
    frames, _ = _frames(lang, "fujisaki_style", UTTERANCES["sentence"], ",", speed)
    assert all(not f.ex["fujisakiPhraseLen"] and not f.ex["fujisakiAccentLen"] for f in frames if f.ex)
    _assert_the_dsp_plays(frames, 22050, f"{lang} comma @{speed} after a one-word statement", spoken_before=first)


# ---------------------------------------------------------------------------
# Fujisaki: an accent command spans its syllable and keeps its height
# ---------------------------------------------------------------------------

def _accent_commands(lang, ipa, speed, clause="."):
    """Every accent command of a clause, as a dict: the sound that carries
    it, its length and its filter's rise time in ms, the ms from that sound
    to the start of the next syllable, and the share of its amplitude that
    the DSP's filter reaches (pitchModel.h designAccent)."""
    frames, trace = _frames(lang, "fujisaki_style", ipa, clause, speed)
    starts = [e["frameIndex"] for e in trace] + [len(frames)]
    keys = [e["phonemeKey"] for e in trace]
    out = []
    for k, (a, b) in enumerate(zip(starts, starts[1:])):
        ex = frames[a].ex
        if not ex or ex["fujisakiAccentAmp"] <= 0.0:
            continue
        # These clauses are consonant-vowel syllables: the next syllable
        # starts with the first consonant after this one's vowel.  (en-us
        # makes two sounds of an l.)
        vowel = next(j for j in range(k, len(keys)) if keys[j][0] not in "mnl")
        syllable_end = starts[next((j for j in range(vowel + 1, len(keys)) if keys[j][0] in "mnl"), len(keys))]
        dur = ex["fujisakiAccentDur"] or 7500.0
        length = ex["fujisakiAccentLen"] or 1024.0
        r = math.exp(-1.0 / length)

        def step(m):
            return 0.0 if m < 0.0 else 1.0 - r ** (m + 1.0) * (1.0 + (m + 1.0) * (1.0 - r))
        reached = max(step(n) - step(n - dur) for n in range(0, int(dur + 8 * length), 4))
        out.append({"carrier": keys[k], "command_ms": dur * 1000.0 / REF_SR, "rise_ms": length * 1000.0 / REF_SR,
                    "syllable_ms": sum(f.duration_ms for f in frames[a:syllable_end]),
                    "height": ex["fujisakiAccentAmp"] * reached})
    return out


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang", LANGS)
def test_fujisaki_accent_command_spans_its_syllable(lang, speed):
    """In Fujisaki's model the accent command lies over the accented mora or
    syllable and leads it by a few tens of ms (Fujisaki & Hirose 1984,
    J. Acoust. Soc. Jpn. (E) 5(4), 233-242).  It was 7500 samples (340 ms)
    at every rate, starting with the vowel; the first fix for #155 cut it to
    the vowel alone, which with the filter's 46 ms rise time halved the
    accents at rate 1.0 and left a third at 2.0."""
    cmds = _accent_commands(lang, UTTERANCES["sentence"], speed)
    assert cmds, f"{lang} @{speed}: no accent command"
    for c in cmds:
        where = f"{lang} @{speed}: {c}"
        assert c["carrier"][0] in "mnl", where                       # starts with the syllable's consonant
        assert c["command_ms"] == pytest.approx(c["syllable_ms"], rel=0.03), where
        # The filter's rise time is the DSP's 46 ms, divided by the rate above 1.
        assert c["rise_ms"] == pytest.approx(1024.0 * 1000.0 / REF_SR / max(1.0, speed), rel=0.01), where


@pytest.mark.parametrize("lang", LANGS)
def test_fujisaki_accents_are_as_high_at_every_rate(lang):
    """The command shrinks with the syllable; the height it reaches does not."""
    by_speed = {s: _accent_commands(lang, UTTERANCES["sentence"], s) for s in SPEEDS}
    longest = [max(c["command_ms"] for c in by_speed[s]) for s in SPEEDS]
    assert longest == sorted(longest, reverse=True) and longest[-1] < 0.5 * longest[0], (
        f"{lang}: the longest accent command at {SPEEDS} is {[round(x) for x in longest]} ms")
    slow = [c["height"] for c in by_speed[SPEEDS[0]]]
    assert min(slow) > 0.02, slow
    for s in SPEEDS:
        assert [c["height"] for c in by_speed[s]] == pytest.approx(slow, rel=0.01), (
            f"{lang} @{s}: accent heights {[round(c['height'], 4) for c in by_speed[s]]}, at {SPEEDS[0]}: {slow}")


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("clause", [".", "!", "?"])
@pytest.mark.parametrize("lang", LANGS)
def test_a_fujisaki_clause_has_one_accent_rise_time(lang, clause, speed):
    """The DSP has one accent filter, and every command designs it anew
    (pitchModel.h).  A longer rise time given while the accent before is
    still dying away carries that fall on at the slower pace: with a rise
    time per command, tried while this was written, the rise of "Hola?" at
    rate 3.0 was undone by the accent before it and the F0 ended under the
    line.  (A guard: it held before this work too, when every command had
    the DSP's own rise time.)"""
    for text, ipa in ACCENT_PHRASES[lang].items():
        frames, _ = _frames(lang, "fujisaki_style", ipa, clause, speed)
        lengths = {round(f.ex["fujisakiAccentLen"], 3) for f in frames if f.ex and f.ex["fujisakiAccentAmp"] > 0.0}
        assert len(lengths) == 1, f"{lang} '{text}{clause}' @{speed}: rise times {sorted(lengths)}"
        c = Contour(frames)
        lowest = min(c.accent_log(c.start_ms + (c.end_ms - c.start_ms) * i / 400.0) for i in range(401))
        assert lowest > -1e-6, f"{lang} '{text}{clause}' @{speed}: the accents take the pitch {lowest:.3f} under the line"


# Real sentences and short words (IPA from eSpeak NG 1.53).  The reporters of
# #155 listen in Spanish and Portuguese and found these modes monotonous.
ACCENT_PHRASES = {
    "en-us": {"The quick brown fox jumps over the lazy dog":
              "ðə kwˈɪk bɹˈa͡ʊn fˈɑːks d͡ʒˈʌmps ˌo͡ʊvɚ ðə lˈe͡ɪzi dˈɑːɡ",
              "Really": "ɹˈi͡əli"},
    "hu": {"Hol van a pályaudvar": "hˈol vˈɑn ˌɑ pˈaːjɑudvɑr", "Igen": "ˈiɡɛn"},
    "es": {"Me gusta mucho esta canción": "me ɣˈusta mˈut͡ʃo ˈesta kanθjˈon", "Hola": "ˈola"},
    "pt-br": {"Eu gosto muito desta música": "ˈe͡ʊ ɡˈɔstʊ mˈũ͡ɪ̃tʊ dˈɛstɐ mˈuzikɐ", "Obrigado": "ˌobɾiɡˈadʊ"},
}
# The first letters of the vowel phonemes in those phrases (the packs' own
# keys too: en-us writes the last vowel of "really" as a raised i, and
# Hungarian its short a as a small capital).
_VOWEL_LETTERS = set("aeiouɑɒæɐəɛɜɚɪɔʊʌøœũãõẽĩⁱᵒᵊᵘᴒᴐáäÃ")
# The highest point of the accent component before the last vowel, as a
# percentage above the line it rides on, at rates 0.5, 1.0, 1.6, 2.0 and 3.0,
# measured on the build without the #155 work (110 Hz, inflection 0.5).
ACCENT_HEIGHT_BEFORE = {
    ("en-us", "The quick brown fox jumps over the lazy dog", "."): [12.7, 12.7, 12.7, 12.7, 12.7],
    ("en-us", "The quick brown fox jumps over the lazy dog", "!"): [24.1, 24.1, 24.1, 24.1, 24.0],
    ("en-us", "The quick brown fox jumps over the lazy dog", "?"): [16.9, 16.9, 16.9, 16.9, 16.8],
    ("en-us", "Really", "."): [12.7, 11.5, 8.9, 7.3, 4.8],
    ("en-us", "Really", "!"): [24.0, 21.6, 16.6, 13.6, 8.7],
    ("en-us", "Really", "?"): [16.8, 15.1, 11.4, 9.4, 6.2],
    ("hu", "Hol van a pályaudvar", "."): [6.1, 6.1, 6.1, 6.1, 6.1],
    ("hu", "Hol van a pályaudvar", "!"): [11.3, 11.3, 11.3, 11.3, 11.3],
    ("hu", "Hol van a pályaudvar", "?"): [8.1, 8.1, 8.1, 8.1, 8.0],
    ("hu", "Igen", "."): [5.6, 3.7, 2.3, 1.7, 1.1],
    ("hu", "Igen", "!"): [10.3, 6.8, 4.1, 3.0, 2.0],
    ("hu", "Igen", "?"): [7.3, 4.9, 2.9, 2.2, 1.4],
    ("es", "Me gusta mucho esta canción", "."): [12.7, 12.7, 12.7, 12.7, 12.6],
    ("es", "Me gusta mucho esta canción", "!"): [24.0, 24.1, 24.1, 24.1, 23.9],
    ("es", "Me gusta mucho esta canción", "?"): [16.8, 16.9, 16.9, 16.9, 16.7],
    ("es", "Hola", "."): [11.9, 8.2, 4.9, 3.8, 2.0],
    ("es", "Hola", "!"): [22.5, 15.2, 9.1, 6.9, 3.7],
    ("es", "Hola", "?"): [15.8, 10.8, 6.5, 4.9, 2.6],
    ("pt-br", "Eu gosto muito desta música", "."): [12.7, 12.7, 12.7, 12.7, 12.7],
    ("pt-br", "Eu gosto muito desta música", "!"): [24.0, 24.1, 24.1, 24.1, 24.1],
    ("pt-br", "Eu gosto muito desta música", "?"): [16.8, 16.9, 16.9, 16.9, 16.9],
    ("pt-br", "Obrigado", "."): [12.4, 11.2, 9.4, 8.4, 6.3],
    ("pt-br", "Obrigado", "!"): [23.4, 21.1, 17.6, 15.6, 11.6],
    ("pt-br", "Obrigado", "?"): [16.4, 14.8, 12.4, 11.0, 8.3],
}


def _accent_height(lang, text, clause, speed):
    frames, trace = _frames(lang, "fujisaki_style", ACCENT_PHRASES[lang][text], clause, speed)
    c = Contour(frames)
    vowels = [e["frameIndex"] for e in trace if e["phonemeKey"][0] in _VOWEL_LETTERS]
    assert len(vowels) >= 2, [e["phonemeKey"] for e in trace]
    last_vowel = c.frame_start[vowels[-1]]
    best, t = 0.0, 0.0
    while t < last_vowel:
        best = max(best, c.accent_log(t))
        t += 2.0
    return (math.exp(best) - 1.0) * 100.0


@pytest.mark.parametrize("clause", [".", "!", "?"])
@pytest.mark.parametrize("lang,text", [(lang, text) for lang in ACCENT_PHRASES for text in ACCENT_PHRASES[lang]])
def test_fujisaki_accents_are_no_lower_than_they_were(lang, text, clause):
    before = ACCENT_HEIGHT_BEFORE[(lang, text, clause)]
    now = [_accent_height(lang, text, clause, s) for s in SPEEDS]
    where = (f"{lang} '{text}{clause}' at {SPEEDS}: {[round(x, 1) for x in now]}% above the line,"
             f" was {before}%")
    for a, b in zip(now, before):
        assert a >= b - 0.05, where
    # And the rate does not take them away.  (In a row of stressed syllables
    # what is left of one accent adds to the next, more so at fast rates.)
    assert now[-1] >= 0.95 * now[0], where


# ---------------------------------------------------------------------------
# Other pitches, other inflections, and no jump inside a sound
# ---------------------------------------------------------------------------

# IPA from eSpeak NG 1.53: word-initial vowels and sonorants at the end of
# the clause, voiced codas, one-word clauses and long sentences.
RANGE_PHRASES = {
    "en-us": {"It is": "ɪɾ ˈɪz", "I love it": "a͡ɪ lˈʌv ɪt", "Bob": "bˈɑːb", "Really": "ɹˈi͡əli",
              "The quick brown fox jumps over the lazy dog":
              "ðə kwˈɪk bɹˈa͡ʊn fˈɑːks d͡ʒˈʌmps ˌo͡ʊvɚ ðə lˈe͡ɪzi dˈɑːɡ"},
    "hu": {"Igen": "ˈiɡɛn", "Hol van a pályaudvar": "hˈol vˈɑn ˌɑ pˈaːjɑudvɑr",
           "Elmentél a boltba": "ˈɛlmɛnteːl ˌɑ bˈoltbɑ"},
    "es": {"Hola": "ˈola", "Dónde está el baño": "dˈonde estˈa el βˈaɲo",
           "Me gusta mucho esta canción": "me ɣˈusta mˈut͡ʃo ˈesta kanθjˈon"},
    "pt-br": {"Não": "nˈɐ̃͡ʊ̃", "Obrigado": "ˌobɾiɡˈadʊ",
              "Eu gosto muito desta música": "ˈe͡ʊ ɡˈɔstʊ mˈũ͡ɪ̃tʊ dˈɛstɐ mˈuzikɐ"},
}
PITCHES = [60.0, 110.0, 220.0]
INFLECTIONS = [0.25, 0.5, 1.0]
# The pitch passes keep their own targets between 0.6 and 2.0 times the pitch
# they are given (passes/pitch_common.h).  The microprosody pass runs after
# them in every mode and moves single sounds by a few Hz (up to 15 by
# default), which at 60 Hz is a quarter of the pitch; so what a frame may
# carry is held a little wider here.
FRAME_RANGE = (0.5, 2.5)
END_RANGE = (0.55, 2.2)


@functools.lru_cache(maxsize=None)
def _range_case(mode, lang, text, clause, speed, pitch, inflection):
    """The frame pitches of a clause, its lowest and highest F0, and where it ends."""
    frames, _ = _frames(lang, mode, RANGE_PHRASES[lang][text], clause, speed, inflection, pitch=pitch)
    c = Contour(frames)
    voiced = [(t0, t1) for (t0, t1, _, _), f in zip(c.segments, [f for f in frames if not f.is_silence])
              if f.params["voiceAmplitude"] > 0.05]
    f0 = [c.at(t0 + (t1 - t0 - 1e-3) * i / 8.0) for t0, t1 in voiced for i in range(9)]
    pitches = [f.params[k] for f in frames if not f.is_silence for k in ("voicePitch", "endVoicePitch")]
    return {"frames": (min(pitches), max(pitches)), "f0": (min(f0), max(f0)), "end": c.at(voiced[-1][1] - 1e-3),
            "finite": all(math.isfinite(p) for p in pitches)}


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("inflection", INFLECTIONS)
@pytest.mark.parametrize("pitch", PITCHES)
@pytest.mark.parametrize("lang", LANGS)
@pytest.mark.parametrize("mode", MODES)
def test_the_pitch_stays_a_voice_at_other_pitches_and_inflections(mode, lang, pitch, inflection, speed):
    """Every test of the first fix ran at 110 Hz and inflection 0.5.  The
    Klatt mode's movements are in Hz: at 60 Hz and full inflection the first
    fix ended its statements at 0.36 of the pitch (0.07 at worst; 0.40 at
    worst before it) and its questions at 2.5 times it.  A Fujisaki
    exclamation peaked at 3.1 times the pitch before any of this."""
    for text in RANGE_PHRASES[lang]:
        for clause in ".!?,":
            m = _range_case(mode, lang, text, clause, speed, pitch, inflection)
            where = (f"{mode} {lang} '{text}{clause}' at {pitch:g} Hz, inflection {inflection:g}, rate {speed}: frames"
                     f" {m['frames'][0]:.1f} to {m['frames'][1]:.1f} Hz, F0 {m['f0'][0]:.1f} to {m['f0'][1]:.1f} Hz,"
                     f" ends at {m['end']:.1f} Hz")
            assert m["finite"], where
            assert FRAME_RANGE[0] * pitch <= m["frames"][0] and m["frames"][1] <= FRAME_RANGE[1] * pitch, where
            # What is heard: in the Fujisaki mode the frames' pitch times the DSP's commands.
            assert FRAME_RANGE[0] * pitch <= m["f0"][0] and m["f0"][1] <= FRAME_RANGE[1] * pitch, where
            if clause in ".!":
                assert m["end"] >= END_RANGE[0] * pitch, where
            if clause == "?":
                assert m["end"] <= END_RANGE[1] * pitch, where


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang", LANGS)
@pytest.mark.parametrize("mode", ["fujisaki_style", "legacy"])
def test_an_inflection_scale_of_zero_is_flat(mode, lang, speed):
    """tests/test_inflection_scale_every_mode.py reads the pitch the frames
    carry.  The Fujisaki mode's melody is also in the commands the DSP adds
    to it, so those are held here: at 0 there are none.  Both modes read a
    scale of 0 as 1 before."""
    for clause in ".!?,":
        frames, _ = _frames(lang, mode, UTTERANCES["sentence"], clause, speed, scale=0.0)
        for f in frames:
            if f.is_silence:
                continue
            where = f"{mode} {lang} '{clause}' @{speed}: {f.params['voicePitch']:.2f} to {f.params['endVoicePitch']:.2f} Hz, {f.ex}"
            assert f.params["voicePitch"] == pytest.approx(BASE, abs=0.01), where
            assert f.params["endVoicePitch"] == pytest.approx(BASE, abs=0.01), where
            assert not f.ex["fujisakiPhraseAmp"] and not f.ex["fujisakiAccentAmp"], where


# en-us and en-gb cut the start of a word-initial sound off as a "word
# boundary dip" frame (wordBoundaryDipMs).  Both frames carried the sound's
# whole pitch ramp, so the pitch ran to the sound's end pitch in the dip
# frame and started again from the top in the main frame (#222).  On the
# clause's last vowel, with its fall no longer smoothed, "It is!" in the
# Klatt mode went 154 > 92 > 154 > 92 Hz.
DIP_PHRASES = {
    "en-us": {"It is": "ɪɾ ˈɪz", "I love it": "a͡ɪ lˈʌv ɪt", "Are you sure about that": "ɑː͡ɹ juː ʃˈʊɹ ɐbˌa͡ʊt ðˈæt"},
    "en-gb": {"Hello world": "həlˈə͡ʊ wˈɜːld", "Are you sure about that": "ɑː juː ʃˈʊ͡əɹ ɐbˌa͡ʊt ðˈat"},
}


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("clause", [".", "!", "?", ","])
@pytest.mark.parametrize("lang", list(DIP_PHRASES))
@pytest.mark.parametrize("mode", MODES)
def test_a_sound_does_not_start_its_pitch_ramp_twice(mode, lang, clause, speed):
    if clause == "," and mode in COMMA_MODES:
        pytest.skip("the comma of this mode is left exactly as it was")
    cut = 0
    for text, ipa in DIP_PHRASES[lang].items():
        frames, trace = _frames(lang, mode, ipa, clause, speed)
        starts = [e["frameIndex"] for e in trace] + [len(frames)]
        for e, a, b in zip(trace, starts, starts[1:]):
            for first, second in zip(frames[a:b], frames[a + 1:b]):
                if first.is_silence or second.is_silence:
                    continue
                if first.params["voiceAmplitude"] <= 0.0 or second.params["voiceAmplitude"] <= 0.0:
                    continue
                cut += 1
                jump = second.params["voicePitch"] - first.params["endVoicePitch"]
                assert abs(jump) <= 0.5, (
                    f"{mode} {lang} '{text}{clause}' @{speed}, /{e['phonemeKey']}/: a frame ends at"
                    f" {first.params['endVoicePitch']:.1f} Hz and the next starts at {second.params['voicePitch']:.1f} Hz")
    assert cut, "no sound of these phrases is cut into frames"


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("clause", [".", "!", "?"])
@pytest.mark.parametrize("lang", list(DIP_PHRASES))
@pytest.mark.parametrize("mode", MODES)
def test_the_ramp_is_cut_where_the_frame_is(mode, lang, clause, speed):
    """Continuity alone would hold for a cut anywhere.  The dip frame takes
    the share of the ramp that it takes of the sound's time, so the pitch
    moves at one pace through both frames and never past the end pitch."""
    cut = 0
    for text, ipa in DIP_PHRASES[lang].items():
        frames, trace = _frames(lang, mode, ipa, clause, speed)
        starts = [e["frameIndex"] for e in trace] + [len(frames)]
        for e, a, b in zip(trace, starts, starts[1:]):
            if b - a != 2:
                continue
            dip, main = frames[a], frames[a + 1]
            if dip.is_silence or main.is_silence:
                continue
            if dip.params["voiceAmplitude"] <= 0.0 or main.params["voiceAmplitude"] <= 0.0:
                continue
            if dip.params["voiceAmplitude"] >= main.params["voiceAmplitude"]:
                continue  # two frames, but not a word-boundary dip
            start, end = dip.params["voicePitch"], main.params["endVoicePitch"]
            share = dip.duration_ms / (dip.duration_ms + main.duration_ms)
            where = (f"{mode} {lang} '{text}{clause}' @{speed}, /{e['phonemeKey']}/: {start:.2f} to {end:.2f} Hz,"
                     f" dip {dip.duration_ms:.1f} ms of {dip.duration_ms + main.duration_ms:.1f},"
                     f" cut at {dip.params['endVoicePitch']:.2f} Hz")
            assert 0.0 < share < 1.0, where
            assert dip.params["endVoicePitch"] == pytest.approx(start + (end - start) * share, abs=0.05), where
            cut += 1
    assert cut, "no word-boundary dip in these phrases"


# ---------------------------------------------------------------------------
# The computed contour is what the DSP plays
# ---------------------------------------------------------------------------

def _f0(samples, sr, t_ms):
    """F0 near t_ms by autocorrelation over 40 ms, 70 to 260 Hz (pure Python)."""
    step = max(1, int(round(sr / 11025.0)))
    rate = sr / step
    c, half = int(t_ms * sr / 1000.0), int(0.020 * sr)
    raw = samples[max(0, c - half):c + half]
    w = [sum(raw[i:i + step]) / step for i in range(0, len(raw) - step + 1, step)]
    mean = sum(w) / len(w)
    w = [x - mean for x in w]
    lo, hi = int(rate / 260.0), int(rate / 70.0)
    ac = [sum(a * b for a, b in zip(w, w[k:])) for k in range(hi + 2)]
    k = max(range(lo, hi + 1), key=lambda i: ac[i])
    assert ac[k] > 0.3 * ac[0], f"no clear pitch at {t_ms:.0f} ms"
    denom = ac[k - 1] - 2.0 * ac[k] + ac[k + 1]
    shift = 0.5 * (ac[k - 1] - ac[k + 1]) / denom if denom else 0.0
    return rate / (k + shift)


@pytest.mark.parametrize("sample_rate", [22050, 44100])
@pytest.mark.parametrize("speed", [0.5, 1.0, 1.6])
@pytest.mark.parametrize("clause", [".", "?"])
@pytest.mark.parametrize("mode", MODES)
def test_the_contour_is_what_the_dsp_plays(mode, clause, speed, sample_rate):
    """Also holds the DSP to its unit: a command length in a frame is in
    samples at 22050 Hz, whatever rate the DSP runs at."""
    frames, _ = _frames("en-us", mode, UTTERANCES["sentence"], clause, speed)
    _assert_the_dsp_plays(frames, sample_rate, f"{mode} '{clause}' @{speed} {sample_rate} Hz")


@pytest.mark.parametrize("speed", [0.5, 1.0])
def test_a_short_clause_does_not_change_the_one_after_it(speed):
    """One DSP speaks clause after clause (NVDA keeps it for the session).
    A length sent with a short clause must not shape the long clause that
    follows it."""
    first, _ = _frames("en-us", "fujisaki_style", UTTERANCES["word, first syllable stressed"], ".", 3.0)
    frames, _ = _frames("en-us", "fujisaki_style", UTTERANCES["sentence"], ".", speed)
    _assert_the_dsp_plays(frames, 22050, f"after a one-word clause, @{speed}", spoken_before=first)


@pytest.mark.parametrize("speed", [2.0, 3.0])
@pytest.mark.parametrize("clause", [".", "!", "?"])
def test_the_fujisaki_contour_is_what_the_dsp_plays_at_fast_rates(clause, speed):
    """At these rates no frame is 45 ms long, so frames from 25 ms are
    probed.  This is where an accent command is shorter than the DSP's own
    rise time, and where the accent filter's rise time is no longer the
    DSP's default."""
    frames, _ = _frames("en-us", "fujisaki_style", UTTERANCES["sentence"], clause, speed)
    _assert_the_dsp_plays(frames, 22050, f"fujisaki_style '{clause}' @{speed}", min_frame_ms=25.0)


def _assert_the_dsp_plays(frames, sample_rate, what, spoken_before=None, min_frame_ms=45.0):
    c = Contour(frames)
    with Player(sample_rate) as pl:
        if spoken_before:
            pl.queue(spoken_before)
            pl.synthesize()
        pl.queue(frames)
        samples = pl.synthesize()
    # Late in each long frame: where a frame starts the DSP crossfades two
    # pitches and the formants move, and a 40 ms window hears neither pitch.
    probes = [t0 + 0.75 * (t1 - t0) for t0, t1, _, _ in c.segments if t1 - t0 >= min_frame_ms]
    assert len(probes) >= 4, probes
    compared = 0
    for t in probes:
        try:
            heard = _f0(samples, sample_rate, t)
        except AssertionError:
            # No one pitch in the 40 ms window: the end of a question, where
            # the pitch moves 30 Hz inside it, or a frame shorter than it.
            continue
        compared += 1
        # The crossfade into a frame delays its pitch ramp by up to 20 ms.
        computed = min((c.at(t - lag) for lag in range(0, 21, 2)), key=lambda hz: abs(hz - heard))
        assert abs(heard - computed) <= 0.03 * computed, (
            f"{what}, {t:.0f} ms: the DSP plays {heard:.1f} Hz, the frames say {c.at(t):.1f} Hz")
    # With frames of 45 ms at most one probe may be without a pitch.
    needed = max(4, len(probes) - 1) if min_frame_ms >= 45.0 else 4
    assert compared >= needed, f"{what}: {compared} of {len(probes)} probes had a pitch"


# ---------------------------------------------------------------------------
# What must not move
# ---------------------------------------------------------------------------

NEW_SETTINGS = {
    "klattExclamationAccentScale": "9.0",
    "legacyExclamationAccentScale": "9.0",
}
FIXED_LENGTHS = {"fujisakiPhraseLen": 2000.0, "fujisakiAccentLen": 600.0, "fujisakiAccentDur": 3000.0}


def _packs_with(tmp_path_factory, name, settings):
    root = tmp_path_factory.mktemp(name)
    shutil.copytree(REPO / "packs", root / "packs")
    default = root / "packs" / "lang" / "default.yaml"
    text = default.read_text(encoding="utf-8")
    assert "\nsettings:\n" in text
    extra = "".join(f"  {k}: {v}\n" for k, v in settings.items())
    default.write_text(text.replace("\nsettings:\n", "\nsettings:\n" + extra, 1), encoding="utf-8", newline="")
    return str(root)


@pytest.fixture(scope="module")
def extreme_packs(tmp_path_factory):
    """A copy of the packs with every setting this change adds turned far up."""
    return _packs_with(tmp_path_factory, "packs_extreme", NEW_SETTINGS)


@pytest.fixture(scope="module")
def fixed_length_packs(tmp_path_factory):
    """A copy of the packs that fixes the three Fujisaki lengths."""
    return _packs_with(tmp_path_factory, "packs_fixed_lengths", FIXED_LENGTHS)


@pytest.mark.parametrize("speed", SPEEDS)
def test_a_pack_can_still_fix_the_fujisaki_lengths(fixed_length_packs, speed):
    """fujisakiAccentDur: 0 now means "as long as the syllable"; a pack that
    gives a length keeps it, and its own filter length, at every rate, as
    before."""
    frames, _ = _frames("en-us", "fujisaki_style", UTTERANCES["sentence"], "?", speed,
                        packs_root=fixed_length_packs)
    commands = [f.ex for f in frames if f.ex and f.ex["fujisakiAccentAmp"] > 0.0]
    assert commands
    for ex in commands:
        assert ex["fujisakiAccentDur"] == FIXED_LENGTHS["fujisakiAccentDur"]
        assert ex["fujisakiAccentLen"] == FIXED_LENGTHS["fujisakiAccentLen"]
    # The phrase's rise time is the pack's, or a quarter of the clause when that is shorter.
    sent = frames[0].ex["fujisakiPhraseLen"]
    clause = sum(f.duration_ms for f in frames if not f.is_silence) * REF_SR / 1000.0
    assert sent == pytest.approx(min(FIXED_LENGTHS["fujisakiPhraseLen"], 0.25 * clause), rel=0.02)


@pytest.mark.parametrize("sample_rate", [22050, 44100, 16000])
def test_a_fixed_fujisaki_length_lasts_the_same_time_at_every_sample_rate(fixed_length_packs, sample_rate):
    """The lengths are samples at 22050 Hz (pack.h, Tuning.md).  The DSP used
    them as samples at its own rate: at 44100 Hz every length a pack set was
    half as long."""
    frames, _ = _frames("en-us", "fujisaki_style", UTTERANCES["sentence"], ".", 0.5,
                        packs_root=fixed_length_packs)
    _assert_the_dsp_plays(frames, sample_rate, f"fixed lengths, {sample_rate} Hz")


def _plain(frames):
    def clean(d):  # NaN marks "not set" in a frame and is not equal to itself
        return None if d is None else {k: (None if v != v else v) for k, v in d.items()}
    return [(clean(f.params), clean(f.ex), f.duration_ms, f.fade_ms) for f in frames]


@pytest.mark.parametrize("clause", [".", "!", "?", ","])
@pytest.mark.parametrize("lang", LANGS)
@pytest.mark.parametrize("mode", UNTOUCHED_MODES)
def test_the_new_settings_do_not_reach_the_default_or_arato_mode(extreme_packs, mode, lang, clause):
    for speed in SPEEDS:
        ipa = UTTERANCES["sentence"]
        usual = _plain(_frames(lang, mode, ipa, clause, speed)[0])
        extreme = _plain(_frames(lang, mode, ipa, clause, speed, packs_root=extreme_packs)[0])
        assert usual == extreme, f"{mode} {lang} '{clause}' @{speed}"


def test_the_new_settings_are_read(extreme_packs):
    """The guard above would pass on settings nobody reads."""
    for mode, clause in (("klatt_style", "!"), ("legacy", "!")):
        ipa = UTTERANCES["word, first syllable stressed"]
        usual = _plain(_frames("en-us", mode, ipa, clause, 1.0)[0])
        extreme = _plain(_frames("en-us", mode, ipa, clause, 1.0, packs_root=extreme_packs)[0])
        assert usual != extreme, mode


@pytest.mark.parametrize("lang", LANGS)
@pytest.mark.parametrize("mode", ["espeak_style", "arato_style", "klatt_style", "legacy", "impulse_style"])
def test_only_the_fujisaki_mode_sends_fujisaki_commands(mode, lang):
    for clause in ".!?,":
        frames, _ = _frames(lang, mode, UTTERANCES["sentence"], clause, 2.0)
        for f in frames:
            if f.ex:
                sent = {k: v for k, v in f.ex.items() if k.startswith("fujisaki") and v != 0.0}
                assert not sent, f"{mode} {lang} '{clause}': {sent}"
