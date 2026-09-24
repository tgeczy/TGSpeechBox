"""pytest fixtures for the TGSpeechBox frontend test suite.

Auto-detects the built nvspFrontend.dll. Bitness must match Python's bitness:
- 32-bit Python -> build-x86-nvda/MinSizeRel/nvspFrontend.dll
- 64-bit Python -> build-x64-nvda/MinSizeRel/nvspFrontend.dll
"""
from __future__ import annotations

import os
import pathlib
import sys

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT / "tests"))


def _is_32bit_python() -> bool:
    return sys.maxsize <= 2**32


def _find_dll() -> pathlib.Path:
    arch = "x86" if _is_32bit_python() else "x64"
    candidates = [
        REPO_ROOT / f"build-{arch}-nvda" / "MinSizeRel" / "nvspFrontend.dll",
        REPO_ROOT / f"build-{arch}-nvda" / "Release" / "nvspFrontend.dll",
        REPO_ROOT / f"build-{arch}" / "MinSizeRel" / "nvspFrontend.dll",
    ]
    for c in candidates:
        if c.exists():
            return c
    candidates_str = "\n  ".join(str(c) for c in candidates)
    pytest.exit(
        f"\nnvspFrontend.dll not found for {arch} Python. Looked at:\n  "
        f"{candidates_str}\n\n"
        f"Build with one of:\n"
        f"  cmake --build build-{arch}-nvda --config MinSizeRel\n",
        returncode=2,
    )


def pytest_sessionstart(session):
    """Build the DLLs the tests load before anything is collected.

    The tests load build-<arch>-nvda/MinSizeRel's nvspFrontend.dll and
    speechPlayer.dll, and tests/nvda stages them into its add-on copy.  A
    source change without a rebuild would otherwise be tested against the
    old engine, silently.  An up-to-date build takes a few seconds.  Set
    TGSB_NO_BUILD=1 to test whatever is built.
    """
    if os.environ.get("TGSB_NO_BUILD"):
        return
    import shutil
    import subprocess
    arch = "x86" if _is_32bit_python() else "x64"
    build_dir = REPO_ROOT / f"build-{arch}-nvda"
    cmake = shutil.which("cmake")
    if not cmake or not (build_dir / "CMakeCache.txt").is_file():
        return  # nothing to build with; _find_dll() reports a missing DLL
    result = subprocess.run(
        [cmake, "--build", str(build_dir), "--config", "MinSizeRel",
         "--target", "nvspFrontend", "speechPlayer"],
        capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode != 0:
        pytest.exit(f"\nbuilding the DLLs under test failed (cmake --build {build_dir}):\n"
                    f"{result.stdout[-3000:]}\n{result.stderr[-2000:]}", returncode=2)


def _find_pack_dir() -> pathlib.Path:
    """Returns the directory containing the 'packs' subfolder (i.e. repo root)."""
    p = REPO_ROOT / "packs"
    if not p.is_dir():
        pytest.exit(f"\npacks/ folder not found at {p}\n", returncode=2)
    return REPO_ROOT


@pytest.fixture(scope="session")
def frontend():
    """Loads nvspFrontend.dll once for the whole session.

    Tests that need a per-language handle should depend on `language_handle`
    or one of the language-specific fixtures (`es_mx`, `es_es`, `en_us`, ...).
    """
    from _test_frontend import TestFrontend
    fe = TestFrontend(str(_find_dll()))
    return fe


@pytest.fixture(scope="session")
def pack_dir() -> str:
    return str(_find_pack_dir())


def _make_handle_fixture(lang_tag: str):
    """Helper: build a function-scoped fixture that yields (frontend, handle) for a language."""
    @pytest.fixture
    def _fixture(frontend, pack_dir):
        h = frontend.create_handle(pack_dir)
        try:
            frontend.set_language(h, lang_tag)
            yield (frontend, h)
        finally:
            frontend.destroy_handle(h)
    _fixture.__name__ = lang_tag.replace("-", "_")
    return _fixture


# Per-language handle fixtures. Add a new one if you start testing a language.
es_mx = _make_handle_fixture("es-mx")
es_es = _make_handle_fixture("es-es")
en_us = _make_handle_fixture("en-us")
en_gb = _make_handle_fixture("en-gb")
hu = _make_handle_fixture("hu")
