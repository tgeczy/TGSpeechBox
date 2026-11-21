"""The packs load from a folder whose name is not plain ASCII.

nvspFrontend_create and nvspFrontend_setOverrideDirectory take their paths as
UTF-8 (nvspFrontend.h).  On Windows the frontend opened the files with the
narrow C++ calls, which read a path in the system code page: any letter
outside ASCII in the path, and the packs were "not found".  The NVDA add-on
lives under %APPDATA%, so that is a Windows account named Tamás, José or
Győző, or a portable NVDA in such a folder: the synthesizer did not load.
Found by checking this repository against the audits of the maintainer's
other speech projects (the same fault in two of them).

What is asserted is what a listener would notice, not "no error": the frames
for a sentence are the same, value for value, as from the repository's own
(ASCII) folder, the pronunciation dictionary answers, the language list is
complete, and a user dictionary in an override folder is in force.
"""
from __future__ import annotations

import ctypes
import shutil

import pytest

#: The letters matter, not the names: inside the Western code page (1252),
#: outside it but European, and two scripts no Western code page has.
FOLDERS = {
    "ascii": "plain folder",
    "cp1252": "Tamás José",
    "hungarian": "Győző űr",
    "cyrillic": "Всеволод",
    "cjk": "用户",
}

IPA = "həlˈoʊ wˈɜːld"
DICT_WORD = "Dumbledore"            # packs/dict/en-us-dict.tsv: read as "Dumbble door"
USER_WORD = "zzyzxqq"
USER_SAYS = "swapped"


def _frames(fe, handle):
    return [(r.is_silence, r.duration_ms, r.fade_ms, tuple(sorted((r.frame_dict or {}).items())))
            for r in fe.capture_frames(handle, IPA)]


def _languages(fe, handle):
    fn = fe._dll.nvspFrontend_getAvailableLanguages
    fn.argtypes, fn.restype = [ctypes.c_void_p], ctypes.c_void_p
    ptr = fn(handle)
    assert ptr, "no language list at all"
    try:
        return sorted(ctypes.string_at(ptr).decode("utf-8").split())
    finally:
        fe._dll.nvspFrontend_freeString(ptr)


@pytest.fixture(scope="module")
def reference(frontend, pack_dir):
    """What the repository's own folder gives: the thing to be equal to."""
    h = frontend.create_handle(pack_dir)
    try:
        frontend.set_language(h, "en-us")
        return {"frames": _frames(frontend, h), "dict": frontend.prepare_text(h, DICT_WORD),
                "languages": _languages(frontend, h)}
    finally:
        frontend.destroy_handle(h)


def test_the_reference_is_worth_comparing_with(reference):
    assert len(reference["frames"]) > 10
    assert reference["dict"] != DICT_WORD, "the dictionary entry this test leans on is gone"
    assert "hu" in reference["languages"] and "en-us" in reference["languages"]


@pytest.mark.parametrize("kind", list(FOLDERS))
def test_packs_load_from_a_folder_named_like_this(kind, frontend, pack_dir, reference, tmp_path):
    root = tmp_path / FOLDERS[kind]
    shutil.copytree(pack_dir + "/packs", root / "packs")
    h = frontend.create_handle(str(root))
    try:
        frontend.set_language(h, "en-us")          # raises with the frontend's own message
        assert _frames(frontend, h) == reference["frames"], "the frames differ: these are not the same packs"
        assert frontend.prepare_text(h, DICT_WORD) == reference["dict"], "the pronunciation dictionary was not read"
        assert _languages(frontend, h) == reference["languages"], "the language list is short"
        frontend.set_language(h, "hu")             # a second language file from the same folder
    finally:
        frontend.destroy_handle(h)


@pytest.mark.parametrize("kind", list(FOLDERS))
def test_a_user_dictionary_in_an_override_folder_named_like_this_is_in_force(kind, frontend, pack_dir, tmp_path):
    """The override folder is where the phone apps and the SAPI engine keep
    what the user changed; it goes through the same file calls."""
    override = tmp_path / ("override " + FOLDERS[kind])
    (override / "packs" / "dict").mkdir(parents=True)
    (override / "packs" / "dict" / "en-us-user.tsv").write_text(
        f"# from_text\tto_text\tfrom_ipa\tto_ipa\tcategory\n{USER_WORD}\t{USER_SAYS}\t\t\ttest\n", encoding="utf-8")
    h = frontend.create_handle(pack_dir)
    try:
        frontend.set_language(h, "en-us")
        assert frontend.prepare_text(h, USER_WORD) == USER_WORD, "the word is already in a dictionary: pick another"
        fn = frontend._dll.nvspFrontend_setOverrideDirectory
        fn.argtypes, fn.restype = [ctypes.c_void_p, ctypes.c_char_p], None
        fn(h, str(override).encode("utf-8"))
        frontend.set_language(h, "en-us")
        assert frontend.prepare_text(h, USER_WORD) == USER_SAYS, "the user's dictionary in the override folder was not read"
    finally:
        frontend.destroy_handle(h)
