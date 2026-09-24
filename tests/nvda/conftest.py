"""Enough of NVDA to import and drive the TGSpeechBox synth driver outside NVDA.

Modelled on panthera-speech's driver harness.  The real driver runs here: its
own threads, the built nvspFrontend.dll and speechPlayer.dll, the repo's
packs, and NVDA's own espeak.dll with its data, which is what the add-on
phonemizes with inside NVDA.  Only NVDA itself is faked, and only in the ways
the driver can tell:

* ``WavePlayer.feed()`` records what it was given and when; its ``onDone``
  callbacks fire once that chunk has "played", at real-time pace scaled by
  ``PLAYBACK_SPEED``, from a later ``feed()`` or from ``idle()``.
* ``idle()`` blocks until the audio has drained and ``stop()`` cuts it short,
  as the real player does.  A non-blocking ``idle()`` would hide ordering
  bugs between utterances.
* ``synthDoneSpeaking`` and ``synthIndexReached`` can be waited on, because
  NVDA paces speech on them.

The add-on is staged the way the release script packages it (the driver's
.py files, speechPlayer.py, packs/, x64/ DLLs) under
build-x64-nvda/nvda-test, and ``synthDrivers`` is registered as a package
rooted there, as NVDA's addonHandler does, so relative imports are exercised
for real.

Needs 64-bit Python (NVDA 2026 is x64), build-x64-nvda/MinSizeRel's DLLs,
and an NVDA install (TGSB_NVDA_DIR, default C:\\Program Files\\NVDA).  The
whole directory is skipped when any of them is missing.
"""
from __future__ import annotations

import os
import pathlib
import shutil
import sys
import threading
import time
import types

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
ADDON_SRC = REPO / "nvdaAddon" / "synthDrivers" / "tgSpeechBox"
DLL_DIR = REPO / "build-x64-nvda" / "MinSizeRel"
STAGE_ROOT = REPO / "build-x64-nvda" / "nvda-test"
STAGE = STAGE_ROOT / "synthDrivers" / "tgSpeechBox"
NVDA_DIR = pathlib.Path(os.environ.get("TGSB_NVDA_DIR", r"C:\Program Files\NVDA"))

#: How much faster than real time the fake player "plays".  1.0 is real time;
#: tests about pacing between utterances do not need to wait it out in full.
PLAYBACK_SPEED = float(os.environ.get("TGSB_PLAYBACK_SPEED", "8"))


def _missing() -> str:
    if sys.maxsize <= 2 ** 32:
        return "needs 64-bit Python (NVDA 2026 is x64)"
    for name in ("nvspFrontend.dll", "speechPlayer.dll"):
        if not (DLL_DIR / name).is_file():
            return f"{DLL_DIR / name} not built (cmake --build build-x64-nvda --config MinSizeRel)"
    if not (NVDA_DIR / "synthDrivers" / "espeak.dll").is_file():
        return f"no NVDA eSpeak under {NVDA_DIR} (set TGSB_NVDA_DIR)"
    return ""


def pytest_collection_modifyitems(config, items):
    reason = _missing()
    if not reason:
        return
    here = pathlib.Path(__file__).parent
    for item in items:
        if here in pathlib.Path(str(item.fspath)).parents:
            item.add_marker(pytest.mark.skip(reason=reason))


# ---------------------------------------------------------------------------
# Staging
# ---------------------------------------------------------------------------

def _copy_if_different(src: pathlib.Path, dst: pathlib.Path) -> None:
    import filecmp
    if dst.exists() and filecmp.cmp(src, dst, shallow=False):
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


_staged = False


def _stage_addon() -> None:
    """Lay the add-on out as scripts/package does, from this tree's sources
    and the DLLs tests/conftest.py has just built.  Once per session; files
    are compared by content, so what is staged is always what is in the tree."""
    global _staged
    if _staged:
        return
    STAGE.mkdir(parents=True, exist_ok=True)
    for stale in STAGE.glob("*.py"):
        if not (ADDON_SRC / stale.name).exists() and stale.name != "speechPlayer.py":
            stale.unlink()  # a module deleted from the tree must not linger here
    for py in ADDON_SRC.glob("*.py"):
        _copy_if_different(py, STAGE / py.name)
    _copy_if_different(REPO / "speechPlayer.py", STAGE / "speechPlayer.py")
    for name in ("nvspFrontend.dll", "speechPlayer.dll"):
        _copy_if_different(DLL_DIR / name, STAGE / "x64" / name)
    # packs/ fresh every session: the driver writes language settings back
    # into its own packs, and a test must never see another test's writes.
    packs = STAGE / "packs"
    if packs.exists():
        shutil.rmtree(packs)
    shutil.copytree(REPO / "packs", packs)
    (packs / ".defaults").mkdir(exist_ok=True)
    for y in (REPO / "packs" / "lang").glob("*.yaml"):
        shutil.copy2(y, packs / ".defaults" / y.name)
    _staged = True


# ---------------------------------------------------------------------------
# The fake NVDA
# ---------------------------------------------------------------------------

class FakeWavePlayer:
    """NVDA's WavePlayer, in the ways the driver can tell.

    Every instance registers itself in ``FakeWavePlayer.instances`` so a test
    can read what the driver fed.  ``feeds`` holds (time, bytes) per call.
    """

    instances: list = []

    def __init__(self, channels=1, samplesPerSec=22050, bitsPerSample=16, outputDevice=None, purpose=None, **_):
        self.rate = samplesPerSec
        self.bytesPerSecond = samplesPerSec * channels * bitsPerSample // 8
        self.feeds: list = []  # (perf_counter, bytes)
        self.stops = 0
        self._lock = threading.Lock()
        self._until = 0.0
        self._callbacks: list = []  # (time the chunk ends, callback)
        FakeWavePlayer.instances.append(self)

    def feed(self, data, size=None, onDone=None):
        data = bytes(data[:size] if size is not None else data)
        with self._lock:
            now = time.perf_counter()
            self.feeds.append((now, data))
            start = max(self._until, now)
            self._until = start + len(data) / self.bytesPerSecond / PLAYBACK_SPEED
            if onDone is not None:
                self._callbacks.append((self._until, onDone))
        self._fire()

    def _fire(self, everything=False):
        due = []
        with self._lock:
            now = time.perf_counter()
            while self._callbacks and (everything or self._callbacks[0][0] <= now):
                due.append(self._callbacks.pop(0)[1])
        for cb in due:
            cb()

    def stop(self):
        with self._lock:
            self.stops += 1
            self._until = 0.0
            del self._callbacks[:]

    def idle(self):
        while True:
            self._fire()
            with self._lock:
                left = self._until - time.perf_counter()
            if left <= 0:
                self._fire(everything=True)
                return
            time.sleep(min(left, 0.002))

    sync = idle

    def pause(self, switch):
        pass

    def close(self):
        self.stop()


class _Notifier:
    """Counts notifications; a test can wait for the next one."""

    def __init__(self):
        self.count = 0
        self.last = {}
        self._cond = threading.Condition()

    def notify(self, **kw):
        with self._cond:
            self.count += 1
            self.last = kw
            self._cond.notify_all()

    def wait_for_count(self, n, timeout=10.0):
        end = time.time() + timeout
        with self._cond:
            while self.count < n:
                left = end - time.time()
                if left <= 0:
                    return False
                self._cond.wait(left)
        return True


class _Log:
    def __init__(self):
        self.messages = []

    def _rec(self, level, msg, *a):
        try:
            self.messages.append((level, msg % a if a else str(msg)))
        except Exception:
            self.messages.append((level, str(msg)))

    def info(self, m, *a, **k): self._rec("info", m, *a)
    def debug(self, m, *a, **k): self._rec("debug", m, *a)
    def debugWarning(self, m, *a, **k): self._rec("debug", m, *a)
    def warning(self, m, *a, **k): self._rec("warning", m, *a)
    def error(self, m, *a, **k): self._rec("error", m, *a)
    def exception(self, m, *a, **k): self._rec("error", m, *a)
    DEBUG = 10
    def isEnabledFor(self, level): return False


class _Section(dict):
    """config.conf sections: a dict that also has NVDA's `_cache`."""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self._cache = {}


class _Conf(dict):
    def __init__(self):
        super().__init__(audio=_Section(outputDevice="default"),
                         speech=_Section(outputDevice="default", autoLanguageSwitching=True,
                                         autoDialectSwitching=False, trustVoiceLanguage=True))
        self.profiles = [{}]

    def save(self):
        pass


class _AutoPropertyType(type):
    """NVDA's baseObject.AutoPropertyType where it bites: `_get_x`/`_set_x`
    become a property `x`, found through the bases as well."""

    def __init__(cls, name, bases, namespace, **kw):
        super().__init__(name, bases, namespace, **kw)
        names = set()
        for klass in cls.__mro__:
            names |= {k[5:] for k in vars(klass) if k[:5] in ("_get_", "_set_")}
        for attr in names:
            if isinstance(getattr(cls, attr, None), property) or attr in namespace:
                continue
            getter = getattr(cls, "_get_" + attr, None)
            setter = getattr(cls, "_set_" + attr, None)
            setattr(cls, attr, property(getter, setter))


def _install_fake_nvda() -> None:
    if "synthDriverHandler" in sys.modules:
        return

    nvwave = types.ModuleType("nvwave")
    nvwave.WavePlayer = FakeWavePlayer
    nvwave.AudioPurpose = type("AudioPurpose", (), {"SPEECH": 1})
    sys.modules["nvwave"] = nvwave

    logh = types.ModuleType("logHandler")
    logh.log = _Log()
    sys.modules["logHandler"] = logh

    cfg = types.ModuleType("config")
    cfg.conf = _Conf()
    sys.modules["config"] = cfg

    gv = types.ModuleType("globalVars")
    gv.appDir = str(NVDA_DIR)
    gv.appArgs = types.SimpleNamespace(configPath=str(STAGE_ROOT / "config"), secure=False)
    sys.modules["globalVars"] = gv

    addonHandler = types.ModuleType("addonHandler")
    addonHandler.initTranslation = lambda: None
    sys.modules["addonHandler"] = addonHandler

    core = types.ModuleType("core")
    core.callLater = lambda ms, fn, *a, **k: None  # the GUI loop never runs here
    sys.modules["core"] = core

    languageHandler = types.ModuleType("languageHandler")
    # NVDA's interface language and the Windows locale; a test sets these
    # before it sets the driver's language to "auto".
    languageHandler.uiLanguage = "en"
    languageHandler.windowsLanguage = "en_US"
    languageHandler.getLanguage = lambda: languageHandler.uiLanguage
    languageHandler.getWindowsLanguage = lambda: languageHandler.windowsLanguage
    sys.modules["languageHandler"] = languageHandler

    speech = types.ModuleType("speech")
    commands = types.ModuleType("speech.commands")

    class IndexCommand:
        def __init__(self, index): self.index = index
        def __repr__(self): return f"IndexCommand({self.index})"

    class PitchCommand:
        def __init__(self, offset=0, multiplier=1):
            self.offset, self.multiplier = offset, multiplier
            self.isDefault = offset == 0 and multiplier == 1

    class LangChangeCommand:
        """NVDA's: `lang` is an NVDA code ("es", "pt_BR") or None for the
        synthesizer's default language."""
        def __init__(self, lang): self.lang = lang
        def __repr__(self): return f"LangChangeCommand({self.lang!r})"

    commands.IndexCommand = IndexCommand
    commands.PitchCommand = PitchCommand
    commands.LangChangeCommand = LangChangeCommand
    speech.commands = commands
    sys.modules["speech"] = speech
    sys.modules["speech.commands"] = commands

    class _Setting:
        def __init__(self, *a, **k):
            self.id = a[0] if a else k.get("id")
            self.displayName = a[1] if len(a) > 1 else k.get("displayName")
            self.defaultVal = k.get("defaultVal")

    def _builtin(settingId, default=None):
        class _Builtin(_Setting):
            def __init__(self, *a, **k):
                super().__init__(settingId, **k)
                if self.defaultVal is None:
                    self.defaultVal = default
        return _Builtin

    class SynthDriver(metaclass=_AutoPropertyType):
        VoiceSetting = _builtin("voice")
        LanguageSetting = _builtin("language")
        RateSetting = _builtin("rate", 50)
        RateBoostSetting = _builtin("rateBoost", False)
        PitchSetting = _builtin("pitch", 50)
        InflectionSetting = _builtin("inflection", 80)
        VolumeSetting = _builtin("volume", 50)

        def __init__(self):
            pass

    class VoiceInfo:
        def __init__(self, id, displayName, language=None):
            self.id, self.displayName, self.language = id, displayName, language

    sdh = types.ModuleType("synthDriverHandler")
    sdh.SynthDriver = SynthDriver
    sdh.VoiceInfo = VoiceInfo
    sdh.synthDoneSpeaking = _Notifier()
    sdh.synthIndexReached = _Notifier()
    sys.modules["synthDriverHandler"] = sdh

    asu = types.ModuleType("autoSettingsUtils")
    ds = types.ModuleType("autoSettingsUtils.driverSetting")
    ds.DriverSetting = _Setting
    ds.BooleanDriverSetting = type("BooleanDriverSetting", (_Setting,), {})
    ds.NumericDriverSetting = type("NumericDriverSetting", (_Setting,), {})
    asu.driverSetting = ds
    utils = types.ModuleType("autoSettingsUtils.utils")
    utils.StringParameterInfo = type(
        "StringParameterInfo", (), {"__init__": lambda self, id, displayName: (setattr(self, "id", id), setattr(self, "displayName", displayName), None)[-1]})
    asu.utils = utils
    sys.modules["autoSettingsUtils"] = asu
    sys.modules["autoSettingsUtils.driverSetting"] = ds
    sys.modules["autoSettingsUtils.utils"] = utils

    import builtins
    if not hasattr(builtins, "_"):
        builtins._ = lambda s: s

    pkg = types.ModuleType("synthDrivers")
    pkg.__path__ = [str(STAGE.parent)]
    sys.modules["synthDrivers"] = pkg


if not _missing():
    # The fakes go in at import; the add-on is staged when the first test
    # needs it, after tests/conftest.py has built the DLLs.
    _install_fake_nvda()


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

class Harness:
    """The driver plus what NVDA would observe of it."""

    def __init__(self, driver):
        import synthDriverHandler
        self.driver = driver
        self.done = synthDriverHandler.synthDoneSpeaking
        self.index = synthDriverHandler.synthIndexReached

    @property
    def player(self) -> FakeWavePlayer:
        for _ in range(500):  # the audio thread creates it on start
            if FakeWavePlayer.instances:
                return FakeWavePlayer.instances[-1]
            time.sleep(0.01)
        raise AssertionError("the driver never opened a WavePlayer")

    def speak(self, sequence, timeout=15.0):
        """Speak and wait for synthDoneSpeaking.  Returns (pcm, first-feed
        latency in seconds, the feeds of this utterance)."""
        player = self.player
        before_feeds = len(player.feeds)
        before_done = self.done.count
        t0 = time.perf_counter()
        self.driver.speak(list(sequence))
        assert self.done.wait_for_count(before_done + 1, timeout), f"no synthDoneSpeaking for {sequence!r}"
        feeds = player.feeds[before_feeds:]
        pcm = b"".join(d for _, d in feeds)
        first = next((t for t, d in feeds if d), None)
        latency = (first - t0) if first is not None else None
        return pcm, latency, feeds


def nvda_sequence(driver, items, doc_lang=None, auto_dialect_switching=False):
    """The speech sequence NVDA hands the synth for `items` read from text
    whose language is `doc_lang`, with automatic language switching on.

    speech.speak() (source/speech/speech.py): the default language is the
    synth's own `language`, normalized ("pt-br" -> "pt_BR", "auto" stays
    "auto"); text in the default language's root, with dialect switching off,
    counts as the default; each run of text gets a LangChangeCommand when its
    language differs from the previous run's.
    """
    from speech.commands import LangChangeCommand

    def normalize(lang):
        ld = lang.replace("-", "_").split("_")
        ld[0] = ld[0].lower()
        if len(ld) >= 2:
            ld[1] = ld[1].upper()
        return "_".join(ld)

    default = normalize(driver.language or "en")
    cur = doc_lang or default
    if not auto_dialect_switching and cur.split("_")[0] == default.split("_")[0]:
        cur = default
    out, prev = [], None
    for item in items:
        if isinstance(item, str):
            if not item:
                continue
            if cur != prev:
                out.append(LangChangeCommand(cur))
                prev = cur
        out.append(item)
    return out


def record_frames(driver):
    """Record every frame the driver queues on the DSP, NaN-safe, silence
    frames included, so two utterances can be compared as the synth sees
    them."""
    import math
    log = []
    p = driver._player
    oq, oqx = p.queueFrame, p.queueFrameEx

    def norm(v):
        return None if isinstance(v, float) and math.isnan(v) else (round(v, 4) if isinstance(v, float) else v)

    def dump(s):
        return None if s is None else tuple(norm(getattr(s, f)) for f, _ in s._fields_)

    def q(frame, minD, fadeD, userIndex=-1, purgeQueue=False):
        if not purgeQueue:  # a cancel's purge is not part of the utterance
            log.append((dump(frame), None, round(minD, 3), round(fadeD, 3)))
        return oq(frame, minD, fadeD, userIndex, purgeQueue)

    def qx(frame, frameEx, minD, fadeD, *a, **k):
        log.append((dump(frame), dump(frameEx), round(minD, 3), round(fadeD, 3)))
        return oqx(frame, frameEx, minD, fadeD, *a, **k)

    p.queueFrame, p.queueFrameEx = q, qx
    return log


@pytest.fixture
def harness():
    _stage_addon()
    from synthDrivers import tgSpeechBox
    FakeWavePlayer.instances.clear()
    driver = tgSpeechBox.SynthDriver()
    h = Harness(driver)
    try:
        yield h
    finally:
        driver.terminate()


@pytest.fixture(name="nvda_sequence")
def _nvda_sequence_fixture():
    return nvda_sequence


@pytest.fixture(name="record_frames")
def _record_frames_fixture():
    return record_frames
