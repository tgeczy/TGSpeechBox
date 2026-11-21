"""Each access key in the SAPI settings dialog reaches one control.

An access key (the letter after "&" in a label) is pressed with Alt.  Two
controls with the same letter share one key: Alt and the letter moves between
them instead of acting, and whichever is first wins.  In two of the
maintainer's other speech projects Alt+U ran "Unregister" where "Check for
updates" was meant.  This dialog has no such pair; the test keeps it so when
a control is added.
"""
from __future__ import annotations

import pathlib
import re
from collections import Counter

REPO = pathlib.Path(__file__).resolve().parents[1]
RC = REPO / "tools" / "tgsbSapiSettings" / "settings_app.rc"

_LABELLED = re.compile(r'^\s*(CONTROL|PUSHBUTTON|DEFPUSHBUTTON|LTEXT|GROUPBOX|AUTOCHECKBOX|CHECKBOX)\s+"((?:[^"]|"")*)"', re.M)


def _labels():
    text = RC.read_text(encoding="utf-8", errors="replace")
    return [(kind, label.replace('""', '"')) for kind, label in _LABELLED.findall(text)]


def _access_key(label):
    """The letter Alt reaches the control by, or None.  "&&" is a literal ampersand."""
    m = re.search(r"&([^&\s])", label.replace("&&", ""))
    return m.group(1).lower() if m else None


def test_the_dialog_was_read():
    labels = _labels()
    assert len(labels) >= 10, "the resource file was not parsed: has its layout changed?"
    assert any(_access_key(label) for _, label in labels), "no access key was found at all"


def test_no_two_controls_share_an_access_key():
    keys = Counter(k for k in (_access_key(label) for _, label in _labels()) if k)
    shared = {key: n for key, n in keys.items() if n > 1}
    assert not shared, f"access keys used by more than one control: {shared}"

