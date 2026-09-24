"""Spanish-Mexican phonemization regression tests (es-mx).

Bug-driven: each test pins a real shipped issue from GitHub
(#74, #81, #84, #95). These exercise the IPA -> frame-stream pipeline
via nvspFrontend_queueIPA_Ex.

KNOWN LIMITATION: the frontend doesn't currently expose a phoneme-attribution
API on the frame callback (we get per-frame DSP parameters, not "this frame
came from phoneme /g/"). So these tests can detect:

  - Crashes / NULL outputs when the engine can't process an input
  - Total frame count regressions (entire word was dropped vs preserved)
  - Acoustic divergence between two known-different inputs (e.g. /g/ vs /l/
    between vowels MUST produce different frame streams; if they don't, the
    engine has lost its ability to distinguish them)

Once a phoneme-attribution API is added, these tests can become more precise:
"frame at index N was attributed to phoneme /ɣ/, not /l/." For now they're
acoustic-differentiation tests, which still catch real regressions.
"""
from __future__ import annotations

import pytest

from _test_frontend import voiced_frames


def _summary(records):
    """Compact text summary of a captured frame stream — useful in assertion messages."""
    voiced = voiced_frames(records)
    silences = [r for r in records if r.is_silence]
    total_ms = sum(r.duration_ms for r in records)
    return (f"{len(records)} records ({len(voiced)} voiced, {len(silences)} silence), "
            f"~{total_ms:.0f}ms total")


# ---------------------------------------------------------------------------
# Smoke: the engine can synthesize Spanish IPA at all.
# ---------------------------------------------------------------------------

def test_es_mx_synthesizes_simple_word(es_mx):
    """Sanity: 'hola' should produce a non-empty voiced frame stream."""
    fe, h = es_mx
    records = fe.capture_frames(h, "ola")  # /ola/ — IPA for "hola" (h is silent in Spanish)
    voiced = voiced_frames(records)
    assert len(voiced) > 0, f"Expected voiced frames for /ola/; got {_summary(records)}"


# ---------------------------------------------------------------------------
# Acoustic differentiation: phonemes that SHOULD sound different must produce
# distinguishable frame streams. If they collapse to identical output, the
# engine has lost the ability to distinguish them — i.e. a regression that
# would manifest as "X sounds like Y" user reports.
# ---------------------------------------------------------------------------

def test_g_and_l_acoustic_distinction_minimal_context(es_mx):
    """Issue #84/#95 regression guard: 'entregado' was sounding like 'entrelado'
    (intervocalic /g/ allophone /ɣ/ collapsing acoustically toward /l/).

    METHODOLOGY: minimal 3-phoneme context (/aɣa/ vs /ala/). Each input
    produces exactly 3 voiced frames (vowel-consonant-vowel), so frame[1] is
    reliably the consonant — no need for phoneme attribution on the callback.

    This sidesteps the vowel-domination problem that breaks aggregate-stat
    tests on whole words: in /entreɣaðo/ vs /entrelaðo/ the consonant
    contributes ~3 frames out of ~50, so MIN/AVG/MAX over the whole word
    is dominated by /e/ /a/ /o/ and the consonant difference is invisible.

    Empirical baseline (es-mx, default speed/pitch, post-3.10-beta-1):
      /aɣa/ middle frame: F1=450, F2=1450, voiceAmplitude=0.82
      /ala/ middle frame: F1=350, F2=1400, voiceAmplitude=0.90
    The 100 Hz F1 gap and the ~0.08 voiceAmplitude dip on /ɣ/ are the engine
    actually distinguishing these phonemes. If they collapse to identical
    parameters, this test fires.
    """
    fe, h = es_mx
    g_frames = voiced_frames(fe.capture_frames(h, "aɣa"))
    l_frames = voiced_frames(fe.capture_frames(h, "ala"))

    # Frame count is exact for minimal 3-phoneme inputs. If this ever produces
    # different counts, frame emission timing has changed — worth knowing.
    assert len(g_frames) == 3, (
        f"Expected 3 voiced frames for /aɣa/, got {len(g_frames)}: {g_frames}"
    )
    assert len(l_frames) == 3, (
        f"Expected 3 voiced frames for /ala/, got {len(l_frames)}: {l_frames}"
    )

    # Frame [1] is the consonant in V-C-V structure.
    g = g_frames[1].frame_dict
    l = l_frames[1].frame_dict

    # F1 must differ by at least 50 Hz. Empirical delta is ~100 Hz; a 50 Hz
    # threshold catches collapse without being so tight it false-fails on
    # legitimate small parameter retunings.
    f1_delta = abs(g["cf1"] - l["cf1"])
    assert f1_delta > 50.0, (
        f"/ɣ/ and /l/ collapsed to identical F1 (delta={f1_delta:.0f} Hz). "
        f"This is the issue #84/#95 acoustic regression — the engine has "
        f"lost the F1 distinction between intervocalic /g/-allophone and /l/. "
        f"ɣ frame: F1={g['cf1']:.0f} F2={g['cf2']:.0f} vAmp={g['voiceAmplitude']:.2f}; "
        f"l frame: F1={l['cf1']:.0f} F2={l['cf2']:.0f} vAmp={l['voiceAmplitude']:.2f}"
    )


# ---------------------------------------------------------------------------
# Dialect divergence: es-mx and es-es should produce different output for
# words where the dialects differ (e.g. /s/ realization). If they don't,
# the dialect replacement (s -> s_es vs s -> s_mx) isn't firing.
# ---------------------------------------------------------------------------

def _s_frame(records):
    """The /s/ of "kasa": the strongest frication frame between the two vowels
    (the /k/ burst before the first vowel is not it)."""
    voiced = [i for i, r in enumerate(records) if r.frame_dict["voiceAmplitude"] > 0.5]
    assert len(voiced) >= 2, "kasa should have two vowels"
    between = [r for r in records[voiced[0] + 1:voiced[-1]] if r.frame_dict["fricationAmplitude"] > 0.5]
    assert between, "no frication between the vowels of kasa: the /s/ is missing"
    return max(between, key=lambda r: r.frame_dict["fricationAmplitude"]).frame_dict


def test_mexican_s_diverges_from_spain_s(es_mx, es_es):
    """Issue #74/#81: Mexican /s/ was sounding identical to Spain /s/ in beta 1.

    The dialect packs replace /s/ with different phonemes (s -> s_mx, s -> s_es)
    whose frication is shaped differently.  If the replacement isn't firing,
    the /s/ of "casa" reaches the DSP as the same frame in both dialects.

    Compares the /s/ frame's parallel (frication) spectrum, pa1..pa6, rather
    than one parameter: the #100 fix gave both /s/ phonemes pa6 0.9 at 6.5 kHz
    (it had been 0.1 for s_es), and the difference is now the apical s_es's
    energy at F5 (pa5 0.9 at 3.75 kHz), which the laminal s_mx has none of.
    Which parameter carries the difference may move again; that it exists
    must not.
    """
    fe_mx, h_mx = es_mx
    fe_es, h_es = es_es

    # /s/ is voiceless, so all non-silence frames, not voiced_frames().
    mx = _s_frame([r for r in fe_mx.capture_frames(h_mx, "kasa") if not r.is_silence])
    es = _s_frame([r for r in fe_es.capture_frames(h_es, "kasa") if not r.is_silence])

    pa = [f"pa{i}" for i in range(1, 7)]
    delta = sum(abs(mx[k] - es[k]) for k in pa)
    assert delta > 0.3, (
        f"es-mx and es-es produced near-identical /s/ frication for 'casa' "
        f"(total |pa| difference {delta:.2f}). Dialect replacement (s -> s_mx vs "
        f"s -> s_es) likely not firing: the issue #81 regression. "
        f"mx {[round(mx[k], 2) for k in pa]}, es {[round(es[k], 2) for k in pa]}"
    )
