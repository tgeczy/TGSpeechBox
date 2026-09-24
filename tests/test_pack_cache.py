"""nvspFrontend_setLanguageCached: switching back reuses a pack, never a stale one (#131).

Automatic language switching moves between the same few languages all the
time, and parsing a language's YAML costs ~110 ms for en-us.  The cached
switch keeps the packs a handle loaded; these tests pin what must not change
with it:

* a pack file edited after the pack was kept (the NVDA settings panel writes
  YAML, the phoneme editor writes YAML, a profile save rewrites
  phonemes.yaml) is read on the next switch into that language;
* the voice profile in use survives every switch, as it does a plain
  setLanguage;
* a switch back with nothing changed gives the same frames as before.
"""
from __future__ import annotations

import ctypes
import os
import pathlib
import shutil
import time

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent


@pytest.fixture
def packs_copy(tmp_path):
    shutil.copytree(REPO / "packs", tmp_path / "packs")
    return tmp_path


@pytest.fixture
def api(frontend):
    dll = frontend._dll
    if not hasattr(dll, "nvspFrontend_setLanguageCached"):
        pytest.skip("nvspFrontend.dll predates setLanguageCached")
    dll.nvspFrontend_setLanguageCached.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    dll.nvspFrontend_setLanguageCached.restype = ctypes.c_int
    dll.nvspFrontend_setVoiceProfile.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    dll.nvspFrontend_setVoiceProfile.restype = ctypes.c_int
    dll.nvspFrontend_getVoiceProfile.argtypes = [ctypes.c_void_p]
    dll.nvspFrontend_getVoiceProfile.restype = ctypes.c_char_p
    return dll


def _switch(api, h, lang):
    assert api.nvspFrontend_setLanguageCached(h, lang.encode()) == 1, f"setLanguageCached({lang})"


def _frames(frontend, h, ipa="kˈæt"):
    return [(r.is_silence, round(r.duration_ms, 3), round(r.fade_ms, 3),
             None if r.frame_dict is None else tuple(round(v, 3) for v in r.frame_dict.values()))
            for r in frontend.capture_frames(h, ipa, speed=1.0)]


def test_switching_back_with_nothing_changed_gives_the_same_frames(frontend, api, packs_copy):
    h = frontend.create_handle(str(packs_copy))
    try:
        _switch(api, h, "en-us")
        before = _frames(frontend, h)
        _switch(api, h, "pt-br")
        _switch(api, h, "en-us")
        assert _frames(frontend, h) == before
    finally:
        frontend.destroy_handle(h)


def test_a_pack_file_edited_after_it_was_kept_is_read_on_the_next_switch(frontend, api, packs_copy):
    h = frontend.create_handle(str(packs_copy))
    try:
        _switch(api, h, "en-us")
        before = _frames(frontend, h)
        _switch(api, h, "pt-br")

        yaml = packs_copy / "packs" / "lang" / "en-us.yaml"
        text = yaml.read_text(encoding="utf-8")
        assert "stopClosureVowelGapMs: 18" in text
        yaml.write_text(text.replace("stopClosureVowelGapMs: 18", "stopClosureVowelGapMs: 60"), encoding="utf-8")
        # Some filesystems keep write times to the second or coarser; make sure
        # the edit is visible as a new time, as a real edit minutes later is.
        later = time.time() + 5
        os.utime(yaml, (later, later))

        _switch(api, h, "en-us")
        after = _frames(frontend, h)
        assert after != before, "the kept en-us pack was used although en-us.yaml had changed"

        # And the edited pack is what a full load gives.
        frontend.set_language(h, "en-us")
        assert _frames(frontend, h) == after
    finally:
        frontend.destroy_handle(h)


def test_the_voice_profile_survives_cached_switches(frontend, api, packs_copy):
    h = frontend.create_handle(str(packs_copy))
    try:
        _switch(api, h, "en-us")
        assert api.nvspFrontend_setVoiceProfile(h, b"Beth") == 1
        for lang in ("pt-br", "en-us", "es", "en-us"):
            _switch(api, h, lang)
            assert api.nvspFrontend_getVoiceProfile(h) == b"Beth", f"profile lost switching to {lang}"
    finally:
        frontend.destroy_handle(h)
