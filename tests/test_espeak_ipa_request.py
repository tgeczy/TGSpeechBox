"""Every host asks eSpeak NG for IPA the same way, NVDA's way.

"TGSpeechBox still sounds like a British American speaker on the iPhone":
the Android and iOS apps, the Linux module and tgsbRender asked eSpeak for IPA
with phoneme mode 0x02, which writes a diphthong as two bare vowels (nˈaʊ).
The NVDA add-on and the SAPI engine ask with 0x36100 + 0x82: the same IPA
with a tie bar (U+0361) joining the symbols of one sound (nˈa͡ʊ).

The language packs are tuned by ear on NVDA, on the tied form, and some of
their rules only take it.  With eSpeak NG 1.53, 186 of 2,500 US English
words came out as different frames on the phones: every MOUTH word (now,
how, out, about), every NEAR and SQUARE word (here, year, there, where),
and many of "for", "more", "car", "sure".  UK English had none, so only the
American vowels were off.  Danish was hit hardest (the stød is tied to its
vowel), then Polish, Dutch and Finnish.

One request for every host: the one NVDA makes, since most people use
TGSpeechBox there and the packs are tuned there.
"""
from __future__ import annotations

import ast
import pathlib
import re

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent

# Each host that calls espeak_TextToPhonemes itself, and how many times.
HOSTS = {
    "src/sapi/tgsb_runtime.cpp": 1,
    "src/platforms/android/jni/tgsb_jni.cpp": 3,
    "src/platforms/ios/bridge/tgsb_bridge.cpp": 2,
    "src/platforms/speechd/sd_tgsb.cpp": 1,
    "tools/tgsbRender.cpp": 1,
}
_CALL = re.compile(r"(?<![\w>.])(?:espeak_)?TextToPhonemes\s*\(([^;]*?)\)\s*;", re.S)
_NUM = re.compile(r"^[0-9a-fA-FxX+|\s()]+$")


def _strip_comments(src):
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    return re.sub(r"//[^\n]*", " ", src)


def _value(expr, src, path):
    """An int from a literal expression, or from a constant the file defines."""
    expr = expr.strip()
    if not _NUM.match(expr):
        m = re.search(rf"\b{re.escape(expr)}\s*=\s*([^;]+);", src)
        assert m, f"{path}: can't find the value of {expr!r}"
        expr = m.group(1).strip()
    assert _NUM.match(expr), f"{path}: not a number: {expr!r}"
    return eval(expr, {"__builtins__": {}})  # digits, hex, + | ( ) only


def _nvda_mode():
    tree = ast.parse((REPO / "nvdaAddon/synthDrivers/tgSpeechBox/__init__.py").read_text(encoding="utf-8"))
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and any(getattr(t, "id", None) == "_ESPEAK_PHONEME_MODE"
                                                for t in node.targets):
            return eval(compile(ast.Expression(node.value), "<mode>", "eval"), {"__builtins__": {}})
    raise AssertionError("NVDA's _ESPEAK_PHONEME_MODE was not found")


def test_nvda_asks_for_ipa_with_tie_bars():
    mode = _nvda_mode()
    assert mode & 0x02, "bit 1: IPA names"
    assert mode & 0x80, "bit 7: use a tie character"
    assert (mode >> 8) == 0x361, "the tie character is U+0361"


@pytest.mark.parametrize("path,calls", sorted(HOSTS.items()))
def test_every_host_asks_espeak_the_way_nvda_does(path, calls):
    src = _strip_comments((REPO / path).read_text(encoding="utf-8"))
    found = [m.group(1) for m in _CALL.finditer(src) if m.group(1).count(",") == 2]
    assert len(found) == calls, f"{path}: expected {calls} espeak_TextToPhonemes calls, found {len(found)}"
    want = _nvda_mode()
    for args in found:
        mode = _value(args.rsplit(",", 1)[1], src, path)
        assert mode == want, (
            f"{path}: asks eSpeak with phoneme mode {mode:#x}; NVDA asks with {want:#x} "
            "(IPA with U+0361 tie bars), and the packs are tuned on that")


def test_no_other_host_calls_espeak_unchecked():
    """A new host (or a new call) must be added to HOSTS, so it is held to the same request."""
    seen = set()
    for root in ("src", "tools"):
        for p in (REPO / root).rglob("*"):
            if p.suffix not in (".cpp", ".c", ".h", ".mm", ".m", ".swift", ".kt") or ".cxx" in p.parts:
                continue
            rel = p.relative_to(REPO).as_posix()
            if any(part.startswith("build") for part in p.parts):
                continue
            src = _strip_comments(p.read_text(encoding="utf-8", errors="ignore"))
            if any(m.group(1).count(",") == 2 for m in _CALL.finditer(src)):
                seen.add(rel)
    assert seen == set(HOSTS), f"hosts calling espeak_TextToPhonemes: {sorted(seen)}"
