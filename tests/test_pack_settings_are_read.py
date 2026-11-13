"""Every setting a language pack sets is one the frontend reads (#178).

A key under ``settings:`` that the frontend does not know is skipped without
a word, and the pack goes on saying it does something.  The shipped packs
had fifteen of them:

- en-us had ``pitchFromProminence: true`` one indent too far left, outside
  its ``prominence:`` block, where nothing reads it;
- en-us, en-gb, en-au and tr set ``coarticulationRightSideScale``, hu four
  more coarticulation and glottal switches, es a ``glottalReinforcement``
  block inside ``allophoneRules``: no such settings exist;
- en-us, da and pt-br set ``postStopAspirationDurationMs`` and es
  ``tiedFromVowelDurationMs``, which Tuning.md lists as built in and not
  configurable (20 ms each);
- de kept four rules in a ``contextualRules:`` block; rules are read from
  ``allophoneRules.rules``.

None of them is applied now either: the lines are gone or are comments that
say so, and every language renders the frames it did.  This test reads the
keys out of mergeSettings (src/frontend/pack.cpp) the way
test_editor_settings_keys.py does, and each pack's ``settings:`` block, and
fails on a key nothing reads.
"""
from __future__ import annotations

import pathlib
import re

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
PACK_CPP = REPO / "src" / "frontend" / "pack.cpp"
PACKS = sorted((REPO / "packs" / "lang").glob("*.yaml"))


def merge_settings_source() -> str:
    """The text of mergeSettings(): from its first line to the next function."""
    text = PACK_CPP.read_text(encoding="utf-8")
    start = text.index("static void mergeSettings(")
    end = text.index("\nstatic ", start + 1)
    return text[start:end]


def frontend_keys() -> dict[tuple, set | None]:
    """What mergeSettings reads: {block path: its keys}, () being ``settings:``.

    A block is a map the code holds in a variable (``pr =
    settings.get("prominence")``) and reads with ``getNumFrom(*pr, "key", ..)``
    or ``pr->get("key")``.  A block the code walks key by key (``for (.. :
    mh->map)``) holds data, not settings: its value is None and it is not
    checked.  Two blocks that use the same variable name share their keys,
    which only makes the check more lenient.
    """
    src = merge_settings_source()
    read: dict[tuple, set | None] = {(): set(re.findall(r'\bget(?:Num|Bool|Str)\(\s*"(\w+)"', src))
                                     | set(re.findall(r'\bsettings\.get\("(\w+)"\)', src))
                                     | set(re.findall(r'From\(\s*settings\s*,\s*"(\w+)"', src))}
    var_paths: dict[str, list[tuple]] = {}
    for var, key in re.findall(r'\b(\w+)\s*=\s*settings\.get\("(\w+)"\)', src):
        var_paths.setdefault(var, []).append((key,))
    grew = True
    while grew:
        grew = False
        for var, parent, key in re.findall(r'\b(\w+)\s*=\s*(\w+)->get\("(\w+)"\)', src):
            for parent_path in list(var_paths.get(parent, [])):
                path = parent_path + (key,)
                if path not in var_paths.setdefault(var, []):
                    var_paths[var].append(path)
                    grew = True
    for var, paths in var_paths.items():
        v = re.escape(var)
        walked = re.search(r'\b%s->map\b' % v, src) is not None
        keys = set(re.findall(r'From\(\s*\*%s\s*,\s*"(\w+)"' % v, src)) | set(re.findall(r'\b%s->get\("(\w+)"\)' % v, src))
        for path in paths:
            if walked or not keys:
                read[path] = None
            elif read.get(path, set()) is not None:
                read.setdefault(path, set()).update(keys)
    return read


_KEY = re.compile(r'^( *)([A-Za-z_][A-Za-z0-9_]*):(?:\s|$)')


def pack_setting_keys(path: pathlib.Path) -> list[tuple[int, tuple]]:
    """(line number, key path) of every key in the pack's ``settings:`` block.

    Keys inside a list (the rules of allophoneRules, the words of
    numberExpansion) are data and are left out, as are comments.
    """
    out, stack, in_settings, list_indent = [], [], False, None
    for no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        indent = len(raw) - len(raw.lstrip(" "))
        if indent == 0:
            in_settings, stack, list_indent = raw.startswith("settings:"), [], None
            continue
        if not in_settings:
            continue
        if list_indent is not None and indent >= list_indent:
            continue
        list_indent = None
        if raw.lstrip().startswith("- "):
            list_indent = indent
            continue
        m = _KEY.match(raw)
        if not m:
            continue
        while stack and stack[-1][0] >= indent:
            stack.pop()
        stack.append((indent, m.group(2)))
        out.append((no, tuple(k for _, k in stack)))
    return out


def unread_keys(path: pathlib.Path) -> list[str]:
    read = frontend_keys()
    bad = []
    for no, key_path in pack_setting_keys(path):
        known = read.get(key_path[:-1], None)
        if known is not None and key_path[-1] not in known:
            bad.append(f"{path.name}:{no}: {'.'.join(key_path)}")
    return bad


def test_both_sides_are_found():
    # A regex that matches nothing would make every check below pass.
    read = frontend_keys()
    assert len(read[()]) > 300
    assert {"enabled", "pitchFromProminence", "primaryStressWeight"} <= read[("prominence",)]
    assert read[("trajectoryLimit", "maxHzPerMs")] is None      # walked: field names, not settings
    assert {"enabled", "rules"} <= read[("allophoneRules",)]
    en_us = [p for _, p in pack_setting_keys(REPO / "packs" / "lang" / "en-us.yaml")]
    assert len(en_us) > 200
    assert ("prominence", "primaryStressWeight") in en_us and ("legacyPitchMode",) in en_us
    assert len(PACKS) >= 25


@pytest.mark.parametrize("pack", PACKS, ids=[p.name for p in PACKS])
def test_every_setting_in_the_pack_is_read(pack):
    bad = unread_keys(pack)
    assert bad == [], (
        f"{len(bad)} setting(s) the frontend does not read (src/frontend/pack.cpp, mergeSettings): {bad}"
    )


@pytest.mark.parametrize("pack,key", [
    # The fifteen, by name: a comment may still mention one, a setting may not.
    ("en-us.yaml", "pitchFromProminence"), ("en-us.yaml", "coarticulationRightSideScale"),
    ("en-us.yaml", "postStopAspirationDurationMs"),
    ("en-gb.yaml", "coarticulationRightSideScale"), ("en-au.yaml", "coarticulationRightSideScale"),
    ("tr.yaml", "coarticulationRightSideScale"),
    ("hu.yaml", "coarticulationTransitionExtent"), ("hu.yaml", "coarticulationAlveolarBackVowelEnabled"),
    ("hu.yaml", "coarticulationLabializedFricativeFrontingEnabled"),
    ("hu.yaml", "positionalAllophonesGlottalReinforcementEnabled"),
    ("es.yaml", "glottalReinforcement"), ("es.yaml", "tiedFromVowelDurationMs"),
    ("da.yaml", "postStopAspirationDurationMs"), ("pt-br.yaml", "postStopAspirationDurationMs"),
    ("de.yaml", "contextualRules"),
])
def test_the_settings_named_in_the_issue_are_not_set(pack, key):
    paths = [p for _, p in pack_setting_keys(REPO / "packs" / "lang" / pack)]
    here = [p for p in paths if p[-1] == key and p[:-1] != ("prominence",)]
    assert here == [], f"{pack} still sets {key}, which nothing reads"


def test_a_prominence_key_outside_its_block_is_caught(tmp_path):
    # The en-us mistake, rebuilt: the same key is read inside prominence: and
    # is nothing directly under settings:.
    good = tmp_path / "good.yaml"
    good.write_text("settings:\n  prominence:\n    enabled: true\n    pitchFromProminence: true\n", encoding="utf-8")
    bad = tmp_path / "bad.yaml"
    bad.write_text("settings:\n  prominence:\n    enabled: true\n\n  pitchFromProminence: true\n", encoding="utf-8")
    assert unread_keys(good) == []
    assert unread_keys(bad) == ["bad.yaml:5: pitchFromProminence"]
