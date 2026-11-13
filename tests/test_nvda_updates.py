"""The NVDA add-on's update check reads GitHub's releases, on the channel chosen.

Until 3.10 the add-on fetched one fixed file from the maintainer's own site
and a version text file beside it, both uploaded by hand for every build.
People asked for the add-on to come from the GitHub releases instead, and for
a choice between stable releases and prereleases (#103).

What can be quietly wrong is the choosing: which release, which asset, and
whether it is newer than what is installed.  That part is pure and is tested
here against release lists shaped like GitHub's.  The fetch is handed in as
`opener`, so nothing here touches the network, and nothing needs NVDA.

The add-on's version is the date in its manifest (2026.11.10), not the
release tag (v-310rc301): a release also carries the SAPI installer, the APK
and the Linux builds, and its tag orders none of them.  So the add-on is
versioned by its own asset's file name, which ends in that date
("TGSpeechBox-v310rc4-2026.11.14.nvda-addon").
"""
from __future__ import annotations

import importlib.util
import json
import pathlib
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
_PATH = REPO / "nvdaAddon" / "synthDrivers" / "tgSpeechBox" / "updates.py"


def _load():
    spec = importlib.util.spec_from_file_location("_tgsb_updates", _PATH)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


updates = _load()
DL = "https://github.com/tgeczy/TGSpeechBox/releases/download"


def release(tag, addon=None, prerelease=False, draft=False, extra=()):
    assets = [{"name": n, "browser_download_url": f"{DL}/{tag}/{n}"} for n in extra]
    if addon:
        assets.append({"name": addon, "browser_download_url": f"{DL}/{tag}/{addon}"})
    return {"tag_name": tag, "prerelease": prerelease, "draft": draft, "assets": assets,
            "html_url": f"https://github.com/tgeczy/TGSpeechBox/releases/tag/{tag}"}


# Newest first, as GitHub lists them.
RELEASES = [
    release("v-311b2", "TGSpeechBox-v311b2-2027.1.20.nvda-addon", draft=True),
    release("v-311b1", "TGSpeechBox-v311b1-2027.1.9.nvda-addon", prerelease=True,
            extra=["TGSpeechSapiSetup-v311b1.exe", "TGSpeechBox-v311b1.apk"]),
    release("v-310", "TGSpeechBox-v310-2026.11.20.nvda-addon",
            extra=["TGSpeechSapiSetup-v310.exe", "tgspeechbox-3.10.0-py3-none-win_amd64.whl"]),
    release("v-310rc4", "TGSpeechBox-v310rc4-2026.11.14.nvda-addon", prerelease=True),
    release("v-310rc301", "TGSpeechBox-v310rc301.nvda-addon"),   # before the date was in the name
]


@pytest.mark.parametrize("text,expected", [
    ("2026.11.10", (2026, 11, 10)),
    ("2026.2.05", (2026, 2, 5)),                                   # an old manifest's zero-padded day
    ("TGSpeechBox-v310rc4-2026.11.14.nvda-addon", (2026, 11, 14)),
    ("TGSpeechBox-2026.11.14.nvda-addon", (2026, 11, 14)),
    ("TGSpeechBox-v310rc301.nvda-addon", None),                    # no date: not a version we can order
    ("", None), (None, None), ("nonsense", None),
])
def test_a_version_is_the_date(text, expected):
    assert updates.parse_version(text) == expected


@pytest.mark.parametrize("latest,installed,newer", [
    ("2026.11.14", "2026.11.10", True),
    ("2026.11.10", "2026.11.9", True),      # numbers, not strings: "10" is after "9"
    ("2027.1.9", "2026.11.20", True),
    ("2026.11.10", "2026.11.10", False),
    ("2026.11.9", "2026.11.10", False),     # older than what is running
    ("2026.2.5", "2026.2.05", False),       # the same day written two ways
    ("", "2026.11.10", False), ("2026.11.10", "", False), ("nonsense", "2026.11.10", False),
])
def test_newer_means_a_later_date(latest, installed, newer):
    # Unreadable on either side is "not newer": say nothing rather than
    # announce an update that may not exist.
    assert updates.is_newer(latest, installed) is newer


def test_the_stable_channel_takes_the_newest_full_release():
    version, page, url = updates.newest_addon(RELEASES, "stable")
    assert version == "2026.11.20"
    assert page.endswith("/releases/tag/v-310")
    assert url.endswith("/v-310/TGSpeechBox-v310-2026.11.20.nvda-addon")


def test_the_prerelease_channel_takes_the_newest_of_either_kind():
    version, page, url = updates.newest_addon(RELEASES, "prerelease")
    assert version == "2027.1.9"
    assert url.endswith("/v-311b1/TGSpeechBox-v311b1-2027.1.9.nvda-addon")


def test_the_prerelease_channel_still_gets_a_newer_stable_release():
    # Someone on prereleases must not be left on an RC when the final is out.
    rels = [r for r in RELEASES if r["tag_name"] not in ("v-311b1", "v-311b2")]
    assert updates.newest_addon(rels, "prerelease")[0] == "2026.11.20"


@pytest.mark.parametrize("channel", ["stable", "prerelease"])
def test_a_draft_is_never_offered(channel):
    assert updates.newest_addon(RELEASES, channel)[0] != "2027.1.20"


def test_an_unknown_channel_is_the_stable_one():
    assert updates.newest_addon(RELEASES, "nightly") == updates.newest_addon(RELEASES, "stable")
    assert updates.newest_addon(RELEASES, None) == updates.newest_addon(RELEASES, "stable")


def test_only_this_add_ons_own_file_counts():
    rels = [release("v-312", None, extra=["OtherSynth-2028.1.1.nvda-addon", "TGSpeechBox-v312.apk",
                                          "TGSpeechSapiSetup-v312-2028.1.1.exe"]),
            release("v-310", "TGSpeechBox-v310-2026.11.20.nvda-addon")]
    assert updates.newest_addon(rels, "stable")[0] == "2026.11.20"


def test_a_release_without_a_dated_add_on_offers_nothing():
    assert updates.newest_addon([RELEASES[-1]], "stable") == (None, None, None)
    assert updates.newest_addon([], "stable") == (None, None, None)
    assert updates.newest_addon(None, "prerelease") == (None, None, None)


def test_the_same_add_on_on_two_releases_links_to_the_newer_page():
    same = "TGSpeechBox-v310-2026.11.20.nvda-addon"
    rels = [release("v-310-hotfix-notes", same), release("v-310", same)]
    assert updates.newest_addon(rels, "stable")[1].endswith("/v-310-hotfix-notes")


def test_the_check_reads_githubs_list_through_the_opener():
    seen = []

    def opener(url):
        seen.append(url)
        return json.dumps(RELEASES).encode("utf-8")

    assert updates.latest_release("stable", opener=opener)[0] == "2026.11.20"
    assert updates.latest_release("prerelease", opener=opener)[0] == "2027.1.9"
    assert seen and all(u.startswith("https://api.github.com/repos/tgeczy/TGSpeechBox/releases") for u in seen)


def test_a_failed_check_gives_a_reason_and_no_update():
    def down(url):
        raise OSError("no route to host")

    version, reason, url = updates.latest_release("stable", opener=down)
    assert version is None and url is None and "no route to host" in reason
    version, reason, url = updates.latest_release("stable", opener=lambda u: b"not json")
    assert version is None and url is None and reason
    version, reason, url = updates.latest_release("stable", opener=lambda u: b"[]")
    assert version is None and url is None and reason


def test_a_single_release_object_is_read_as_a_list_of_one():
    one = json.dumps(RELEASES[2]).encode("utf-8")
    assert updates.latest_release("stable", opener=lambda u: one)[0] == "2026.11.20"


def test_never_on_a_secure_screen(monkeypatch):
    # On the sign-in screen NVDA runs as SYSTEM; the add-on must not reach
    # onto the network from there, whatever the caller does.
    called = []
    monkeypatch.setattr(updates, "_secure", lambda: True)
    version, reason, url = updates.latest_release("stable", opener=lambda u: called.append(u) or b"[]")
    assert version is None and url is None and "secure" in reason
    assert not called


def test_the_installed_version_is_the_manifests():
    # Outside NVDA the manifest two folders up is read; in the repository that
    # is nvdaAddon/manifest.ini.
    assert updates.parse_version(updates.installed_version()) is not None
