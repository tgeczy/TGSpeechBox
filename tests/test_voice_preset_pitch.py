"""Every platform gives each built-in voice the same preset (#144, #92, #200).

Greg: on Linux, "starting with Benjamin" every voice spoke higher.  In April
(#92) Linux's Adam alone got a 0.92 pitch multiplier to sound lower; the
real cause then was a module that started at the wrong pitch, fixed in the
same change.  That left Linux's Adam 8% below Benjamin, where every other
platform has them level.  Measured on coconut with default settings, the
Linux module now matches NVDA for every voice (Benjamin 104.7 Hz on both),
and Adam with it.

#200: Robert's preset carried a -6 dB/oct voicing tilt.  The NVDA driver
has not applied it since 2.60 (a preset's keys reach the engine only as frame
fields, and the tilt is not one; releases 1.85 to 2.50 looked it up and sent
it), and SAPI never did; the Android and iOS bridges did, so
Robert was brighter on a phone than on Windows.  NVDA is the reference: the
tilt is gone from every host.

The built-in voices are written out once per host (NVDA's constants.py, the
SAPI engine, the Android and iOS bridges, the Speech Dispatcher module,
tgsbRender and the phoneme editor); this reads each host's whole preset for
each voice (every frame field it scales or sets, and the voicing tilt) and
holds it to NVDA's.

Not held here: SAPI and the Speech Dispatcher module have no per-voice head
size, so their David does not get the head size 100 that NVDA, Android, iOS
and the phoneme editor give him.
"""
from __future__ import annotations

import ast
import pathlib
import re

import pytest

ROOT = pathlib.Path(__file__).resolve().parents[1]
VOICES = ["Adam", "Benjamin", "Caleb", "David", "Robert"]
NUM = r"-?[0-9]+(?:\.[0-9]+)?"


def _read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def frame_fields():
    """The names of speechPlayer_frame_t's fields, from src/frame.h."""
    text = _read("src/frame.h")
    body = text[text.index("typedef struct {"):text.index("} speechPlayer_frame_t;")]
    names = []
    for line in body.splitlines():
        m = re.match(r"\s*speechPlayer_frameParam_t\s+([^;]+);", line.split("//")[0])
        if m:
            names += [n.strip() for n in m.group(1).split(",")]
    assert len(names) == 47, names
    return set(names)


def _op(frame, field, kind, value):
    """Record one preset operation; a multiplier of 1 does nothing and is left out."""
    value = float(value)
    if kind == "mul" and value == 1.0:
        return
    assert field not in frame, f"{field} is both scaled and set"
    frame[field] = (kind, value)


def nvda_presets():
    text = _read("nvdaAddon/synthDrivers/tgSpeechBox/constants.py")
    start = text.index("voices = {") + len("voices = ")
    return ast.literal_eval(text[start:text.index("\n}\n", start) + 2])


def nvda():
    # profile_utils.buildVoiceOps: a key reaches the engine when it names a
    # frame field ("x" sets it, "x_mul" scales it).  The driver reads no
    # voicing tone from a preset, so the tilt of a built-in voice is the
    # engine's default, 0.
    fields = frame_fields()
    out = {}
    for v, preset in nvda_presets().items():
        frame = {}
        for k, val in preset.items():
            if k.endswith("_mul") and k[:-4] in fields:
                _op(frame, k[:-4], "mul", val)
            elif k in fields:
                _op(frame, k, "abs", val)
        out[v] = {"frame": frame, "tilt": 0.0}
    return out


def sapi():
    text = _read("src/sapi/tgsb_runtime.cpp")
    text = text[text.index("void runtime::apply_preset_and_volume"):]
    out = {}
    for v in VOICES:
        start = text.index(f'eq(preset, L"{v}")')
        block = text[start:text.index("\n    }", start)]
        frame = {}
        for field, op, val in re.findall(r"\bf\.(\w+) (\*=|=) (%s);" % NUM, block):
            _op(frame, field, "mul" if op == "*=" else "abs", val)
        m = re.search(r"voicedTiltDbPerOct\s*=\s*(%s)" % NUM, block)
        out[v] = {"frame": frame, "tilt": float(m.group(1)) if m else 0.0}
    return out


def bridge(rel):
    text = _read(rel)
    out = {}
    for v in VOICES:
        start = text.index(f"k{v}Overrides[] = {{")
        block = text[start:text.index("};", start)]
        frame = {}
        for field, val, mul in re.findall(r"\{ OFF\((\w+)\), (%s), ([01]) \}" % NUM, block):
            _op(frame, field, "mul" if mul == "1" else "abs", val)
        m = re.search(r'PRESET\("%s",\s*k%sOverrides,\s*(%s),\s*([01]),\s*(%s)\)'
                      % (v.lower(), v, NUM, NUM), text)
        out[v] = {"frame": frame, "tilt": float(m.group(1)) if m.group(2) == "1" else 0.0}
        if float(m.group(3)) > 0.0:  # 0 = the engine's default
            out[v]["f4FreqScale"] = float(m.group(3))
    return out


def table(rel):
    """The BuiltinVoice table of the Speech Dispatcher module and tgsbRender."""
    text = _read(rel)
    start = text.index("struct BuiltinVoice {")
    decl = re.sub(r"//[^\n]*", "", text[start:text.index("};", start)])
    members = []
    for names in re.findall(r"\b(?:double|bool)\s+([^;]+);", decl):
        members += [n.strip() for n in names.split(",")]
    start = text.index("kBuiltinVoices[] = {")
    rows = re.sub(r"//[^\n]*", "", text[start:text.index("};", start)])
    out = {}
    for v in VOICES:
        vals = [x.strip() for x in re.search(r'\{"%s",([^}]*)\}' % v, rows).group(1).split(",")]
        assert len(vals) == len(members), (v, len(vals), len(members))
        row = dict(zip(members, vals))
        frame = {}
        for name, val in row.items():
            if name.endswith("_mul") or name.endswith("_abs"):
                assert f"v.{name}" in text, f"{rel}: {name} is in the table and never applied"
                if val != "NAN":
                    _op(frame, name[:-4], name[-3:], val)
        tilt = float(row["voicedTiltDbPerOct"]) if row["hasVoicedTilt"] == "true" else 0.0
        out[v] = {"frame": frame, "tilt": tilt}
    return out


def editor():
    text = _read("tools/tgsbPhonemeEditorWin32/tgsb_runtime.cpp")
    text = text[text.index("void TgsbRuntime::applySpeechSettingsToFrame"):]
    out = {}
    for v in VOICES:
        start = text.index(f'voice == "{v}") {{')
        block = text[start:text.index("} else", start)]
        frame = {}
        for kind, field, val in re.findall(r'\b(mul|abs)ByName\("(\w+)", (%s)\)' % NUM, block):
            _op(frame, field, kind, val)
        out[v] = {"frame": frame, "tilt": 0.0}
    return out


HOSTS = {
    "sapi": sapi,
    "android": lambda: bridge("src/platforms/android/jni/tgsb_jni.cpp"),
    "ios": lambda: bridge("src/platforms/ios/bridge/tgsb_bridge.cpp"),
    "speech-dispatcher": lambda: table("src/platforms/speechd/sd_tgsb.cpp"),
    "tgsbRender": lambda: table("tools/tgsbRender.cpp"),
    "phoneme-editor": editor,
}


@pytest.mark.parametrize("voice", VOICES)
@pytest.mark.parametrize("host", list(HOSTS))
def test_voice_preset_matches_nvda(host, voice):
    assert HOSTS[host]()[voice] == nvda()[voice]


@pytest.mark.parametrize("host", list(HOSTS))
def test_voice_pitch_matches_nvda(host):
    def pitch(presets):
        return {v: presets[v]["frame"].get("voicePitch", ("mul", 1.0)) for v in VOICES}
    assert pitch(HOSTS[host]()) == pitch(nvda())


def test_nvda_has_the_five_voices():
    assert list(nvda_presets()) == VOICES


@pytest.mark.parametrize("voice", VOICES)
def test_nvda_preset_holds_only_what_the_driver_applies(voice):
    """A key the driver ignores reads as part of the voice and is not (#200:
    "voicedTiltDbPerOct": -6.0 sat in Robert's preset and never reached the
    engine).  The driver applies frame fields (buildVoiceOps) and reads the
    head size default by name (voice_management.py)."""
    assert 'voices.get(voice, {}).get("headSize", 50)' in _read(
        "nvdaAddon/synthDrivers/tgSpeechBox/voice_management.py")
    fields = frame_fields()
    ignored = [k for k in nvda_presets()[voice]
               if k != "headSize" and k not in fields
               and not (k.endswith("_mul") and k[:-4] in fields)]
    assert ignored == []


# ── Head size ───────────────────────────────────────────────────────────────
# NVDA keeps a voice's default head size in its preset ("headSize", 50 when
# absent).  Android and iOS keep it where they load the setting, and the
# phoneme editor in presetVoicingDefault.

def nvda_head_sizes():
    return {v.lower(): p["headSize"] for v, p in nvda_presets().items() if "headSize" in p}


HEAD_SIZE_FILES = [
    "src/platforms/android/app/src/main/kotlin/com/tgspeechbox/tts/TgsbTtsService.kt",
    "src/platforms/android/app/src/main/kotlin/com/tgspeechbox/tts/TgsbViewModel.kt",
    "src/platforms/ios/au-extension/TGSBAudioUnit.swift",
    "src/platforms/ios/host-app/EngineSettingsView.swift",
    "src/platforms/ios/host-app/TgsbEngine.swift",
]


@pytest.mark.parametrize("rel", HEAD_SIZE_FILES)
def test_head_size_default_matches_nvda(rel):
    """Every place a phone app loads the head size setting falls back to the
    voice's NVDA default: 100 for David, 50 for the others."""
    sites = re.findall(r'\b(?:load|loadV|loadSlider|prefFloat)\((?:d, )?"headSize", ([^\n]*)', _read(rel))
    assert sites, rel
    for default in sites:
        m = re.match(r'(?:if \()?\w+ == "(\w+)"\)? (?:\? )?(\d+)f? (?:else|:) (\d+)f?[,)]', default)
        assert m, f'{rel}: head size default is not per voice: "headSize", {default}'
        assert ({m.group(1): int(m.group(2))}, int(m.group(3))) == (nvda_head_sizes(), 50), default


def test_editor_head_size_default_matches_nvda():
    text = _read("tools/tgsbPhonemeEditorWin32/Dialogs.cpp")
    body = text[text.index("static int presetVoicingDefault("):]
    body = body[:body.index("\n}")]
    got = {v.lower(): int(n) for v, n in
           re.findall(r'voice == "(\w+)" && param == "f4FreqScale"\) return (\d+);', body)}
    assert got == nvda_head_sizes()


def test_editor_profile_seed_matches_nvda():
    """"Save to Profile" starts a new profile from the built-in voice's
    formant scales (profile_tone.cpp); they are NVDA's."""
    text = _read("tools/tgsbPhonemeEditorWin32/profile_tone.cpp")
    text = text[text.index("static void seedClassScalesFromPreset("):]
    want = nvda()
    seen = 0
    for v in VOICES:
        start = text.index(f'voice == "{v}") {{')
        block = text[start:text.index("} else", start)]
        for group, vals in re.findall(r"arr\(s\.(\w\w)_mul, s\.\w+, \{([^}]*)\}\)", block):
            for i, val in enumerate(vals.split(","), start=1):
                kind, scale = want[v]["frame"].get(f"{group}{i}", ("mul", 1.0))
                if kind == "abs":  # no class-scale form; the editor says so in its note
                    continue
                assert float(val) == scale, f"{v} {group}{i}"
                seen += 1
    assert seen >= 60
