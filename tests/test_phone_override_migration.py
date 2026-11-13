"""The phone apps drop stored overrides for the timing settings nobody heard (#172).

"Import pack" on Android and iOS stores every setting of the imported file as
an override, the lines the shipped pack itself had among them, and applies
them again after every language load.  Until 3.10 release candidate 4 the
frontend read none of its sixteen phoneme timing settings, so those stored
values did nothing.  With #172 they are read: en-us had shipped
postStopAspirationDurationMs 35 since 3.01, and an English listener who once
imported a pack would have got a 35 ms puff after p, t and k where 20 ms had
always played (es, da and pt-br carried such a line too).

Each app removes the stored overrides for those sixteen settings once:
PackOverrideMigration.kt on Android (its logic has a JVM test,
PackOverrideMigrationTest.kt), and tgsbMigrateStoredPackOverrides in the iOS
app and in the iOS system voice extension, which cannot be built or run on
the machines these tests run on.  What can be held here, by reading the
sources: all three name exactly the settings the frontend started to read,
the two Swift copies are the same text, and each is called where stored
overrides are about to be used.

The second half holds the reason itself on the frontend: a stored 35 is heard
now, at every rate.
"""
from __future__ import annotations

import ctypes
import os
import pathlib
import re
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
_ARCH = "x86" if sys.maxsize <= 2 ** 32 else "x64"
os.environ.setdefault("TGSB_LIB_DIR", str(REPO / f"build-{_ARCH}-nvda" / "MinSizeRel"))
os.environ.setdefault("TGSB_PACKS_ROOT", str(REPO))
sys.path.insert(0, str(REPO / "python"))

from tgspeechbox.engine import Frontend  # noqa: E402

ANDROID = REPO / "src" / "platforms" / "android" / "app" / "src" / "main" / "kotlin" / "com" / "tgspeechbox" / "tts"
IOS_APP = REPO / "src" / "platforms" / "ios" / "host-app" / "TgsbEngine.swift"
IOS_VOICE = REPO / "src" / "platforms" / "ios" / "au-extension" / "TGSBAudioUnit.swift"
SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]


def _text(path):
    return path.read_text(encoding="utf-8").replace("\r\n", "\n")


def _frontend_timing_keys():
    """The getNum block under "Phoneme timing" in src/frontend/pack.cpp."""
    src = _text(REPO / "src" / "frontend" / "pack.cpp")
    start = src.index("// Phoneme timing (ms at speed=1.0")
    block = src[start:src.index("\n\n", start)]
    keys = re.findall(r'getNum\("(\w+)"', block)
    assert len(keys) == len(set(keys)) == 16, keys
    return set(keys)


def _swift_function(path):
    src = _text(path)
    start = src.index("func tgsbMigrateStoredPackOverrides(")
    return src[start:src.index("\n}\n", start) + 3]


def test_android_names_the_settings_the_frontend_started_to_read():
    src = _text(ANDROID / "PackOverrideMigration.kt")
    start = src.index("TIMING_KEYS_UNREAD_BEFORE_SCHEMA_1: Set<String> = setOf(")
    listed = re.findall(r'"(\w+)"', src[start:src.index(")", start + 60)])
    assert set(listed) == _frontend_timing_keys() and len(listed) == 16


@pytest.mark.parametrize("path", [IOS_APP, IOS_VOICE], ids=["app", "system voice"])
def test_ios_names_the_same_settings(path):
    body = _swift_function(path)
    start = body.index("let unheard: Set<String> = [")
    listed = re.findall(r'"(\w+)"', body[start:body.index("]", start)])
    assert set(listed) == _frontend_timing_keys() and len(listed) == 16


def test_the_two_ios_copies_are_one_text():
    assert _swift_function(IOS_APP) == _swift_function(IOS_VOICE)


def test_each_app_runs_it_before_stored_overrides_are_used():
    # Android: every reader of the settings goes through TgsbStorage.prefs(),
    # and the service leaves the unheard values out until the clean-up ran.
    storage = _text(ANDROID / "TgsbStorage.kt")
    assert "PackOverrideMigration.run(prefs)" in storage[storage.index("fun prefs(ctx: Context)"):]
    service = _text(ANDROID / "TgsbTtsService.kt")
    apply = service[service.index("private fun applyStoredOverrides("):]
    apply = apply[:apply.index("\n    }\n")]
    assert "PackOverrideMigration.withoutUnheardTimingKeys(stored)" in apply
    # iOS: the app at start-up, the system voice before it reads the overrides.
    app = _text(IOS_APP)
    init = app[app.index("    init() {"):]
    assert init.index("tgsbMigrateStoredPackOverrides(UserDefaults(suiteName: kAppGroupId))") < init.index("self.sampleRate")
    voice = _text(IOS_VOICE)
    apply = voice[voice.index("private func applyStoredOverrides("):]
    assert apply.index("tgsbMigrateStoredPackOverrides(d)") < apply.index('d?.string(forKey: "pack_overrides_')


# ---------------------------------------------------------------------------
# Why: a stored value for one of these is heard now
# ---------------------------------------------------------------------------

def _aspiration_ms(fe, speed):
    frames = fe.frames("pˈæt tˈɑːp kˈɪt", speed=speed)
    trace = fe.frame_trace()
    starts = [e["frameIndex"] for e in trace] + [len(frames)]
    return sum(f.duration_ms for e, a, b in zip(trace, starts, starts[1:]) if e["phonemeKey"] == "h"
               for f in frames[a:b])


@pytest.mark.parametrize("speed", SPEEDS)
def test_a_stored_35_ms_aspiration_is_heard_since_172(speed):
    """The call the apps make for each stored override.  Before #172 it
    changed nothing, which is why nobody noticed the line in their files."""
    with Frontend("en-us") as plain, Frontend("en-us") as stored:
        set_data = stored._lib.nvspFrontend_setData
        set_data.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
        set_data.restype = ctypes.c_int
        SETTINGS = 0  # NVSP_DATA_SETTINGS
        assert set_data(stored._h, SETTINGS, b"en-us", b"postStopAspirationDurationMs", b"35")
        shipped, with_override = _aspiration_ms(plain, speed), _aspiration_ms(stored, speed)
    assert shipped > 0
    assert with_override == pytest.approx(shipped * 35.0 / 20.0, rel=0.02), f"@{speed}: {shipped} ms, {with_override} ms"
