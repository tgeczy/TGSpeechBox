"""A voice profile scales where a phoneme is heading, not only where it starts (#173).

A profile's classScales multiply a phoneme's formants (cf_mul, pf_mul) and
bandwidths (cb_mul, pb_mul).  They were applied to the start values only.  The
targets the passes had already worked out, in the default voice's formants,
were left alone: a diphthong's offglide, the canonical vowel a coarticulated
vowel settles on, its steady state.  So with Beth (vowel F1-F3 times 1.10,
1.15, 1.10) "bye" started on Beth's formants and ended on 360/1900/2570, the
default voice's offglide, and the vowel of "deep" left Beth's 330/2270/3227 for
the default 310/2020/2960 within the frame.

Every target now gets the multipliers its start value got.  A voice without a
profile keeps the frames it had.  Bobby's F1-F3 frequency multipliers are 1, so
his formant targets stay; his B1 and B2 are 5% wider, and a diphthong of his
now ends that much wider too.
"""
from __future__ import annotations

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

import tgspeechbox  # noqa: E402

SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]
F123 = ("cf1", "cf2", "cf3", "pf1", "pf2", "pf3")
B123 = ("cb1", "cb2", "cb3", "pb1", "pb2", "pb3")
ENDS = ("endCf1", "endCf2", "endCf3", "endPf1", "endPf2", "endPf3")
START_OF = dict(zip(ENDS, F123))


def render(lang, ipa, speed, voice="", root=None):
    """(frames, [(phoneme key, first frame, one past its last frame)])."""
    with tgspeechbox.Frontend(lang, root) as fe:
        if voice:
            fe.set_voice_profile(voice)
        frames = fe.frames(ipa, speed, 110.0, 0.5, ".")
        trace = fe.frame_trace()
    spans = []
    for i, e in enumerate(trace):
        end = trace[i + 1]["frameIndex"] if i + 1 < len(trace) else len(frames)
        spans.append((e["phonemeKey"], e["frameIndex"], end))
    return frames, spans


def span_of(spans, key):
    """The first phoneme with this key; a pack's own variant ("o_es") counts."""
    for k, a, b in spans:
        if k == key or k.startswith(key + "_"):
            return a, b
    raise AssertionError(f"no phoneme {key!r} in {[k for k, _, _ in spans]}")


def frames_of(frames, spans, key):
    """The phoneme's own frames (a span can end in a silence frame)."""
    a, b = span_of(spans, key)
    return [f for f in frames[a:b] if f.params is not None]


def voiced(frames):
    return [f for f in frames if f.params is not None]


def finite(x):
    return x == x


def total_ms(frames):
    return sum(f.duration_ms for f in frames)


# Diphthongs the collapse pass turns into one gliding vowel: (language, IPA,
# the trace key of the glide).
DIPHTHONGS = [
    ("en-us", "bˈaɪ", "ɑ"),
    ("en-us", "nˈaʊ", "ä"),
    ("en-us", "ɡˈoʊ", "ᵒ"),
    ("en-gb", "bˈaɪ", "a"),
]


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang,ipa,key", DIPHTHONGS)
def test_beth_diphthong_ends_on_beths_offglide(lang, ipa, key, speed):
    """The issue's case: the last frame of the glide carries the multipliers
    the first one does (it ended on the default voice's formants)."""
    plain, pspans = render(lang, ipa, speed)
    beth, bspans = render(lang, ipa, speed, "Beth")
    pg, bg = frames_of(plain, pspans, key), frames_of(beth, bspans, key)
    assert len(pg) > 2 and len(bg) > 2, "not a glide: the collapse pass did not run"
    for k in F123 + B123:
        first = bg[0].params[k] / pg[0].params[k]
        last = bg[-1].params[k] / pg[-1].params[k]
        assert last == pytest.approx(first, rel=1e-6), (
            f"{k}: starts at {first:.3f} times the default voice and ends at {last:.3f} times"
        )
    # Beth's vowel F2 multiplier is 1.15: the test means something.
    assert bg[-1].params["cf2"] / pg[-1].params["cf2"] > 1.1
    assert total_ms(beth) == pytest.approx(total_ms(plain), abs=1e-6)


# A vowel after a consonant: its end target is the canonical vowel.
CV_WORDS = [
    ("en-us", "dˈiːp", "i"),
    ("en-gb", "dˈiːp", "i"),
    ("hu", "ɡˈɛ", "ɛ"),
    ("es", "dˈos", "o"),      # coarticulationSteadyState: onset, steady state, exit
    ("en-gb", "ɡˈəʊ", "əw"),  # the phoneme's own endCf/endPf in phonemes.yaml
]


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang,ipa,key", CV_WORDS)
def test_beth_vowel_stays_on_beths_formants(lang, ipa, key, speed):
    plain, pspans = render(lang, ipa, speed)
    beth, bspans = render(lang, ipa, speed, "Beth")
    assert bspans == pspans, "frames do not line up"
    a, b = span_of(pspans, key)
    mul = {k: beth[a].params[k] / plain[a].params[k] for k in F123}
    assert mul["cf2"] > 1.1
    targets = 0
    for i in range(a, b):
        if plain[i].params is None:
            continue
        for k in F123:  # the steady-state and exit segments start on targets
            assert beth[i].params[k] / plain[i].params[k] == pytest.approx(mul[k], rel=1e-6), (i, k)
        for e in ENDS:
            pe, be = plain[i].ex[e], beth[i].ex[e]
            assert finite(pe) == finite(be), (i, e)
            if finite(pe):
                targets += 1
                assert be / pe == pytest.approx(mul[START_OF[e]], rel=1e-6), (
                    f"frame {i} {e}: {be:.1f}, the default voice has {pe:.1f}"
                )
    assert targets, "the vowel has no end target: nothing was tested"
    assert total_ms(beth) == pytest.approx(total_ms(plain), abs=1e-6)


@pytest.mark.parametrize("speed", SPEEDS)
def test_beth_stop_burst_leans_toward_beths_vowel(speed):
    """A stop's burst is blended toward the next vowel's F2/F3 read from the
    phoneme table; that vowel is Beth's now."""
    plain, pspans = render("en-us", "dˈiːp", speed)
    beth, bspans = render("en-us", "dˈiːp", speed, "Beth")
    assert bspans == pspans
    a, b = span_of(pspans, "d")
    va, _ = span_of(pspans, "i")
    for k, vowel_mul in (("cf2", 1.15), ("cf3", 1.10)):
        rise = [beth[i].params[k] - plain[i].params[k] for i in range(a, b) if plain[i].params is not None]
        assert max(rise) > 20.0, f"{k}: the burst still aims at the default voice's vowel"
        # Never past the vowel itself.
        canonical = plain[va].ex["endC" + k[1:]]
        assert max(rise) < (vowel_mul - 1.0) * canonical


BOBBY_WORDS = [("en-us", "dˈiːp"), ("en-us", "mˈɑnlɪ"), ("hu", "ɡˈɛ"), ("es", "dˈos")]


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang,ipa", BOBBY_WORDS)
def test_bobby_keeps_his_targets(lang, ipa, speed):
    """Bobby's F1-F3 multipliers are 1: every target is the default voice's.
    (Spanish renders a steady state and works its parallel targets out from
    the start values, which Bobby's phonemeOverrides replace: cascade only.)"""
    plain, pspans = render(lang, ipa, speed)
    bobby, bspans = render(lang, ipa, speed, "Bobby")
    assert bspans == pspans
    seen = 0
    for p, q in zip(plain, bobby):
        if p.params is None:
            continue
        for e in (ENDS[:3] if lang == "es" else ENDS):
            if finite(p.ex[e]):
                seen += 1
                assert q.ex[e] == p.ex[e], e
            else:
                assert not finite(q.ex[e]), e
    assert seen


@pytest.mark.parametrize("speed", SPEEDS)
def test_bobby_diphthong_keeps_its_offglide_formants_and_his_bandwidths(speed):
    plain, pspans = render("en-us", "bˈaɪ", speed)
    bobby, bspans = render("en-us", "bˈaɪ", speed, "Bobby")
    pg, bg = frames_of(plain, pspans, "ɑ"), frames_of(bobby, bspans, "ɑ")
    for k in F123:
        assert bg[-1].params[k] == pg[-1].params[k], k
    # cb_mul and pb_mul 1.05, 1.05, 1.0: the glide ends as much wider as it starts.
    for k in B123:
        first = bg[0].params[k] / pg[0].params[k]
        assert bg[-1].params[k] / pg[-1].params[k] == pytest.approx(first, rel=1e-6), k
    assert bg[0].params["cb1"] / pg[0].params["cb1"] == pytest.approx(1.05, rel=1e-6)


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang,ipa", [("en-us", "bˈaɪ"), ("en-us", "dˈiːp"), ("hu", "ɡˈɛ"), ("es", "dˈos")])
def test_no_profile_is_untouched_by_a_profile_before_it(lang, ipa, speed):
    """Turning a profile off again gives the frames of a frontend that never had one."""
    fresh = repr([(f.params, f.ex, f.duration_ms, f.fade_ms) for f in render(lang, ipa, speed)[0]])
    with tgspeechbox.Frontend(lang) as fe:
        fe.set_voice_profile("Beth")
        fe.frames(ipa, speed, 110.0, 0.5, ".")
        fe.set_voice_profile("")
        after = repr([(f.params, f.ex, f.duration_ms, f.fade_ms) for f in fe.frames(ipa, speed, 110.0, 0.5, ".")])
    assert after == fresh


# A profile whose cascade and parallel multipliers differ, and whose consonant
# classes compound, to tell the multipliers apart.
SYNTHETIC = """  T173:
    classScales:
      vowel:
        cf_mul: [1.20, 1.30, 1.10, 1, 1, 1]
        pf_mul: [1.05, 1.25, 1.15, 1, 1, 1]
        cb_mul: [1.40, 1.50, 1.60, 1, 1, 1]
        pb_mul: [1.10, 1.20, 1.30, 1, 1, 1]
      consonant:
        cf_mul: [1.00, 1.10, 1.00, 1, 1, 1]
      nasal:
        cf_mul: [1.00, 1.20, 1.00, 1, 1, 1]
    phonemeOverrides: {}
"""


@pytest.fixture(scope="module")
def synthetic_root(tmp_path_factory):
    root = tmp_path_factory.mktemp("profile173")
    shutil.copytree(REPO / "packs", root / "packs")
    path = root / "packs" / "phonemes.yaml"
    text = path.read_text(encoding="utf-8")
    assert text.count("\nvoiceProfiles:\n") == 1
    path.write_text(text.replace("\nvoiceProfiles:\n", "\nvoiceProfiles:\n" + SYNTHETIC), encoding="utf-8")
    return str(root)


@pytest.mark.parametrize("speed", SPEEDS)
def test_each_target_gets_its_own_multiplier(synthetic_root, speed):
    plain, pspans = render("en-us", "bˈaɪ", speed, root=synthetic_root)
    prof, sspans = render("en-us", "bˈaɪ", speed, "T173", root=synthetic_root)
    pg, sg = frames_of(plain, pspans, "ɑ"), frames_of(prof, sspans, "ɑ")
    want = dict(cf1=1.20, cf2=1.30, cf3=1.10, pf1=1.05, pf2=1.25, pf3=1.15,
                cb1=1.40, cb2=1.50, cb3=1.60, pb1=1.10, pb2=1.20, pb3=1.30)
    for k, m in want.items():
        assert sg[0].params[k] / pg[0].params[k] == pytest.approx(m, rel=1e-6), ("start", k)
        assert sg[-1].params[k] / pg[-1].params[k] == pytest.approx(m, rel=1e-6), ("end", k)


@pytest.mark.parametrize("speed", SPEEDS)
def test_parallel_target_that_follows_the_cascade_gets_the_parallel_multiplier(synthetic_root, speed):
    """A vowel with only a cascade end target: the parallel formant heads for
    the same canonical value, scaled as the parallel formants are."""
    plain, pspans = render("en-us", "dˈiːp", speed, root=synthetic_root)
    prof, sspans = render("en-us", "dˈiːp", speed, "T173", root=synthetic_root)
    assert sspans == pspans
    a, _ = span_of(pspans, "i")
    for e, m in (("endCf1", 1.20), ("endCf2", 1.30), ("endCf3", 1.10), ("endPf1", 1.05), ("endPf2", 1.25), ("endPf3", 1.15)):
        assert finite(plain[a].ex[e])
        assert prof[a].ex[e] / plain[a].ex[e] == pytest.approx(m, rel=1e-6), e


@pytest.mark.parametrize("speed", SPEEDS)
def test_consonant_targets_get_the_product_of_their_classes(synthetic_root, speed):
    """/m/ is a consonant and a nasal: F2 times 1.10 times 1.20, start and target."""
    plain, pspans = render("en-us", "mˈɑnlɪ", speed, root=synthetic_root)
    prof, sspans = render("en-us", "mˈɑnlɪ", speed, "T173", root=synthetic_root)
    assert sspans == pspans
    a, _ = span_of(pspans, "m")
    assert prof[a].params["cf2"] / plain[a].params["cf2"] == pytest.approx(1.32, rel=1e-6)
    assert finite(plain[a].ex["endCf2"])
    assert prof[a].ex["endCf2"] / plain[a].ex["endCf2"] == pytest.approx(1.32, rel=1e-6)
    # F1 and F3 are not scaled for consonants, nor are the parallel formants.
    for e in ("endCf1", "endCf3", "endPf1", "endPf2", "endPf3"):
        if finite(plain[a].ex[e]):
            assert prof[a].ex[e] == pytest.approx(plain[a].ex[e], rel=1e-12), e
    # /l/ is a consonant only.
    la, lb = span_of(pspans, "l")
    seen = 0
    for i in range(la, lb):
        if plain[i].params is not None and finite(plain[i].ex["endCf2"]):
            seen += 1
            assert prof[i].ex["endCf2"] / plain[i].ex["endCf2"] == pytest.approx(1.10, rel=1e-6)
    assert seen
