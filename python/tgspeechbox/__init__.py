"""TGSpeechBox's formant speech engine from Python.

The same DSP and frontend the NVDA add-on, SAPI, Android, iOS and Linux run:
IPA in, frames and audio out.  See engine.render(), Frontend and Player.

MIT licensed.  Contains no eSpeak NG: pass IPA, or phonemize with an
eSpeak you have.
"""
from ._version import __version__
from .engine import Frontend, Player, QueuedFrame, Utterance, render

__all__ = ["Frontend", "Player", "QueuedFrame", "Utterance", "render", "__version__", "dsp_version"]


def dsp_version() -> int:
    from . import _native
    return int(_native.dsp().speechPlayer_getDspVersion())
