"""Canadian English: "my", "right" and "house" are one gliding vowel, and
Canadian Raising happens (#188).

en-ca's PRICE and MOUTH rules sat in ``replacements``, after en.yaml's
a -> æ, so they never ran: "my" was æ + ɪ and "house" æ + ʊ̞, two vowels of
84 ms each, and nothing was raised.  They are in ``preReplacements`` now,
the way en-us keeps its PRICE and MOUTH.

Canadian Raising (Joos 1942, Language 18: 141-144; Chambers 1973, Canadian
Journal of Linguistics 18: 113-135): before a voiceless consonant inside the
word the diphthong starts higher, [ʌɪ] and [ʌʊ]; elsewhere it starts low.
"writer" (a flapped /t/) is raised and "rider" is not; stress on the next
syllable can stop it ("citation" beside "cite").  Labov, Ash & Boberg (2006,
The Atlas of North American English) count a speaker as raising from 60 Hz
of F1 between the two contexts; Hagiwara (2005, Journal of the Acoustical
Society of America 117, doi:10.1121/1.4787300) measured about 200 Hz before
/t/ in Winnipeg.  The pack's ʌ is 120 Hz below ɑ and 150 Hz below ä.

The IPA is eSpeak NG's en-us (the voice every host uses for en-ca), with
tie bars as the hosts ask for it and without as its command line writes it.
"""
from __future__ import annotations

import os
import pathlib
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
_ARCH = "x86" if sys.maxsize <= 2 ** 32 else "x64"
os.environ.setdefault("TGSB_LIB_DIR", str(REPO / f"build-{_ARCH}-nvda" / "MinSizeRel"))
os.environ.setdefault("TGSB_PACKS_ROOT", str(REPO))
sys.path.insert(0, str(REPO / "python"))

import tgspeechbox  # noqa: E402

TIE = chr(0x361)
SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]

# word: (IPA, the consonant keys before the vowel under test)
RAISED_PRICE = {"right": "ɹˈa͡ɪt", "ice": "ˈa͡ɪs", "knife": "nˈa͡ɪf", "like": "lˈa͡ɪk", "type": "tˈa͡ɪp",
                "price": "pɹˈa͡ɪs", "white": "wˈa͡ɪt", "cite": "sˈa͡ɪt", "titan": "tˈa͡ɪtən", "icon": "ˈa͡ɪkɑːn",
                "brighten": "bɹˈa͡ɪʔn̩", "microphone": "mˈa͡ɪkɹəfˌo͡ʊn"}
RAISED_MOUTH = {"house": "hˈa͡ʊs", "out": "ˈa͡ʊt", "about": "ɐbˈa͡ʊt", "mouth": "mˈa͡ʊθ", "south": "sˈa͡ʊθ",
                "doubt": "dˈa͡ʊt", "couch": "kˈa͡ʊt͡ʃ"}
LOW_PRICE = {"my": "mˈa͡ɪ", "I": "ˈa͡ɪ", "time": "tˈa͡ɪm", "ride": "ɹˈa͡ɪd", "eyes": "ˈa͡ɪz", "knives": "nˈa͡ɪvz",
             "prize": "pɹˈa͡ɪz", "five": "fˈa͡ɪv", "wide": "wˈa͡ɪd", "nine": "nˈa͡ɪn", "island": "ˈa͡ɪlənd",
             "fire": "fˈa͡ɪ͡ɚ", "higher": "hˈa͡ɪɚ"}
LOW_MOUTH = {"loud": "lˈa͡ʊd", "how": "hˈa͡ʊ", "now": "nˈa͡ʊ", "down": "dˈa͡ʊn", "houses": "hˈa͡ʊzᵻz",
             "power": "pˈa͡ʊɚ", "flour": "flˈa͡ʊɚ"}

VOWEL_KEYS = {"ʌ", "ɑ", "ä", "æ", "ᴐ", "ɪ", "ʊ", "ʊ̞"}
ONSET = {"raised": "ʌ", "price": "ɑ", "mouth": "ä"}


def _spell(ipa, tied):
    return ipa if tied else ipa.replace(TIE, "")


def _render(ipa, lang="en-ca", speed=1.0):
    u = tgspeechbox.render(ipa, lang, speed=speed)
    return u, [e["phonemeKey"] for e in u.frame_trace]


def _first_vowel(ipa, lang="en-ca", speed=1.0):
    """(keys, key of the first vowel phoneme, its frames)."""
    u, keys = _render(ipa, lang, speed)
    tr = u.frame_trace
    n = next(i for i, k in enumerate(keys) if k in VOWEL_KEYS)
    end = tr[n + 1]["frameIndex"] if n + 1 < len(tr) else len(u.frames)
    body = [f.params for f in u.frames[tr[n]["frameIndex"]:end] if f.params]
    return keys, keys[n], body


def _cases(*tables):
    return [(w, ipa) for t in tables for w, ipa in t.items()]


# ── one vowel, not two ──────────────────────────────────────────────

@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("tied", [True, False])
@pytest.mark.parametrize("word,ipa", _cases(RAISED_PRICE, LOW_PRICE))
def test_price_is_one_gliding_vowel(word, ipa, tied, speed):
    keys, onset, body = _first_vowel(_spell(ipa, tied), speed=speed)
    assert "æ" not in keys and "ɪ" not in keys, f"{word} @{speed}: two vowels: {keys}"
    assert onset in ("ʌ", "ɑ"), f"{word} @{speed}: {keys}"
    # It glides to the front: F1 falls and F2 ends where ɪ is.
    assert len(body) >= 3, f"{word} @{speed}: no glide, {len(body)} frame(s)"
    assert body[-1]["cf1"] < body[0]["cf1"] - 150 and body[-1]["cf2"] >= 1800, (
        f"{word} @{speed}: F1 {body[0]['cf1']:.0f}->{body[-1]['cf1']:.0f}, F2 {body[0]['cf2']:.0f}->{body[-1]['cf2']:.0f}")


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("tied", [True, False])
@pytest.mark.parametrize("word,ipa", _cases(RAISED_MOUTH, LOW_MOUTH))
def test_mouth_is_one_gliding_vowel(word, ipa, tied, speed):
    keys, onset, body = _first_vowel(_spell(ipa, tied), speed=speed)
    assert "æ" not in keys and "ʊ̞" not in keys and "ʊ" not in keys, f"{word} @{speed}: two vowels: {keys}"
    assert onset in ("ʌ", "ä"), f"{word} @{speed}: {keys}"
    # It glides back and up: F1 and F2 both fall, to en.yaml's ʊ̞.
    assert len(body) >= 3, f"{word} @{speed}: no glide, {len(body)} frame(s)"
    assert body[-1]["cf1"] < body[0]["cf1"] - 100 and body[-1]["cf2"] <= 1050, (
        f"{word} @{speed}: F1 {body[0]['cf1']:.0f}->{body[-1]['cf1']:.0f}, F2 {body[0]['cf2']:.0f}->{body[-1]['cf2']:.0f}")


# ── Canadian Raising ────────────────────────────────────────────────

@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("tied", [True, False])
@pytest.mark.parametrize("word,ipa", _cases(RAISED_PRICE, RAISED_MOUTH))
def test_raised_before_a_voiceless_consonant(word, ipa, tied, speed):
    keys, onset, _ = _first_vowel(_spell(ipa, tied), speed=speed)
    assert onset == ONSET["raised"], f"{word} @{speed}: {keys}"


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("tied", [True, False])
@pytest.mark.parametrize("word,ipa", _cases(LOW_PRICE))
def test_price_starts_low_elsewhere(word, ipa, tied, speed):
    # ɑ, the onset en-us uses.  Not ᴐ: the COT-CAUGHT rule (ɑ -> ᴐ) must not
    # take PRICE's onset for LOT, or "my" is "moy".
    keys, onset, _ = _first_vowel(_spell(ipa, tied), speed=speed)
    assert onset == ONSET["price"], f"{word} @{speed}: {keys}"


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("tied", [True, False])
@pytest.mark.parametrize("word,ipa", _cases(LOW_MOUTH))
def test_mouth_starts_low_elsewhere(word, ipa, tied, speed):
    keys, onset, _ = _first_vowel(_spell(ipa, tied), speed=speed)
    assert onset == ONSET["mouth"], f"{word} @{speed}: {keys}"


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("raised,low", [("ɹˈa͡ɪt", "ɹˈa͡ɪd"), ("lˈa͡ɪf", "lˈa͡ɪv"), ("nˈa͡ɪf", "nˈa͡ɪvz"),
                                        ("hˈa͡ʊs", "hˈa͡ʊzᵻz"), ("ˈa͡ʊt", "lˈa͡ʊd")])
def test_the_raised_onset_is_higher_by_more_than_60_hz(raised, low, speed):
    # Labov, Ash & Boberg (2006): 60 Hz of F1 between the contexts is raising.
    _, _, r = _first_vowel(raised, speed=speed)
    _, _, lo = _first_vowel(low, speed=speed)
    assert lo[0]["cf1"] - r[0]["cf1"] >= 100, f"@{speed}: F1 {r[0]['cf1']:.0f} against {lo[0]['cf1']:.0f}"


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("tied", [True, False])
@pytest.mark.parametrize("word,ipa,onset", [
    ("writer", "ɹˈa͡ɪɾɚ", "ʌ"), ("writing", "ɹˈa͡ɪɾɪŋ", "ʌ"), ("lighter", "lˈa͡ɪɾɚ", "ʌ"), ("item", "ˈa͡ɪɾəm", "ʌ"),
    ("outer", "ˈa͡ʊɾɚ", "ʌ"), ("shouting", "ʃˈa͡ʊɾɪŋ", "ʌ"),
    ("rider", "ɹˈa͡ɪdɚ", "ɑ"), ("riding", "ɹˈa͡ɪdɪŋ", "ɑ"), ("louder", "lˈa͡ʊdɚ", "ä"),
])
def test_writer_is_raised_and_rider_is_not(word, ipa, onset, tied, speed):
    # eSpeak NG's en-us writes the flap ɾ only for /t/; /d/ stays d.
    keys, got, _ = _first_vowel(_spell(ipa, tied), speed=speed)
    assert got == onset, f"{word} @{speed}: {keys}"


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("tied", [True, False])
@pytest.mark.parametrize("word,ipa", [
    ("citation", "sa͡ɪtˈe͡ɪʃən"), ("titanic", "ta͡ɪtˈænɪk"), ("psychology", "sa͡ɪkˈɑːləd͡ʒi"),
    ("hypothesis", "ha͡ɪpˈɑːθəsˌɪs"), ("myself", "ma͡ɪsˈɛlf"),
])
def test_not_raised_before_the_stressed_syllable(word, ipa, tied, speed):
    keys, onset, _ = _first_vowel(_spell(ipa, tied), speed=speed)
    assert onset == ONSET["price"], f"{word} @{speed}: {keys}"


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("tied", [True, False])
@pytest.mark.parametrize("phrase,ipa,onset", [
    ("high school", "hˈa͡ɪ skˈuːl", "ɑ"), ("my turn", "ma͡ɪ tˈɜːn", "ɑ"), ("how come", "hˌa͡ʊ kˈʌm", "ä"),
    ("cowhide", "kˈa͡ʊha͡ɪd", "ä"),  # h only ever opens a syllable
])
def test_not_raised_across_a_word_or_before_h(phrase, ipa, onset, tied, speed):
    keys, got, _ = _first_vowel(_spell(ipa, tied), speed=speed)
    assert got == onset, f"{phrase} @{speed}: {keys}"


# ── what must stay as it was ────────────────────────────────────────

@pytest.mark.parametrize("word,ipa,vowel", [
    ("cot", "kˈɑːt", "ᴐ"), ("caught", "kˈɔːt", "ᴐ"), ("father", "fˈɑːðɚ", "ᴐ"), ("lot", "lˈɑːt", "ᴐ"),
    ("a short LOT", "kɑt", "ᴐ"),                      # the COT-CAUGHT merger still merges
    ("cat", "kˈæt", "æ"), ("TRAP written a", "kˈat", "æ"),   # en.yaml's a -> æ still runs
    ("car", "kˈɑː͡ɹ", "ʌ"), ("sorry", "sˈɑːɹi", "ᴐ"),          # START, and the Canadian "sorry"
])
def test_other_en_ca_vowels_are_as_they_were(word, ipa, vowel):
    keys, got, _ = _first_vowel(ipa)
    assert got == vowel, f"{word}: {keys}"
    assert "ɑ" not in keys, f"{word}: {keys}"


def test_microsoft_is_raised_and_keeps_its_soft():
    # The older preReplacements (sˌɑːft -> sˌɔft) still run beside the new ones.
    _, keys = _render("mˈa͡ɪkɹəsˌɑːft")
    assert keys[:2] == ["m", "ʌ"] and "ᴐ" in keys and "ɑ" not in keys, keys


# en-us, en-gb and en-au do not read en-ca.yaml: their PRICE and MOUTH are
# what they were (keys, and the onset's F1 and F2 at speed 1.0).
OTHERS = [
    ("en-us", "ɹˈa͡ɪt", ["ɹ", "ɑ", "t", "t"], (700, 1330)), ("en-us", "mˈa͡ɪ", ["m", "ɑ"], (700, 1305)),
    ("en-us", "hˈa͡ʊs", ["h", "ä", "s"], (730, 1450)), ("en-us", "lˈa͡ʊd", ["l", "l", "ä", "d", "d"], (730, 1450)),
    ("en-gb", "ɹˈa͡ɪt", ["ɹ", "a_gb", "t", "t"], None), ("en-gb", "hˈa͡ʊs", ["h", "å", "s"], None),
    ("en-au", "ɹˈa͡ɪt", ["ɹ", "ᵅ", "t", "t"], None), ("en-au", "mˈa͡ɪ", ["m", "ᵅ"], None),
]


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang,ipa,keys,onset", OTHERS)
def test_other_english_is_not_raised(lang, ipa, keys, onset, speed):
    u, got = _render(ipa, lang, speed)
    assert got == keys, f"{lang} {ipa} @{speed}: {got}"
    assert "ʌ" not in got
    if onset:
        tr = u.frame_trace
        n = next(i for i, k in enumerate(got) if k in ("ɑ", "ä"))
        p = u.frames[tr[n]["frameIndex"]].params
        assert (round(p["cf1"]), round(p["cf2"])) == onset, f"{lang} {ipa} @{speed}: {p['cf1']:.0f}/{p['cf2']:.0f}"
