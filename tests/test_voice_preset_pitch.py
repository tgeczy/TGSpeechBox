"""Every platform gives each built-in voice the same pitch (#144, #92).

Greg: on Linux, "starting with Benjamin" every voice spoke higher.  In April
(#92) Linux's Adam alone got a 0.92 pitch multiplier to sound lower; the
real cause then was a module that started at the wrong pitch, fixed in the
same change.  That left Linux's Adam 8% below Benjamin, where every other
platform has them level.  Measured on coconut with default settings, the
Linux module now matches NVDA for every voice (Benjamin 104.7 Hz on both),
and Adam with it.

The built-in voices are written out once per host (NVDA's constants.py, the
SAPI engine, the Android and iOS bridges, the Speech Dispatcher module and
tgsbRender); this reads each host's pitch multiplier for each voice and holds
them to NVDA's.
"""
from __future__ import annotations

import pathlib
import re

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[1]
VOICES = ["Adam", "Benjamin", "Caleb", "David", "Robert"]


def _read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def nvda():
    text = _read("nvdaAddon/synthDrivers/tgSpeechBox/constants.py")
    body = text[text.index("voices = {"):]
    out = {}
    for v in VOICES:
        block = body[body.index(f'"{v}": {{'):]
        block = block[:block.index("\n    },")]
        m = re.search(r'"voicePitch_mul":\s*([0-9.]+)', block)
        out[v] = float(m.group(1)) if m else 1.0
    return out


def sapi():
    text = _read("src/sapi/tgsb_runtime.cpp")
    out = {}
    for v in VOICES:
        start = text.index(f'eq(preset, L"{v}")')
        nxt = text.find("else if", start + 1)
        block = text[start:nxt if nxt > 0 else start + 2000]
        m = re.search(r"f\.voicePitch \*= ([0-9.]+);", block)
        out[v] = float(m.group(1)) if m else 1.0
    return out


def bridge(rel):
    text = _read(rel)
    out = {}
    for v in VOICES:
        start = text.index(f"k{v}Overrides[] = {{")
        block = text[start:text.index("};", start)]
        m = re.search(r"\{ OFF\(voicePitch\), ([0-9.]+), 1 \}", block)
        out[v] = float(m.group(1)) if m else 1.0
    return out


def speechd():
    text = _read("src/platforms/speechd/sd_tgsb.cpp")
    return {v: float(re.search(r'\{"%s",\s*([0-9.]+),' % v, text).group(1)) for v in VOICES}


def tgsb_render():
    text = _read("tools/tgsbRender.cpp")
    return {v: float(re.search(r'\{"%s",\s*(?://[^\n]*\n\s*)?([0-9.]+),' % v, text).group(1)) for v in VOICES}


HOSTS = {
    "sapi": sapi,
    "android": lambda: bridge("src/platforms/android/jni/tgsb_jni.cpp"),
    "ios": lambda: bridge("src/platforms/ios/bridge/tgsb_bridge.cpp"),
    "speech-dispatcher": speechd,
    "tgsbRender": tgsb_render,
}


@pytest.mark.parametrize("host", list(HOSTS))
def test_voice_pitch_matches_nvda(host):
    assert HOSTS[host]() == nvda()
