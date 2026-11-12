"""nvspFrontend_setLanguageCached: switching back reuses a pack, never a stale one (#131).

Automatic language switching moves between the same few languages all the
time, and parsing a language's YAML costs ~110 ms for en-us.  The cached
switch keeps the packs a handle loaded; these tests pin what must not change
with it:

* a pack file edited after the pack was kept (the NVDA settings panel writes
  YAML, the phoneme editor writes YAML, a profile save rewrites
  phonemes.yaml) is read on the next switch into that language;
* the voice profile in use survives every switch, as it does a plain
  setLanguage, and so does having none: a profile turned off is not brought
  back by a pack that was kept while it was on (#174);
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
    dll.nvspFrontend_beginStream.argtypes = [ctypes.c_void_p]
    dll.nvspFrontend_beginStream.restype = None
    return dll


def _switch(api, h, lang):
    assert api.nvspFrontend_setLanguageCached(h, lang.encode()) == 1, f"setLanguageCached({lang})"


def _frames(frontend, h, ipa="kˈæt", speed=1.0):
    return [(r.is_silence, round(r.duration_ms, 3), round(r.fade_ms, 3),
             None if r.frame_dict is None else tuple(round(v, 3) for v in r.frame_dict.values()))
            for r in frontend.capture_frames(h, ipa, speed=speed)]


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


# --- A profile turned off stays off (#174) -----------------------------------
#
# setVoiceProfile changes the pack in use; the packs kept for the cached
# switch were left with the profile that was on when they were put away, and
# the next automatic language switch brought it back.  A cached switch has to
# give what a plain setLanguage gives at that point, whatever the profile is.

SPEEDS = [0.5, 1.0, 1.6, 2.0, 3.0]
HELLO = "hɛloʊ"


def _profile(api, h):
    return api.nvspFrontend_getVoiceProfile(h).decode("utf-8")


def _set_profile(api, h, name):
    assert api.nvspFrontend_setVoiceProfile(h, name.encode("utf-8")) == 1, f"setVoiceProfile({name!r})"


def _hello(frontend, api, h, speed):
    # From the start of a stream each time, so that a render does not depend
    # on how many came before it on the handle.
    api.nvspFrontend_beginStream(h)
    return _frames(frontend, h, HELLO, speed)


def test_a_profile_turned_off_stays_off_through_cached_switches(frontend, api, pack_dir):
    h = frontend.create_handle(pack_dir)
    try:
        frontend.set_language(h, "en-us")
        _set_profile(api, h, "Beth")
        for lang in ("hu", "en-us"):
            _switch(api, h, lang)
            assert _profile(api, h) == "Beth", f"profile lost switching to {lang}"

        _set_profile(api, h, "")
        assert _profile(api, h) == ""
        for lang in ("hu", "en-us", "hu", "en-us"):
            _switch(api, h, lang)
            assert _profile(api, h) == "", f"the profile came back switching to {lang}"
    finally:
        frontend.destroy_handle(h)


@pytest.mark.parametrize("speed", SPEEDS)
def test_frames_after_a_profile_is_turned_off_are_those_of_no_profile(frontend, api, pack_dir, speed):
    plain = frontend.create_handle(pack_dir)   # never had a profile
    h = frontend.create_handle(pack_dir)
    try:
        want = {}
        for lang in ("en-us", "hu"):
            frontend.set_language(plain, lang)
            want[lang] = _hello(frontend, api, plain, speed)

        frontend.set_language(h, "en-us")
        _set_profile(api, h, "Beth")
        assert _hello(frontend, api, h, speed) != want["en-us"], "Beth changes nothing; the test would see nothing"
        _switch(api, h, "hu")
        _switch(api, h, "en-us")

        _set_profile(api, h, "")
        assert _hello(frontend, api, h, speed) == want["en-us"]
        for lang in ("hu", "en-us", "hu", "en-us"):
            _switch(api, h, lang)
            assert _hello(frontend, api, h, speed) == want[lang], (
                f"{lang} after the profile was turned off is not {lang} with no profile")
    finally:
        frontend.destroy_handle(plain)
        frontend.destroy_handle(h)


# A step is a language to switch to, or ("profile", name) to set one ("" = none).
_SCRIPTS = {
    "never_on": ["en-us", "hu", "en-us", "hu"],
    "kept_on": ["en-us", ("profile", "Beth"), "hu", "en-us", "es", "en-us"],
    "turned_off": ["en-us", ("profile", "Beth"), "hu", "en-us", ("profile", ""), "hu", "en-us", "hu"],
    "turned_off_in_a_third_language": [
        "en-us", ("profile", "Beth"), "hu", "pt-br", ("profile", ""), "en-us", "hu", "pt-br"],
    "none_set_in_the_second_language": ["en-us", "hu", ("profile", ""), "en-us", "hu", "en-us"],
    "changed": ["en-us", ("profile", "Beth"), "hu", "en-us", ("profile", "Bobby"), "hu", "es", "en-us"],
    "off_then_another": [
        "en-us", ("profile", "Beth"), "hu", ("profile", ""), "en-us", ("profile", "Bobby"), "hu", "en-us"],
}


def _assert_cached_switches_match_full_loads(frontend, api, pack_root, script, speed):
    full = frontend.create_handle(pack_root)
    cached = frontend.create_handle(pack_root)
    try:
        for n, step in enumerate(script):
            if isinstance(step, tuple):
                _set_profile(api, full, step[1])
                _set_profile(api, cached, step[1])
            else:
                frontend.set_language(full, step)
                _switch(api, cached, step)
            where = f"step {n} ({step!r}) of {script!r}"
            assert _profile(api, cached) == _profile(api, full), f"profile after {where}"
            assert _hello(frontend, api, cached, speed) == _hello(frontend, api, full, speed), f"frames after {where}"
    finally:
        frontend.destroy_handle(full)
        frontend.destroy_handle(cached)


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("name", sorted(_SCRIPTS))
def test_a_cached_switch_gives_what_a_full_load_gives(frontend, api, pack_dir, name, speed):
    _assert_cached_switches_match_full_loads(frontend, api, pack_dir, _SCRIPTS[name], speed)


@pytest.fixture
def packs_with_a_profile_in_hu(packs_copy):
    # "voiceProfileName:" in a language's settings (nvspFrontend.h; the NVDA
    # settings panel can write it) names a profile for that language.
    yaml = packs_copy / "packs" / "lang" / "hu.yaml"
    text = yaml.read_text(encoding="utf-8")
    assert text.count("\nsettings:\n") == 1
    yaml.write_text(text.replace("\nsettings:\n", "\nsettings:\n  voiceProfileName: Beth\n"), encoding="utf-8")
    return packs_copy


def test_a_profile_named_in_a_language_yaml_is_still_used(frontend, api, packs_with_a_profile_in_hu):
    # What must be left alone: with no profile set by the host, a language
    # that names one gets it, loaded in full or switched to.
    root = str(packs_with_a_profile_in_hu)
    full = frontend.create_handle(root)
    cached = frontend.create_handle(root)
    try:
        frontend.set_language(full, "hu")
        assert _profile(api, full) == "Beth"
        _switch(api, cached, "en-us")
        assert _profile(api, cached) == ""
        _switch(api, cached, "hu")
        assert _profile(api, cached) == "Beth"
    finally:
        frontend.destroy_handle(full)
        frontend.destroy_handle(cached)


@pytest.mark.parametrize("speed", SPEEDS)
@pytest.mark.parametrize("name", ["never_on", "turned_off", "changed", "none_set_in_the_second_language"])
def test_a_cached_switch_gives_what_a_full_load_gives_with_a_profile_in_a_yaml(
        frontend, api, packs_with_a_profile_in_hu, name, speed):
    _assert_cached_switches_match_full_loads(
        frontend, api, str(packs_with_a_profile_in_hu), _SCRIPTS[name], speed)
