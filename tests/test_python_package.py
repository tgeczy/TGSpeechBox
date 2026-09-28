"""The tgspeechbox Python package (python/), run against this tree's build.

The wheel carries the two libraries and packs/; here the package is imported
from python/ with TGSB_LIB_DIR pointing at the build tests/conftest.py just
made, so a change to the engine or the package fails here before any wheel
is built.  The wheel CI runs the same smoke test on each platform's wheel.
"""
from __future__ import annotations

import os
import pathlib
import re
import subprocess
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
_ARCH = "x86" if sys.maxsize <= 2 ** 32 else "x64"
os.environ.setdefault("TGSB_LIB_DIR", str(REPO / f"build-{_ARCH}-nvda" / "MinSizeRel"))
os.environ.setdefault("TGSB_PACKS_ROOT", str(REPO))
sys.path.insert(0, str(REPO / "python"))

import tgspeechbox  # noqa: E402
from tgspeechbox import _structs  # noqa: E402


def test_render_makes_speech():
    u = tgspeechbox.render("həˈloʊ wˈɜːld", "en-us")
    assert u.sample_rate == 22050
    assert 0.2 < u.duration_s < 2.0
    assert max(abs(s) for s in u.samples) > 1000, "rendered audio is (near) silent"
    voiced = [f for f in u.frames if not f.is_silence and f.params["voiceAmplitude"] > 0.1]
    assert voiced, "no voiced frames"
    assert all(50 < f.params["voicePitch"] < 400 for f in voiced)


def test_same_ipa_same_frames_as_the_test_wrapper(frontend, pack_dir):
    """The package and tests/_test_frontend.py read the same library the same way."""
    ipa = "kˈæt"
    h = frontend.create_handle(pack_dir)
    try:
        frontend.set_language(h, "en-us")
        ref = frontend.capture_frames(h, ipa, speed=1.0, base_pitch=110.0, inflection=0.5)
    finally:
        frontend.destroy_handle(h)
    with tgspeechbox.Frontend("en-us") as fe:
        got = fe.frames(ipa, speed=1.0, pitch=110.0, inflection=0.5)
    assert len(got) == len(ref)
    for g, r in zip(got, ref):
        assert g.is_silence == r.is_silence
        assert abs(g.duration_ms - r.duration_ms) < 1e-9
        if not g.is_silence:
            assert g.params["cf2"] == pytest.approx(r.frame_dict["cf2"])


def test_voice_profiles_and_their_voice_source():
    with tgspeechbox.Frontend("en-us") as fe:
        assert {"Beth", "Bobby"} <= set(fe.voice_profiles())
        fe.set_voice_profile("Beth")
        tone = fe.voicing_tone()
    assert tone["voicedTiltDbPerOct"] == pytest.approx(-10.0)
    u = tgspeechbox.render("həˈloʊ", "en-us", voice="Beth")
    assert u.samples


def test_traces_name_the_phonemes():
    with tgspeechbox.Frontend("en-us") as fe:
        fe.frames("kˈæt")
        keys = [e["phonemeKey"] for e in fe.frame_trace()]
        passes = {e["pass"] for e in fe.pass_trace()}
    assert any(k.startswith("k") for k in keys) and any(k.startswith("æ") for k in keys)
    assert passes, "no pass snapshots"


def test_player_voicing_tone_round_trips():
    with tgspeechbox.Player() as pl:
        pl.set_voicing_tone(voicedTiltDbPerOct=-4.0, chorusDepth=0.3)
        t = pl.voicing_tone()
    assert t.voicedTiltDbPerOct == pytest.approx(-4.0)
    assert t.chorusDepth == pytest.approx(0.3)
    # Settings not named keep the DSP's own values, not zero.
    assert t.voicingPeakPos == pytest.approx(0.91)
    assert t.speedQuotient == pytest.approx(2.0)
    assert t.highShelfFcHz == pytest.approx(2000.0)


def test_struct_mirrors_match_the_c_headers():
    for gen in ("tools/gen_frame_ex.py", "tools/gen_voicing_tone.py"):
        r = subprocess.run([sys.executable, str(REPO / gen), "--check"], capture_output=True, text=True)
        assert r.returncode == 0, f"{gen} --check: {r.stdout}{r.stderr}"
    text = (REPO / "src" / "frontend" / "nvspFrontend.h").read_text(encoding="utf-8")
    body = re.search(r"typedef\s+struct\s+nvspFrontend_Frame\s*\{(.*?)\}\s*nvspFrontend_Frame\s*;", text, re.S).group(1)
    names = []
    for line in body.splitlines():
        m = re.match(r"double\s+(.+);$", line.split("//")[0].strip())
        if m:
            names += [n.strip() for n in m.group(1).split(",")]
    assert list(_structs.FRAME_FIELDS) == names


def test_command_line(tmp_path):
    out = tmp_path / "x.wav"
    env = dict(os.environ, PYTHONPATH=str(REPO / "python"), PYTHONIOENCODING="utf-8")
    r = subprocess.run([sys.executable, "-m", "tgspeechbox", "--ipa", "həˈloʊ", "--out", str(out)],
                       capture_output=True, text=True, env=env, encoding="utf-8")
    assert r.returncode == 0, r.stderr
    assert out.stat().st_size > 1000
