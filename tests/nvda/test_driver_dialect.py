"""Automatic language switching keeps the user's own dialect (#127, #131).

With NVDA's automatic language switching on, NVDA compares the language of
the text with the synth's own `language` and sends a LangChangeCommand when
they differ.  For text in the same language as the synth, with dialect
switching off (NVDA's default), it sends the synth's own language instead, so
a Brazilian user's voice stays Brazilian on a page tagged plain "pt".

That comparison works on the synth's `language` setting, and ours is "auto"
by default.  NVDA normalizes "auto" to "auto", which matches nothing, so every
tag reached the driver as written: plain "pt" chose the European Portuguese
pack for a Brazilian user, plain "es" chose Spain for a Mexican one.  Edu
(#127) had automatic switching on the day he reported that "agota" was right
when echoed as he typed it and blurred when read back.

The sequences below are built the way NVDA's speech.speak() builds them
(conftest.nvda_sequence), from the driver's own `language`.
"""
from __future__ import annotations

import pytest

def _frames_for(harness, record_frames, sequence):
    log = record_frames(harness.driver)
    harness.driver.cancel()
    harness.speak(sequence)
    return list(log)


def _as_user(harness, ui_language, windows_language, setting):
    import languageHandler
    languageHandler.uiLanguage = ui_language
    languageHandler.windowsLanguage = windows_language
    harness.driver.language = setting


CASES = [
    # (NVDA UI language, Windows locale, driver setting, the user's dialect,
    #  a tag the text may carry, a word)
    ("pt_BR", "pt_BR", "auto", "pt-br", "pt", "agota"),
    ("pt_BR", "pt_BR", "auto", "pt-br", "pt_PT", "agora"),
    ("pt_BR", "pt_BR", "pt-br", "pt-br", "pt", "agota"),
    ("es_MX", "es_MX", "auto", "es-mx", "es", "gracias"),
    ("es_MX", "es_MX", "es-mx", "es-mx", "es_ES", "gracias"),
    ("en", "en_GB", "auto", "en-gb", "en", "garden"),
]


@pytest.mark.parametrize("ui,win,setting,dialect,tag,word", CASES,
                         ids=[f"{c[3]}-{c[2]}-tag-{c[4]}" for c in CASES])
def test_text_in_the_users_language_keeps_the_users_dialect(harness, nvda_sequence, record_frames, ui, win, setting, dialect, tag, word):
    _as_user(harness, ui, win, setting)
    assert harness.driver._resolvedLang == dialect

    # Warm up with the same word, so both readings below follow the same
    # utterance (the frontend's stream state carries from one to the next).
    _frames_for(harness, record_frames, nvda_sequence(harness.driver, [word]))
    untagged = _frames_for(harness, record_frames, nvda_sequence(harness.driver, [word]))
    tagged = _frames_for(harness, record_frames, nvda_sequence(harness.driver, [word], doc_lang=tag))
    assert harness.driver._activeSpeechLang in (dialect, None), (
        f"text tagged {tag!r} switched a {dialect} user to {harness.driver._activeSpeechLang}")
    assert tagged == untagged, (
        f"{word!r} tagged {tag!r} does not sound as it does in the user's own {dialect}")


def test_another_language_still_switches(harness, nvda_sequence):
    """The fix must not stop real switching: Spanish text for a Brazilian
    user is read in Spanish."""
    _as_user(harness, "pt_BR", "pt_BR", "auto")
    harness.driver.cancel()
    harness.speak(nvda_sequence(harness.driver, ["gracias"], doc_lang="es"))
    assert harness.driver._activeSpeechLang == "es"


def test_dialect_switching_on_follows_the_tag(harness, nvda_sequence):
    """With NVDA's "automatic dialect switching" on, an explicit dialect is
    honoured; a bare tag still says nothing about the dialect."""
    import config
    _as_user(harness, "pt_BR", "pt_BR", "auto")
    config.conf["speech"]["autoDialectSwitching"] = True
    try:
        harness.driver.cancel()
        harness.speak(nvda_sequence(harness.driver, ["agora"], doc_lang="pt_PT", auto_dialect_switching=True))
        assert harness.driver._activeSpeechLang == "pt"
        harness.driver.cancel()
        harness.speak(nvda_sequence(harness.driver, ["agora"], doc_lang="pt", auto_dialect_switching=True))
        assert harness.driver._activeSpeechLang == "pt-br"
    finally:
        config.conf["speech"]["autoDialectSwitching"] = False
