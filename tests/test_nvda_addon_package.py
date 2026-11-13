"""The packaged NVDA add-on is named after its own manifest's version.

The add-on's update check (#103) orders add-ons by the date at the end of the
asset's file name and compares it with the date in the installed manifest.
The two were typed by hand for every release.  If the name has no date, the
update is never offered; if the name's date is later than the manifest's
inside it, the add-on offers the same update again after installing it,
for ever.  tools/package_nvda_addon.py takes the name from the manifest, so
neither can happen, and this holds it to the updater's own rule.
"""
from __future__ import annotations

import importlib.util
import pathlib
import sys
import zipfile

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod
    spec.loader.exec_module(mod)
    return mod


updates = _load("_tgsb_updates_for_package", REPO / "nvdaAddon" / "synthDrivers" / "tgSpeechBox" / "updates.py")
package = _load("_tgsb_package_nvda_addon", REPO / "tools" / "package_nvda_addon.py")


def stage(tmp_path, version="2026.11.14", manifest=True):
    root = tmp_path / "stage"
    (root / "synthDrivers" / "tgSpeechBox" / "__pycache__").mkdir(parents=True)
    (root / "synthDrivers" / "tgSpeechBox" / "__init__.py").write_text("# driver\n", encoding="utf-8")
    (root / "synthDrivers" / "tgSpeechBox" / "__pycache__" / "__init__.cpython-313.pyc").write_bytes(b"\0")
    (root / "locale" / "hu" / "LC_MESSAGES").mkdir(parents=True)
    (root / "locale" / "hu" / "LC_MESSAGES" / "nvda.mo").write_bytes(b"\xde\x12\x04\x95")
    if manifest:
        (root / "manifest.ini").write_text(
            "name=tgSpeechBox\nsummary=tgSpeechBox\n" + (f"version={version}\n" if version else "")
            + "minimumNVDAVersion = 2024.1\n", encoding="utf-8")
    return root


@pytest.mark.parametrize("tag,version,name", [
    ("v310rc4", "2026.11.14", "TGSpeechBox-v310rc4-2026.11.14.nvda-addon"),
    ("v-310rc4", "2026.11.14", "TGSpeechBox-v310rc4-2026.11.14.nvda-addon"),   # the git tag's own spelling
    ("v310", "2026.12.1", "TGSpeechBox-v310-2026.12.1.nvda-addon"),
    ("v311b1", "2027.1.9", "TGSpeechBox-v311b1-2027.1.9.nvda-addon"),
])
def test_the_file_is_named_after_the_manifest(tmp_path, tag, version, name):
    out = package.package(stage(tmp_path, version), tag, tmp_path / "out")
    assert out.name == name
    # What the updater will make of it: its own asset, and exactly the version
    # the installed add-on will report from the manifest inside.
    assert updates.ADDON_ASSET.match(out.name)
    assert updates.parse_version(out.name) == updates.parse_version(version)
    assert not updates.is_newer(out.name, version)


def test_the_updater_offers_it_to_an_older_add_on_and_not_to_itself(tmp_path):
    out = package.package(stage(tmp_path, "2026.11.14"), "v310rc4", tmp_path / "out")
    rel = [{"tag_name": "v-310rc4", "prerelease": False, "draft": False, "html_url": "page",
            "assets": [{"name": out.name, "browser_download_url": "https://example.invalid/" + out.name}]}]
    version, _page, url = updates.newest_addon(rel, "stable")
    assert url.endswith(out.name)
    assert updates.is_newer(version, "2026.11.10")
    assert not updates.is_newer(version, "2026.11.14")


def test_the_files_sit_at_the_root_of_the_archive(tmp_path):
    # NVDA reads manifest.ini from the root; a folder zipped whole would bury it.
    out = package.package(stage(tmp_path), "v310rc4", tmp_path / "out")
    with zipfile.ZipFile(out) as z:
        names = set(z.namelist())
        assert "manifest.ini" in names
        assert "synthDrivers/tgSpeechBox/__init__.py" in names
        assert "locale/hu/LC_MESSAGES/nvda.mo" in names
        assert not [n for n in names if "__pycache__" in n or n.endswith(".pyc")]
        assert "version=2026.11.14" in z.read("manifest.ini").decode("utf-8")


@pytest.mark.parametrize("version", ["3.10", "2026.11", "v310rc4", "2026.13.1", "2026.2.30", ""])
def test_a_version_that_is_not_a_date_is_refused(tmp_path, version):
    # Better no file than one the updater can never offer.
    with pytest.raises(ValueError):
        package.package(stage(tmp_path, version), "v310rc4", tmp_path / "out")
    assert not (tmp_path / "out").exists()


def test_a_folder_without_a_manifest_is_refused(tmp_path):
    with pytest.raises(ValueError):
        package.package(stage(tmp_path, manifest=False), "v310rc4", tmp_path / "out")


@pytest.mark.parametrize("tag", ["", "v310 rc4", "../v310", "v310/rc4", "2026.11.14"])
def test_a_tag_that_would_spoil_the_name_is_refused(tmp_path, tag):
    # A second date in the name would be read as the version.
    with pytest.raises(ValueError):
        package.package(stage(tmp_path), tag, tmp_path / "out")
