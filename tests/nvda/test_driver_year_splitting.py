"""Year splitting turns on and off from the voice panel, and stays as set (#140).

Edu: "sometimes it reads 20 20 instead of 2020, so I disable that option,
but it's still strange.  It seems like the selection box is messed up."
Year splitting lives in the language file, like the other #140 settings, so
the checkbox has to show the current language's file and what is spoken has
to follow the checkbox: unchecked, reopened, checked again, across a
language switch and back.
"""
from __future__ import annotations

import pytest

TEXT = ["In 2020 it rained."]


def _speech(harness, record_frames):
    log = record_frames(harness.driver)
    harness.driver.cancel()
    harness.speak(TEXT)
    return list(log)


def _reopen_panel(driver):
    """NVDA's voice panel reads every setting from the driver when it opens."""
    return driver.yearSplitting


@pytest.mark.parametrize("lang", ["en-us", "es"])
def test_unchecked_stays_unchecked_and_is_heard(harness, record_frames, lang):
    d = harness.driver
    d.language = lang
    d.yearSplitting = True
    assert _reopen_panel(d) is True
    split = _speech(harness, record_frames)

    d.yearSplitting = False
    assert _reopen_panel(d) is False, f"{lang}: the box came back checked"
    whole = _speech(harness, record_frames)
    assert whole != split, f"{lang}: unchecking changed nothing that is spoken"

    d.yearSplitting = True
    assert _reopen_panel(d) is True
    assert _speech(harness, record_frames) == split, f"{lang}: checking it again didn't bring the split back"

    d.yearSplitting = False
    assert _speech(harness, record_frames) == whole


def test_language_switch_and_back_keeps_the_choice(harness, record_frames):
    d = harness.driver
    d.language = "en-us"
    d.yearSplitting = False
    whole = _speech(harness, record_frames)
    d.language = "es"
    d.language = "en-us"
    assert _reopen_panel(d) is False, "switching language and back turned year splitting on again"
    assert _speech(harness, record_frames) == whole


def _english_text(driver, doc_lang):
    import conftest as H
    return H.nvda_sequence(driver, TEXT, doc_lang=doc_lang)


@pytest.mark.parametrize("doc_lang", ["en", "en_US"])
def test_a_switched_to_language_keeps_its_own_setting(harness, record_frames, doc_lang):
    """Auto language switching speaks a language by its own rules, not the
    voice language's: Spanish with the box unchecked still splits English
    years while English has it on, and an English choice made with English
    as the voice is honored when the driver switches to English later."""
    d = harness.driver
    d.language = "en-us"
    d.yearSplitting = True
    split = _speech(harness, record_frames)
    d.yearSplitting = False
    whole = _speech(harness, record_frames)

    d.yearSplitting = True
    d.language = "es"
    d.yearSplitting = False
    log = record_frames(d)
    d.cancel()
    harness.speak(_english_text(d, doc_lang))
    assert list(log) == split, f"{doc_lang}: English took Spanish's year splitting"

    d.language = "en-us"
    d.yearSplitting = False
    d.language = "es"
    log = record_frames(d)
    d.cancel()
    harness.speak(_english_text(d, doc_lang))
    assert list(log) == whole, f"{doc_lang}: the English choice (off) was not honored after a switch"
