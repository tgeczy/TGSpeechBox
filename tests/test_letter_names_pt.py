"""Portuguese letter names say the letter's own vowel (#130, #123).

packs/dict/pt-letters.tsv names the accented letters in text, and eSpeak
phonemizes that text, so a name is only right if eSpeak reads its first word
with the letter's vowel.  eSpeak pt reads a bare "e" as [i] and a bare "o" as
[u], which is how "e circunflexo" came out as "i circunflexo" and "o til" as
"u til" (Edu's list on #130).  eSpeak's own name for "ê" is no help either: it
says "é circunflexo", open.

Checked against every eSpeak a TGSpeechBox build uses on this machine:

* the one we ship (the SAPI engine, Android, iOS): eSpeak NG's command-line
  build at ESPEAK_EXE, reading the repo's resources/espeak-ng-data;
* NVDA's own, which the NVDA add-on phonemizes with: NVDA_DIR's espeak.dll.

Each is skipped when it is not there.
"""
from __future__ import annotations

import ctypes
import os
import pathlib
import subprocess
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
LETTERS = REPO / "packs" / "dict" / "pt-letters.tsv"
ESPEAK_EXE = pathlib.Path(os.environ.get(
    "TGSB_ESPEAK_EXE", r"C:\git\espeak-ng\build-x64\src\Release\espeak-ng.exe"))
NVDA_DIR = pathlib.Path(os.environ.get("TGSB_NVDA_DIR", r"C:\Program Files\NVDA"))

# The vowel each letter's name has to start with.
EXPECTED = {
    "pt-br": {
        "á": "a", "é": "ɛ", "í": "i", "ó": "ɔ", "ú": "u",
        "â": "a", "ê": "e", "ô": "o",
        "ã": "a", "õ": "ɔ",
        "à": "a", "ò": "ɔ", "ù": "u",
    },
    # European Portuguese: the ones that differ from pt-br by more than the
    # quality of a (which is its own question) are the ones pinned.
    "pt": {
        "é": "ɛ", "í": "i", "ó": "ɔ", "ú": "u",
        "ê": "e", "ô": "o", "õ": "ɔ", "ò": "ɔ", "ù": "u",
    },
}

IPA_VOWELS = set("aɐɑæeɛəɘɜiɨɪoɔɵœøuʉʊʏyɯɤ")


def letter_names() -> dict:
    names = {}
    for line in LETTERS.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#") or "\t" not in line:
            continue
        letter, name = line.split("\t", 1)
        names[letter] = name.strip()
    return names


def first_vowel(ipa: str) -> str:
    for ch in ipa:
        if ch in IPA_VOWELS:
            return ch
    return ""


class CliEspeak:
    """The eSpeak we ship: its CLI over the repo's compiled data."""

    label = "shipped"

    def __init__(self):
        if not ESPEAK_EXE.is_file():
            pytest.skip(f"no eSpeak NG command-line build at {ESPEAK_EXE}")
        self.data = REPO / "resources"

    def ipa(self, voice: str, text: str) -> str:
        out = subprocess.run(
            [str(ESPEAK_EXE), f"--path={self.data}", "-v", voice, "--ipa", "-q", text],
            capture_output=True, text=True, encoding="utf-8", check=True)
        return out.stdout.strip()


class NvdaEspeak:
    """NVDA's espeak.dll with NVDA's own data, as the add-on uses it."""

    label = "nvda"
    _dll = None

    def __init__(self):
        synth = NVDA_DIR / "synthDrivers"
        dll = synth / "espeak.dll"
        if not dll.is_file():
            pytest.skip(f"no NVDA eSpeak at {dll}")
        if NvdaEspeak._dll is None:
            try:
                lib = ctypes.CDLL(str(dll))
            except OSError as e:  # wrong bitness for this Python, usually
                pytest.skip(f"cannot load {dll} into this Python: {e}")
            lib.espeak_Initialize.restype = ctypes.c_int
            lib.espeak_Initialize.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
            lib.espeak_SetVoiceByName.argtypes = [ctypes.c_char_p]
            lib.espeak_TextToPhonemes.restype = ctypes.c_char_p
            lib.espeak_TextToPhonemes.argtypes = [
                ctypes.POINTER(ctypes.c_void_p), ctypes.c_int, ctypes.c_int]
            AUDIO_OUTPUT_SYNCHRONOUS = 2
            if lib.espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, str(synth).encode("mbcs"), 0) <= 0:
                pytest.skip("NVDA eSpeak did not initialize")
            NvdaEspeak._dll = lib
        self.lib = NvdaEspeak._dll

    def ipa(self, voice: str, text: str) -> str:
        assert self.lib.espeak_SetVoiceByName(voice.encode()) == 0
        buf = ctypes.create_string_buffer(text.encode("utf-8"))
        ptr = ctypes.c_void_p(ctypes.addressof(buf))
        parts = []
        espeakCHARS_UTF8, IPA = 1, 0x02
        while ptr.value:
            out = self.lib.espeak_TextToPhonemes(ctypes.byref(ptr), espeakCHARS_UTF8, IPA)
            if out:
                parts.append(out.decode("utf-8"))
        return " ".join(parts)


@pytest.fixture(params=[CliEspeak, NvdaEspeak], ids=["shipped", "nvda"])
def espeak(request):
    return request.param()


CASES = [(voice, letter, vowel) for voice, table in EXPECTED.items() for letter, vowel in table.items()]


@pytest.mark.parametrize("voice,letter,vowel", CASES, ids=[f"{v}-{l}" for v, l, _ in CASES])
def test_letter_name_says_its_vowel(espeak, voice, letter, vowel):
    names = letter_names()
    assert letter in names, f"{letter} has no name in {LETTERS.name}"
    ipa = espeak.ipa(voice, names[letter])
    assert first_vowel(ipa) == vowel, (
        f"{espeak.label} eSpeak {voice} reads {letter} ({names[letter]!r}) as [{ipa}]; "
        f"its name should start with [{vowel}]")
