"""The engine from Python: the frontend (IPA to frames) and the DSP (frames
to audio), the same two libraries every TGSpeechBox platform runs.

Text to IPA is not part of this package: TGSpeechBox uses eSpeak NG for that,
which is GPL, and this package is MIT and ships none of it.  Pass IPA, or get
it from an eSpeak you have (``espeak-ng -q --ipa -v en-us "hello"``).
"""
from __future__ import annotations

import array
import ctypes
import json
import wave
from dataclasses import dataclass, field
from typing import Dict, List, Optional

from . import _native
from ._structs import (VOICINGTONE_MAGIC, VOICINGTONE_VERSION, Frame, FrameEx, FrameExCallback,
                       FrontendVoicingTone, VoicingTone)

_DATA_FRAMETRACE = 3
_DATA_PASSTRACE = 4


@dataclass
class QueuedFrame:
    """One frame as the frontend emits it (None params = a silence frame)."""
    params: Optional[Dict[str, float]]
    ex: Optional[Dict[str, float]]
    duration_ms: float
    fade_ms: float
    user_index: int

    @property
    def is_silence(self) -> bool:
        return self.params is None


@dataclass
class Utterance:
    """What render() made: the audio, the frames behind it, and the traces."""
    samples: array.array  # int16 PCM, mono
    sample_rate: int
    frames: List[QueuedFrame]
    frame_trace: List[dict] = field(default_factory=list)
    pass_trace: List[dict] = field(default_factory=list)

    @property
    def duration_s(self) -> float:
        return len(self.samples) / float(self.sample_rate)

    def write_wav(self, path: str) -> None:
        with wave.open(path, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(self.sample_rate)
            w.writeframes(self.samples.tobytes())


class Frontend:
    """IPA to frames, for one language (and optionally a voice profile)."""

    def __init__(self, lang: str = "en-us", packs_root: Optional[str] = None):
        self._lib = _native.frontend()
        root = packs_root or _native.packs_root()
        self._h = self._lib.nvspFrontend_create(root.encode("utf-8"))
        if not self._h:
            raise RuntimeError("TGSpeechBox: could not create the frontend for %s" % root)
        self.set_language(lang)

    def close(self) -> None:
        if self._h:
            self._lib.nvspFrontend_destroy(self._h)
            self._h = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def _error(self) -> str:
        e = self._lib.nvspFrontend_getLastError(self._h)
        return e.decode("utf-8", "replace") if e else "unknown error"

    def set_language(self, lang: str) -> None:
        if not self._lib.nvspFrontend_setLanguage(self._h, lang.encode("utf-8")):
            raise ValueError("TGSpeechBox: language %r: %s" % (lang, self._error()))
        self.language = lang

    def voice_profiles(self) -> List[str]:
        raw = self._lib.nvspFrontend_getVoiceProfileNames(self._h) or b""
        return [n for n in raw.decode("utf-8").splitlines() if n.strip()]

    def set_voice_profile(self, name: str) -> None:
        """A voice profile from phonemes.yaml ("Beth"), or "" for none."""
        if not self._lib.nvspFrontend_setVoiceProfile(self._h, name.encode("utf-8")):
            raise ValueError("TGSpeechBox: voice profile %r: %s" % (name, self._error()))

    def voicing_tone(self) -> Optional[Dict[str, float]]:
        """The active profile's voice source, or None when it has none."""
        t = FrontendVoicingTone()
        return t.to_dict() if self._lib.nvspFrontend_getVoicingTone(self._h, ctypes.byref(t)) else None

    def prepare_text(self, text: str) -> str:
        """The frontend's text pass (dictionaries, letter names, numbers)
        before phonemization."""
        p = self._lib.nvspFrontend_prepareText(self._h, text.encode("utf-8"))
        if not p:
            return text
        try:
            return ctypes.string_at(p).decode("utf-8")
        finally:
            self._lib.nvspFrontend_freeString(p)

    def frames(self, ipa: str, speed: float = 1.0, pitch: float = 110.0,
               inflection: float = 0.5, clause: str = ".") -> List[QueuedFrame]:
        """The frames the frontend emits for one clause of IPA.  Starts a new
        utterance, as a platform does for each request."""
        out: List[QueuedFrame] = []

        def cb(_ud, frame, frame_ex, dur, fade, idx):
            params = frame.contents.to_dict() if frame else None
            ex = frame_ex.contents.to_dict() if (frame and frame_ex) else None
            out.append(QueuedFrame(params, ex, dur, fade, idx))

        c_cb = FrameExCallback(cb)
        self._lib.nvspFrontend_beginStream(self._h)
        ok = self._lib.nvspFrontend_queueIPA_Ex(self._h, ipa.encode("utf-8"), speed, pitch, inflection,
                                               clause.encode("utf-8"), 0, c_cb, None)
        if not ok:
            raise ValueError("TGSpeechBox: IPA %r: %s" % (ipa, self._error()))
        return out

    def _query(self, what: int) -> list:
        p = self._lib.nvspFrontend_queryData(self._h, what, b"", 0, 0)
        if not p:
            return []
        try:
            return json.loads(ctypes.string_at(p).decode("utf-8", "replace"))
        finally:
            self._lib.nvspFrontend_freeString(p)

    def frame_trace(self) -> List[dict]:
        """For the last frames() call: which frame each phoneme starts at."""
        return self._query(_DATA_FRAMETRACE)

    def pass_trace(self) -> List[dict]:
        """For the last frames() call: every pass's view of every token
        (formants and amplitudes as each pass left them)."""
        return self._query(_DATA_PASSTRACE)


class Player:
    """Frames to audio: the DSP."""

    def __init__(self, sample_rate: int = 22050):
        self._lib = _native.dsp()
        self.sample_rate = int(sample_rate)
        self._h = self._lib.speechPlayer_initialize(self.sample_rate)
        if not self._h:
            raise RuntimeError("TGSpeechBox: could not start the DSP")

    def close(self) -> None:
        if self._h:
            self._lib.speechPlayer_terminate(self._h)
            self._h = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    @staticmethod
    def _header(t: VoicingTone) -> VoicingTone:
        # The DSP reads and writes the struct only with a valid header.
        t.magic = VOICINGTONE_MAGIC
        t.structSize = ctypes.sizeof(VoicingTone)
        t.structVersion = VOICINGTONE_VERSION
        return t

    def voicing_tone(self) -> VoicingTone:
        t = self._header(VoicingTone())
        self._lib.speechPlayer_getVoicingTone(self._h, ctypes.byref(t))
        return t

    def set_voicing_tone(self, **values: float) -> None:
        """Change voice-source settings by name (voicedTiltDbPerOct=-4, ...);
        the rest keep their current values."""
        t = self.voicing_tone()
        for k, v in values.items():
            if not any(k == name for name, _ in VoicingTone._fields_[4:]):
                raise KeyError("TGSpeechBox: no voicing tone setting %r" % k)
            setattr(t, k, float(v))
        self._lib.speechPlayer_setVoicingTone(self._h, ctypes.byref(self._header(t)))

    def queue(self, frames: List[QueuedFrame]) -> None:
        ms = self.sample_rate / 1000.0
        for f in frames:
            dur = max(1, int(round(f.duration_ms * ms)))
            fade = max(1, int(round(f.fade_ms * ms)))
            if f.params is None:
                self._lib.speechPlayer_queueFrameEx(self._h, None, None, 0, dur, fade, f.user_index, 0)
                continue
            fr = Frame(**f.params)
            ex = FrameEx(**f.ex) if f.ex else None
            self._lib.speechPlayer_queueFrameEx(self._h, ctypes.byref(fr), ctypes.byref(ex) if ex else None,
                                                ctypes.sizeof(FrameEx) if ex else 0, dur, fade, f.user_index, 0)

    def synthesize(self) -> array.array:
        """Everything queued, as int16 samples."""
        out = array.array("h")
        buf = (ctypes.c_short * 4096)()
        while True:
            n = self._lib.speechPlayer_synthesize(self._h, 4096, buf)
            if n <= 0:
                break
            out.extend(buf[:n])
        return out


def render(ipa: str, lang: str = "en-us", *, voice: str = "", speed: float = 1.0,
           pitch: float = 110.0, inflection: float = 0.5, clause: str = ".",
           sample_rate: int = 22050, packs_root: Optional[str] = None) -> Utterance:
    """IPA to audio in one call, with the frames and traces behind it.

    >>> u = render("həˈloʊ wˈɜːld")
    >>> u.write_wav("hello.wav")
    """
    with Frontend(lang, packs_root) as fe:
        if voice:
            fe.set_voice_profile(voice)
        frames = fe.frames(ipa, speed, pitch, inflection, clause)
        tone = fe.voicing_tone() if voice else None
        ftrace, ptrace = fe.frame_trace(), fe.pass_trace()
    with Player(sample_rate) as pl:
        if tone:
            pl.set_voicing_tone(**tone)
        pl.queue(frames)
        samples = pl.synthesize()
    return Utterance(samples, sample_rate, frames, ftrace, ptrace)
