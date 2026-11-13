"""The pause after a clause follows the speech rate (scalePauseToRate).

The pause a host leaves after a clause had one length whatever the rate:
25 ms after a comma and 35 ms after a sentence mark with short pauses, 50
and 60 ms with long ones.  At three times the rate the words were a third
as long and the pauses were not; at half the rate the words were twice as
long and the pauses still 25 and 35 ms.

People do not speak that way.  Pause duration follows speaking rate in six
languages read at five rates (Werner, Trouvain & Möbius 2022, Speech
Prosody).  Fast readers of six European languages drop their longest pauses
and their average pause is shorter (Demol, Verhelst & Verhoeve 2007,
Interspeech).  Three Lithuanian readers' sentence pauses measure 0.2 to 0.6
of their length at a fast rate and 1.1 to 1.4 at a slow one (Kazlauskienė,
Raškinis & Kalašinskaitė-Zavišienė 2025, Baltic Journal of Modern Computing
13).  And a pause must not be lost: silent pauses at salient points bring
the recall of time-compressed speech back to that of a normal rate (O'Leary
et al. 2023, Trends in Hearing 27).

So with the language pack setting scalePauseToRate (on in default.yaml) a
pause is its length times rate^-0.5, kept between 0.5 and 1.6 of it: it
shrinks more slowly than the words do, never under half, and "short" stays
shorter than "long" at any one rate.  nvspFrontend_scalePause is the one
function that does it, for every host: it takes the pause
nvspFrontend_nextClause gave, the rate the listener hears, and the time
stretch of the player the silence is queued on (a silence queued on a
stretched player is shortened by the stretch, so the function lengthens it
by as much, and the pause is not shrunk twice).  Off, it returns the pause
it was given.

What must stay as it is: nvspFrontend_nextClause's own pauses, a pause at
rate 1.0, and every frame queueIPA emits, in every language.
"""
from __future__ import annotations

import ctypes
import math
import pathlib
import re
import shutil

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
KEY = "scalePauseToRate"
SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]
# pauseMode (1 short, 2 long) -> (after a comma, after a sentence mark), in ms.
BASE = {1: (25.0, 35.0), 2: (50.0, 60.0)}
ALL_BASES = sorted(b for pair in BASE.values() for b in pair)


def _factor(rate):
    return min(max(rate ** -0.5, 0.5), 1.6)


def _scale(frontend, handle, pause_ms, rate, stretch=1.0):
    fn = frontend._dll.nvspFrontend_scalePause
    fn.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_double, ctypes.c_double]
    fn.restype = ctypes.c_double
    return fn(handle, pause_ms, rate, stretch)


def _clause_pauses(frontend, text, pause_mode):
    """The pause nvspFrontend_nextClause gives after each clause of `text`."""
    fn = frontend._dll.nvspFrontend_nextClause
    fn.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                   ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
                   ctypes.c_char_p, ctypes.POINTER(ctypes.c_double)]
    fn.restype = ctypes.c_int
    data = text.encode("utf-8")
    pause = ctypes.c_double()
    out, pos = [], 0
    while pos < len(data):
        nxt = fn(data, len(data), pos, pause_mode, None, None, None, ctypes.byref(pause))
        if nxt <= pos:
            break
        out.append(pause.value)
        pos = nxt
    return out


def _set_setting(yaml_path, value):
    """Write `scalePauseToRate: <value>` into a language file's settings."""
    text = yaml_path.read_bytes().decode("utf-8")
    line = f"  {KEY}: {value}"
    text, n = re.subn(rf"(?m)^  {KEY}:[^\r\n]*", line, text)
    if not n:
        text, n = re.subn(r"(?m)^settings:[ \t]*(\r?\n)", lambda m: m.group(0) + line + m.group(1), text, count=1)
    assert n == 1, f"{yaml_path.name}: no settings: block to write into"
    yaml_path.write_bytes(text.encode("utf-8"))


@pytest.fixture
def handle_for(frontend, pack_dir, tmp_path):
    """handle_for(lang, off=()): a handle speaking `lang` from a copy of the
    packs in which the language files named in `off` (without ".yaml") turn
    scalePauseToRate off; from the repository's packs when `off` is empty."""
    handles = []
    copies = {}

    def _make(lang, off=()):
        root = pack_dir
        if off:
            key = tuple(sorted(off))
            if key not in copies:
                copies[key] = tmp_path / f"packs-{len(copies)}"
                shutil.copytree(REPO / "packs", copies[key] / "packs")
                for name in key:
                    _set_setting(copies[key] / "packs" / "lang" / f"{name}.yaml", "false")
            root = str(copies[key])
        h = frontend.create_handle(root)
        handles.append(h)
        frontend.set_language(h, lang)
        return h

    yield _make
    for h in handles:
        frontend.destroy_handle(h)


# ---------------------------------------------------------------------------
# The curve
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("pause_mode", [1, 2], ids=["short", "long"])
def test_a_pause_is_its_length_times_rate_to_the_minus_half(frontend, handle_for, speed, pause_mode):
    h = handle_for("en-us")
    comma, sentence = BASE[pause_mode]
    # The pauses the splitter gives are the lengths at rate 1.0, as before.
    assert _clause_pauses(frontend, "One, two. Three", pause_mode) == [comma, sentence, 0.0]
    for base in (comma, sentence):
        got = _scale(frontend, h, base, speed)
        assert got == pytest.approx(base * speed ** -0.5, rel=1e-12)
    assert _scale(frontend, h, 0.0, speed) == 0.0  # no pause stays no pause


@pytest.mark.parametrize("base", ALL_BASES)
def test_at_rate_one_a_pause_is_exactly_what_it_was(frontend, handle_for, base):
    assert _scale(frontend, handle_for("en-us"), base, 1.0) == base


@pytest.mark.parametrize("speed, factor", [
    (0.1, 1.6), (0.25, 1.6), (0.39, 1.6),   # 1.6 is reached just under rate 0.4
    (4.0, 0.5), (5.4, 0.5), (8.0, 0.5),     # 0.5 at rate 4; SAPI reaches 5.4, NVDA 8
])
@pytest.mark.parametrize("base", ALL_BASES)
def test_a_pause_stays_between_half_and_1_6_of_its_length(frontend, handle_for, speed, factor, base):
    assert _scale(frontend, handle_for("en-us"), base, speed) == pytest.approx(base * factor, rel=1e-12)


@pytest.mark.parametrize("speed", [0.1, 0.25] + SPEEDS + [4.0, 8.0])
def test_short_stays_shorter_than_long_and_a_comma_than_a_sentence(frontend, handle_for, speed):
    h = handle_for("en-us")
    short_comma, short_sentence, long_comma, long_sentence = (
        _scale(frontend, h, b, speed) for b in (25.0, 35.0, 50.0, 60.0))
    assert short_comma < short_sentence < long_comma < long_sentence


@pytest.mark.parametrize("speed", SPEEDS)
def test_a_pause_shrinks_and_grows_more_slowly_than_the_words(frontend, handle_for, speed):
    # The words are 1/speed as long as at rate 1.0.
    ratio = _scale(frontend, handle_for("en-us"), 35.0, speed) / 35.0
    if speed > 1.0:
        assert 1.0 / speed < ratio < 1.0
    elif speed < 1.0:
        assert 1.0 < ratio < 1.0 / speed
    else:
        assert ratio == 1.0
    assert ratio >= 0.5, "a pause must never vanish (O'Leary et al. 2023)"


# ---------------------------------------------------------------------------
# A pause queued on a time-stretched player
# ---------------------------------------------------------------------------

def _silence_samples(stretch, queued_samples, sample_rate=22050):
    """How many samples the DSP plays for one silence frame of
    `queued_samples` at a time stretch."""
    dll_path = REPO / "build-x64-nvda" / "MinSizeRel" / "speechPlayer.dll"
    if ctypes.sizeof(ctypes.c_void_p) == 4:
        dll_path = REPO / "build-x86-nvda" / "MinSizeRel" / "speechPlayer.dll"
    if not dll_path.is_file():
        pytest.skip(f"{dll_path.name} is not built")
    dll = ctypes.CDLL(str(dll_path))
    dll.speechPlayer_initialize.argtypes = [ctypes.c_int]
    dll.speechPlayer_initialize.restype = ctypes.c_void_p
    dll.speechPlayer_queueFrame.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint,
                                            ctypes.c_uint, ctypes.c_int, ctypes.c_bool]
    dll.speechPlayer_queueFrame.restype = None
    dll.speechPlayer_synthesize.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_void_p]
    dll.speechPlayer_synthesize.restype = ctypes.c_int
    dll.speechPlayer_setTimeStretch.argtypes = [ctypes.c_void_p, ctypes.c_double]
    dll.speechPlayer_setTimeStretch.restype = None
    dll.speechPlayer_terminate.argtypes = [ctypes.c_void_p]
    dll.speechPlayer_terminate.restype = None
    player = dll.speechPlayer_initialize(sample_rate)
    try:
        dll.speechPlayer_setTimeStretch(player, stretch)
        dll.speechPlayer_queueFrame(player, None, queued_samples, 66, -1, False)
        buf = (ctypes.c_short * 4096)()
        total = 0
        while True:
            got = dll.speechPlayer_synthesize(player, 4096, buf)
            if got <= 0:
                return total
            total += got
    finally:
        dll.speechPlayer_terminate(player)


@pytest.mark.parametrize("stretch", [1.25, 1.5, 2.0, 4.0])
def test_the_dsp_shortens_a_queued_silence_by_the_time_stretch(stretch):
    """Why a host passes its stretch: above 2x NVDA, Android, iOS and Linux
    synthesize at 2x and let the DSP consume frames faster, silence frames
    too.  (The few ms after the frame are the DSP's own tail, at any
    stretch.)"""
    queued = 2205  # 100 ms
    tail = _silence_samples(1.0, queued) - queued
    assert 0 <= tail < 220
    assert _silence_samples(stretch, queued) - tail == pytest.approx(queued / stretch, abs=3)


@pytest.mark.parametrize("rate", [3.0, 4.0, 8.0])
@pytest.mark.parametrize("base", ALL_BASES)
def test_a_pause_on_a_stretched_player_is_not_shrunk_twice(frontend, handle_for, rate, base):
    # Synthesis is capped at 2x; the DSP stretch takes the rest.
    stretch = rate / 2.0
    queued = _scale(frontend, handle_for("en-us"), base, rate, stretch)
    heard = queued / stretch
    assert heard == pytest.approx(base * _factor(rate), rel=1e-12)
    # A pause written as samples after the DSP (SAPI) passes no stretch.
    assert _scale(frontend, handle_for("en-us"), base, rate, 1.0) == pytest.approx(heard, rel=1e-12)


def test_at_three_times_the_rate_a_short_comma_pause_is_heard_for_14_ms(frontend, handle_for):
    # Before: 25 ms queued, 16.7 ms heard through the 1.5x stretch, next to
    # words a third as long.  Now 25 * 3^-0.5 = 14.4 ms heard, 21.7 queued.
    queued = _scale(frontend, handle_for("en-us"), 25.0, 3.0, 1.5)
    assert round(queued, 2) == 21.65
    assert round(queued / 1.5, 2) == 14.43


# ---------------------------------------------------------------------------
# Off, per language, and what is left alone
# ---------------------------------------------------------------------------

@pytest.mark.parametrize("speed", [0.25] + SPEEDS + [4.0, 8.0])
def test_off_gives_the_pause_as_it_was(frontend, handle_for, speed):
    h = handle_for("en-us", off=("default",))
    for base in ALL_BASES:
        assert _scale(frontend, h, base, speed) == base
        # Not lengthened for a stretched player either: off is what a host
        # queued before, 25 ms whatever the stretch.
        assert _scale(frontend, h, base, speed, 1.5) == base


@pytest.mark.parametrize("speed", SPEEDS)
def test_a_language_that_turns_it_off_leaves_the_others_on(frontend, handle_for, speed):
    off = ("es",)
    for lang in ("es", "es-mx"):  # es-mx inherits es
        assert _scale(frontend, handle_for(lang, off=off), 35.0, speed) == 35.0
    for lang in ("en-us", "hu", "pt-br"):
        assert _scale(frontend, handle_for(lang, off=off), 35.0, speed) == pytest.approx(35.0 * speed ** -0.5)


def _languages():
    return sorted(p.stem for p in (REPO / "packs" / "lang").glob("*.yaml") if p.stem != "default")


# Languages whose pack turns the setting off.  None today; one that does
# is named here, so that no language loses it by accident.
OFF_LANGUAGES: set = set()


def test_it_is_on_for_every_language_from_default_yaml(frontend, handle_for):
    default = (REPO / "packs" / "lang" / "default.yaml").read_text(encoding="utf-8")
    assert re.search(rf"(?m)^  {KEY}: true\s*$", default)
    langs = _languages()
    assert len(langs) >= 25
    for lang in langs:
        want = 25.0 if lang in OFF_LANGUAGES else pytest.approx(25.0 * 2.0 ** -0.5)
        assert _scale(frontend, handle_for(lang), 25.0, 2.0) == want, lang


@pytest.mark.parametrize("rate", [0.0, -1.0, float("nan"), float("inf")])
def test_a_rate_that_is_no_rate_leaves_the_pause_alone(frontend, handle_for, rate):
    assert _scale(frontend, handle_for("en-us"), 35.0, rate) == 35.0


@pytest.mark.parametrize("stretch", [0.0, -2.0, 0.5, float("nan")])
def test_a_stretch_under_one_is_no_stretch(frontend, handle_for, stretch):
    # The DSP takes no stretch under 1.0 (speechPlayer_setTimeStretch).
    assert _scale(frontend, handle_for("en-us"), 35.0, 2.0, stretch) == pytest.approx(35.0 * 2.0 ** -0.5)


def test_without_a_handle_the_pause_comes_back_as_given(frontend):
    assert _scale(frontend, None, 35.0, 3.0, 1.5) == 35.0


@pytest.mark.parametrize("pause_mode, want", [(0, [0.0, 0.0, 0.0]), (1, [25.0, 35.0, 35.0]), (2, [50.0, 60.0, 60.0])],
                         ids=["off", "short", "long"])
def test_the_splitter_still_gives_the_pauses_of_rate_one(frontend, pause_mode, want):
    # nvspFrontend_nextClause takes no rate; an older host that calls it
    # alone gets what it always got.
    assert _clause_pauses(frontend, "Hello, world. How are you?", pause_mode) == want


def _frames(frontend, handle, ipa, speed):
    return [(r.is_silence, r.duration_ms, r.fade_ms, r.user_index,
             None if r.frame_dict is None else tuple(repr(v) for v in r.frame_dict.values()))
            for r in frontend.capture_frames(handle, ipa, speed=speed, clause_type=",")]


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang, ipa", [
    ("en-us", "həlˈoʊ wˈɜːld"),
    ("en-gb", "həlˈəʊ wˈɜːld"),
    ("hu", "jˈoː nˈɒpot"),
    ("es", "bwˈenos dˈias"),
])
def test_the_frames_of_a_clause_do_not_depend_on_the_setting(frontend, handle_for, lang, ipa, speed):
    # The setting is about the silence a host adds after a clause; nothing
    # the frontend queues for the clause itself changes.
    on = _frames(frontend, handle_for(lang), ipa, speed)
    off = _frames(frontend, handle_for(lang, off=("default",)), ipa, speed)
    assert len(on) > 4
    assert on == off


# ---------------------------------------------------------------------------
# Every host scales its pauses with the one function
# ---------------------------------------------------------------------------

def _strip_comments(src):
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    return re.sub(r"//[^\n]*", " ", src)


def _source(rel):
    return _strip_comments((REPO / rel).read_text(encoding="utf-8", errors="replace"))


def _calls(src, name):
    """The argument lists of every call of `name` in C or C++ source, each
    split at its top-level commas."""
    out = []
    for m in re.finditer(rf"(?<!\w){re.escape(name)}\s*\(", src):
        depth, i, args, cur = 1, m.end(), [], ""
        while depth and i < len(src):
            c = src[i]
            if c in "([{":
                depth += 1
            elif c in ")]}":
                depth -= 1
                if not depth:
                    break
            if c == "," and depth == 1:
                args.append(" ".join(cur.split()))
                cur = ""
            else:
                cur += c
            i += 1
        args.append(" ".join(cur.split()))
        out.append(args)
    return out


# Hosts that queue the pause on their player as a silence frame, after
# capping synthesis at 2x and giving the rest to the DSP's time stretch.
STRETCHED_HOSTS = [
    "src/platforms/android/jni/tgsb_jni.cpp",
    "src/platforms/ios/bridge/tgsb_bridge.cpp",
    "src/platforms/speechd/sd_tgsb.cpp",
]


@pytest.mark.parametrize("path", STRETCHED_HOSTS)
def test_a_host_that_stretches_scales_the_pause_by_the_rate_heard(path):
    calls = _calls(_source(path), "nvspFrontend_scalePause")
    assert len(calls) == 1, f"{path}: expected one nvspFrontend_scalePause call, found {len(calls)}"
    handle, pause, rate, stretch = calls[0]
    assert pause == "pauseMs"
    # The rate the listener hears: the capped speed times the stretch.
    assert re.fullmatch(r"speed \* \w*[tT]imeStretch", rate), f"{path}: rate is {rate!r}"
    assert rate.endswith(stretch) and stretch.endswith("imeStretch"), f"{path}: stretch is {stretch!r}"


def test_android_hands_both_requests_stretch_to_the_clause_loop():
    src = _source("src/platforms/android/jni/tgsb_jni.cpp")
    calls = [c for c in _calls(src, "synthesizeClauses") if "const char *text" not in c[1]]
    assert len(calls) == 2
    for args in calls:
        assert "timeStretch" in args, f"synthesizeClauses({', '.join(args)}) passes no time stretch"


def test_sapi_scales_the_pause_it_writes_after_the_dsp():
    engine = _source("src/sapi/ISpTTSEngineImpl.cpp")
    calls = _calls(engine, "scale_pause")
    assert len(calls) == 1
    pause, rate = calls[0]
    assert pause == "clause.pause_ms"
    # Rate boost is a 1.35x time stretch on top of the frontend's speed.
    assert rate == "params.speed * time_stretch"
    assert "clause.pause_ms * rt_->sample_rate()" not in engine, "the unscaled pause is still written"
    runtime = _calls(_source("src/sapi/tgsb_runtime.cpp"), "nvspFrontend_scalePause")
    assert len(runtime) == 1
    # Zero samples written straight to the site never pass the stretch.
    assert runtime[0][1:] == ["pause_ms", "rate", "1.0"]


def test_the_phoneme_editor_scales_its_pause():
    src = _source("tools/tgsbPhonemeEditorWin32/tgsb_runtime.cpp")
    assert re.search(r'GetProcAddress\(\s*m_frontend\s*,\s*"nvspFrontend_scalePause"\s*\)', src)
    calls = _calls(src, "m_feScalePause")
    assert len(calls) == 1
    # The editor synthesizes at its whole speed: no stretch.
    assert calls[0][1:] == ["pMs", "speed", "1.0"]


def test_nvda_scales_the_pause_by_the_rate_heard():
    addon = REPO / "nvdaAddon" / "synthDrivers" / "tgSpeechBox"
    pipeline = (addon / "speech_pipeline.py").read_text(encoding="utf-8")
    calls = re.findall(r"self\._frontend\.scalePause\((.*)\)[ \t]*\r?$", pipeline, flags=re.M)
    assert len(calls) == 1
    # _effectiveSpeed is the rate before the 2x synthesis cap, rate boost
    # included; _timeStretch is what the player was given.
    assert [a.strip() for a in calls[0].split(",")] == ["float(punctPauseMs)", "_effectiveSpeed", "_timeStretch"]
    assert "nvspFrontend_scalePause" in (addon / "_frontend.py").read_text(encoding="utf-8")


def test_tgsbrender_leaves_no_pause_to_scale():
    # It renders what the frontend queues, clause after clause.
    calls = _calls(_source("tools/tgsbRender.cpp"), "nvspFrontend_nextClause")
    assert calls
    for args in calls:
        assert args[3] == "0" and args[-1] == "nullptr"


def test_no_other_host_takes_a_pause_from_the_splitter_unscaled():
    """A new host (or a new clause loop) that asks nvspFrontend_nextClause
    for a pause must scale it, and be named here."""
    takes_pause = set()
    for root in ("src", "tools"):
        for p in (REPO / root).rglob("*"):
            if p.suffix not in (".cpp", ".c", ".h", ".hpp", ".mm", ".m") or not p.is_file():
                continue
            rel = p.relative_to(REPO).as_posix()
            if rel.startswith("src/frontend/") or ".cxx" in p.parts or any(
                    part.startswith("build") for part in p.parts):
                continue
            calls = _calls(_source(rel), "nvspFrontend_nextClause")
            if any(args[-1] not in ("NULL", "nullptr") for args in calls):
                takes_pause.add(rel)
    assert takes_pause == set(STRETCHED_HOSTS) | {"src/sapi/ISpTTSEngineImpl.cpp"}
    for rel in takes_pause - {"src/sapi/ISpTTSEngineImpl.cpp"}:
        assert _calls(_source(rel), "nvspFrontend_scalePause"), f"{rel} does not scale its pause"


def test_the_export_is_documented():
    header = (REPO / "src" / "frontend" / "nvspFrontend.h").read_text(encoding="utf-8")
    assert "NVSP_FRONTEND_API double nvspFrontend_scalePause(" in header
    assert "nvspFrontend_scalePause" in (REPO / "Developers.md").read_text(encoding="utf-8")
    assert f"`{KEY}`" in (REPO / "Tuning.md").read_text(encoding="utf-8")
