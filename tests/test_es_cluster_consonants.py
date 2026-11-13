"""A Spanish consonant next to a tap is still the Spanish one (#187).

"verde", "padre", "tarde", "cuadro" said their d as the base ð, the English
"th" of "the", and not as ð_es, the Spanish approximant that "dos" and
"todo" get.  "carne" and "arma" had the base n and m, "virgen" the base x,
"archivo" the base t͡ʃ, "algo" the dark English l.

es.yaml gives a cluster room with a micro-schwa (ɾð -> ɾᵊð), and further
down maps each base phoneme to the Spanish one (ð -> ð_es, l -> l_es, ...).
A rule that makes the text longer protects what it wrote from the rules
after it, so the consonant in the cluster never reached its own mapping.
The cluster rules now write the Spanish phoneme themselves, as the one for
ɾs has since #143.  es-mx keeps the two it does not take from es.yaml: its
own s_mx and the base x.

One part of the report is left as it was, and pinned below: the l AFTER the
micro-schwa or after ɣ_cls ("perla", "Carlos", "globo", "regla") is still the
base l.  With l_es there Whisper lost words it had heard before; es.yaml has
the numbers.

Spanish /b d g/ are approximants after a tap as they are after a vowel
([ˈbeɾðe], [ˈaɾβol]; Martínez-Celdrán, Fernández-Planas & Carrera-Sabaté
2003, "Castilian Spanish", Journal of the International Phonetic
Association 33(2): 255-259; Carrasco, Hualde & Simonet 2012, Phonetica
69(3): 149-179, measured them after liquids in Madrid Spanish).

The IPA is eSpeak NG 1.53's for each word, the same in es and es-419 unless
the table says otherwise.
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

from tgspeechbox.engine import Frontend  # noqa: E402

SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]
DIALECTS = ["es", "es-mx", "es-es"]

WORDS = {
    # tap + consonant
    "verde": "bˈeɾðe",
    "tarde": "tˈaɾðe",
    "carne": "kˈaɾne",
    "arma": "ˈaɾma",
    "hermano": "eɾmˈano",
    "virgen": "bˈiɾxen",
    "archivo": "aɾt͡ʃˈiβo",
    "marchar": "maɾt͡ʃˈaɾ",
    # consonant + tap
    "padre": "pˈaðɾe",
    "madre": "mˈaðɾe",
    "cuadro": "kwˈaðɾo",
    "ladrón": "laðɾˈon",
    "drama": "dɾˈama",
    # l or s next to a velar
    "algo": "ˈalɣo",
    "salgo": "sˈalɣo",
    "algunos": "alɣˈunos",
    # eSpeak NG writes ɣ after l and s inside a word, so these two rules
    # (lɡ, sɡ) are only reached by other IPA.  Made up.
    "(made up) l + ɡ": "ˈalɡo",
    "(made up) s + ɡ": "ˈasɡo",
}

# The base phonemes es.yaml maps to a Spanish one.  None may be left.
MAPPED = {"ð", "d", "l", "m", "n", "t͡ʃ", "s", "x"}
# es-mx takes neither x -> x_es nor s -> s_es: its /x/ is the base one.
KEEPS_BASE = {"es": set(), "es-es": set(), "es-mx": {"x"}}


def _keys(lang, ipa, speed):
    with Frontend(lang) as fe:
        fe.frames(ipa, speed)
        return [e["phonemeKey"] for e in fe.frame_trace()]


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang", DIALECTS)
@pytest.mark.parametrize("word", list(WORDS))
def test_no_base_consonant_is_left(word, lang, speed):
    keys = _keys(lang, WORDS[word], speed)
    left = sorted((set(keys) & MAPPED) - KEEPS_BASE[lang])
    assert left == [], f"{word} ({lang}) at {speed}: base {left} in {keys}"


# The whole word, so that the cluster keeps its shape: the micro-schwa is
# where it was, and only the consonant beside it changed.
EXACT = {
    "verde": ["b", "b", "e_es", "ɾ", "ᵊ", "ð_es", "e_es"],
    "padre": ["p", "p", "a_es", "ð_es", "ᵊ", "ɾ", "e_es"],
    "carne": ["k", "k", "a_es", "ɾ", "ᵊ", "n_es", "e_es"],
    "arma": ["a_es", "ɾ", "ᵊ", "m_es", "a_es"],
    "archivo": ["a_es", "ɾ", "ᵊ", "t͡ʃ_es", "t͡ʃ_es", "i", "β", "o_es"],
    "drama": ["d_es", "d_es", "ᵊ", "ɾ", "a_es", "m_es", "a_es"],
    "algo": ["a_es", "l_es", "ɣ_cls", "o_es"],
    "(made up) l + ɡ": ["a_es", "l_es", "ᵊ", "ɡ", "ɡ", "o_es"],
}


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("lang", ["es", "es-mx"])
@pytest.mark.parametrize("word", list(EXACT))
def test_the_cluster_keeps_its_shape(word, lang, speed):
    assert _keys(lang, WORDS[word], speed) == EXACT[word]


# Left as they were: the l after the micro-schwa (ɾl, ɡl) and after ɣ_cls
# (ɣl) is the base l.  With l_es there, Whisper large-v2 heard "perla" as
# "tela", "burla" as "luna", "regla" as "red la" (ɾl and ɡl: 2 of 14
# sentences against 6 of 14 with the base l; ɣl: 12 of 14 against 14 of
# 14), so those three rules of es.yaml were not changed; the comment above
# its ɾð rule has the numbers.  Pinned so that a change is made on purpose.
L_AFTER_THE_CLUSTER = {
    "perla": ("pˈeɾla", ["p", "p", "e_es", "ɾ", "ᵊ", "l", "a_es"]),
    "Carlos": ("kˈaɾlos", ["k", "k", "a_es", "ɾ", "ᵊ", "l", "o_es", "s_es"]),
    "globo": ("ɡlˈoβo", ["ɡ", "ɡ", "ᵊ", "l", "o_es", "β", "o_es"]),
    "inglés": ("iŋɡlˈes", ["i", "ŋ", "ɡ", "ɡ", "ᵊ", "l", "e_es", "s_es"]),
    "regla": ("rˈeɣla", ["r_es", "e_es", "ɣ_cls", "l", "a_es"]),
    "iglesia": ("iɣlˈesja", ["i", "ɣ_cls", "l", "e_es", "s_es", "j_es", "a_es"]),
    "siglo": ("sˈiɣlo", ["s_es", "i", "ɣ_cls", "l", "o_es"]),
}


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("word", list(L_AFTER_THE_CLUSTER))
def test_l_after_the_cluster_is_what_it_was(word, speed):
    ipa, want = L_AFTER_THE_CLUSTER[word]
    assert _keys("es", ipa, speed) == want


@pytest.mark.parametrize("speed", SPEEDS)
def test_castilian_takes_the_same_clusters(speed):
    # es-es reads es.yaml's cluster rules; its own β_cas, ɣ_cas and ð_cas are
    # not written into them (see the commit message of #187).
    assert _keys("es-es", WORDS["verde"], speed) == EXACT["verde"]
    assert _keys("es-es", WORDS["carne"], speed) == EXACT["carne"]
    assert _keys("es-es", WORDS["algo"], speed) == EXACT["algo"]


@pytest.mark.parametrize("speed", SPEEDS)
def test_x_after_a_tap_is_the_dialects(speed):
    assert _keys("es", WORDS["virgen"], speed) == ["b", "b", "i", "ɾ", "ᵊ", "x_es", "e_es", "n_es"]
    assert _keys("es-es", WORDS["virgen"], speed) == ["b", "b", "i", "ɾ", "ᵊ", "x_es", "e_es", "n_es"]
    # Mexican /x/ is the softer base one, here as everywhere ("jota").
    assert _keys("es-mx", WORDS["virgen"], speed) == ["b", "b", "i", "ɾ", "ᵊ", "x", "e_es", "n_es"]
    assert _keys("es-mx", "xˈota", speed)[0] == "x"


@pytest.mark.parametrize("speed", SPEEDS)
def test_s_before_g_is_the_dialects(speed):
    assert _keys("es", WORDS["(made up) s + ɡ"], speed) == ["a_es", "s_es", "ᵊ", "ɡ", "ɡ", "o_es"]
    assert _keys("es-es", WORDS["(made up) s + ɡ"], speed) == ["a_es", "s_es", "ᵊ", "ɡ", "ɡ", "o_es"]
    assert _keys("es-mx", WORDS["(made up) s + ɡ"], speed) == ["a_es", "s_mx", "ᵊ", "ɡ", "ɡ", "o_es"]


# Words with none of these clusters are what they were.
UNCHANGED = {
    "dos": ("dˈos", ["d_es", "d_es", "o_es", "s_es"]),
    "casa": ("kˈasa", ["k", "k", "a_es", "s_es", "a_es"]),
    "cuando": ("kwˈando", ["k", "k", "w", "a_es", "n_es", "d_es", "d_es", "o_es"]),
    "luna": ("lˈuna", ["l_es", "u_es", "n_es", "a_es"]),
    "rasgo": ("rˈasɣo", ["r_es", "a_es", "s_es", "ɣ_es", "o_es"]),
    "árbol": ("ˈaɾβol", ["a_es", "ɾ", "ᵊ", "β", "o_es", "l_es"]),
    "mucho": ("mˈut͡ʃo", ["m_es", "u_es", "t͡ʃ_es", "t͡ʃ_es", "o_es"]),
}


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("word", list(UNCHANGED))
def test_other_words_are_what_they_were(word, speed):
    ipa, want = UNCHANGED[word]
    assert _keys("es", ipa, speed) == want


@pytest.mark.parametrize("lang", ["pt-br", "it", "en-us", "hu"])
@pytest.mark.parametrize("word", ["verde", "padre", "carne", "archivo"])
def test_other_languages_do_not_get_spanish_phonemes(lang, word):
    keys = _keys(lang, WORDS[word], 1.0)
    assert not [k for k in keys if k.endswith(("_es", "_mx", "_cls"))], keys
