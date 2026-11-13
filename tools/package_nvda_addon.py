"""Package a staged NVDA add-on folder as a .nvda-addon named after its manifest.

    python tools/package_nvda_addon.py <staged folder> <tag> [<output folder>]

The staged folder is the add-on as NVDA will unpack it: manifest.ini,
synthDrivers/, globalPlugins/ and locale/ directly inside.  The file written is

    TGSpeechBox-<tag>-<manifest version>.nvda-addon      (TGSpeechBox-v310rc4-2026.11.14.nvda-addon)

The add-on's update check (synthDrivers/tgSpeechBox/updates.py, #103) orders
add-ons by the date at the end of that name and compares it with the version
in the installed manifest, so the name is taken from the manifest here rather
than typed: a name without the date is never offered as an update, and a name
whose date is later than the manifest inside it is offered again after every
install.  tests/test_nvda_addon_package.py holds this to the updater's rule.
"""
from __future__ import annotations

import datetime
import pathlib
import re
import sys
import zipfile

_VERSION_LINE = re.compile(r"^\s*version\s*=\s*(.*?)\s*$", re.M)
_VERSION = re.compile(r"^(\d{4})\.(\d{1,2})\.(\d{1,2})$")
_TAG = re.compile(r"^v-?([0-9a-z]+)$")
_SKIP_DIRS = {"__pycache__"}
_SKIP_SUFFIXES = {".pyc", ".pyo"}


def manifest_version(stage: pathlib.Path) -> str:
    """-> the version in <stage>/manifest.ini; ValueError unless it is a real date, YYYY.M.D."""
    manifest = stage / "manifest.ini"
    if not manifest.is_file():
        raise ValueError(f"no manifest.ini in {stage}")
    found = _VERSION_LINE.search(manifest.read_text(encoding="utf-8"))
    version = found.group(1).strip("\"'") if found else ""
    parts = _VERSION.match(version)
    if not parts:
        raise ValueError(f"manifest version {version!r} is not a date (YYYY.M.D); the update check could never offer it")
    try:
        datetime.date(*(int(p) for p in parts.groups()))
    except ValueError:
        raise ValueError(f"manifest version {version!r} is not a real date") from None
    return version


def addon_name(tag: str, version: str) -> str:
    """-> the asset's file name for a release tag ("v310rc4" or "v-310rc4") and a manifest version."""
    found = _TAG.match(tag or "")
    if not found:
        raise ValueError(f"tag {tag!r} should look like v310rc4")
    return f"TGSpeechBox-v{found.group(1)}-{version}.nvda-addon"


def package(stage, tag, out_dir) -> pathlib.Path:
    """Zip the staged add-on, files at the root of the archive, and return the file written."""
    stage, out_dir = pathlib.Path(stage), pathlib.Path(out_dir)
    name = addon_name(tag, manifest_version(stage))
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / name
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in sorted(stage.rglob("*")):
            rel = path.relative_to(stage)
            if not path.is_file() or _SKIP_DIRS.intersection(rel.parts) or path.suffix in _SKIP_SUFFIXES:
                continue
            z.write(path, rel.as_posix())
    return out


def main(argv) -> int:
    if len(argv) not in (3, 4):
        print(__doc__)
        return 2
    try:
        out = package(argv[1], argv[2], argv[3] if len(argv) == 4 else ".")
    except ValueError as e:
        print(f"not packaged: {e}", file=sys.stderr)
        return 1
    print(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
