"""NVDA hands the driver Spanish ¿ and ¡, so it can pause there (#133).

NVDA's symbol dictionaries drop ¿ and ¡ before speech reaches a synthesizer.
The add-on ships a symbol dictionary (manifest [symbolDictionaries],
NVDA 2024.4 and later) that keeps them at every level; symbols_check.py runs
NVDA's own characterProcessing with and without it.
"""
from __future__ import annotations

import os
import pathlib
import subprocess
import sys

from conftest import NVDA_DIR

ROOT = pathlib.Path(__file__).resolve().parents[2]
LOCALE = ROOT / "nvdaAddon" / "locale"


def test_nvda_keeps_inverted_marks_with_the_addon_dictionary():
    run = subprocess.run(
        [sys.executable, "-S", str(pathlib.Path(__file__).with_name("symbols_check.py")), str(NVDA_DIR), str(LOCALE)],
        capture_output=True, text=True, encoding="utf-8", timeout=120,
        env={**os.environ, "PYTHONIOENCODING": "utf-8"})
    assert run.returncode == 0, run.stdout + run.stderr


def test_manifest_declares_the_dictionary():
    text = (ROOT / "nvdaAddon" / "manifest.ini").read_text(encoding="utf-8")
    assert "[[invertedmarks]]" in text and "mandatory = true" in text
    for loc in ("en", "es"):
        assert (LOCALE / loc / "symbols-invertedmarks.dic").is_file()
