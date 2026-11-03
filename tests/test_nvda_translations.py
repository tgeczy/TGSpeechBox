"""The NVDA add-on's translations stay in step with its interface (#154).

Leonardo found buttons in English in the Spanish interface: the
"Save/Import Voice Profile Sliders" and "Export/Import User Dictionary"
buttons were added after the .po files were last synced, so no translator
ever saw them.  And the Hungarian and Brazilian Portuguese .po files had
line breaks typed inside their strings, which gettext tools reject, so
Poedit could not open them and their .mo files had fallen behind.

These tests need no NVDA and no translator: every string the add-on
passes to _() is in every .po (empty until someone translates it), every
.po is well-formed, and every .mo is the compiled form of its .po.
"""
from __future__ import annotations

import ast
import gettext
import pathlib

import pytest

REPO = pathlib.Path(__file__).resolve().parent.parent
ADDON = REPO / "nvdaAddon"
LANGS = sorted(p.parent.parent.name for p in ADDON.glob("locale/*/LC_MESSAGES/nvda.po"))
_KEYWORDS = ("msgid ", "msgstr ", "msgctxt ", "msgid_plural ", "msgstr[")


def _po_path(lang):
    return ADDON / "locale" / lang / "LC_MESSAGES" / "nvda.po"


def _po_lines(lang):
    return _po_path(lang).read_text(encoding="utf-8").splitlines()


def _unquote(text, where):
    s = text.strip()
    assert len(s) >= 2 and s[0] == '"' and s[-1] == '"', f"{where}: not one quoted string: {text!r}"
    return ast.literal_eval(s)


def _parse_po(lang):
    """msgid -> (msgstr, fuzzy).  Strict: a line is blank, a comment, a
    keyword with one quoted string, or one quoted string continuing it."""
    catalog = {}
    entry, field, fuzzy = {}, None, False

    def finish():
        nonlocal entry, field, fuzzy
        if "msgid" in entry:
            assert entry["msgid"] not in catalog, f"{lang}: msgid twice: {entry['msgid']!r}"
            catalog[entry["msgid"]] = (entry.get("msgstr", ""), fuzzy)
        entry, field, fuzzy = {}, None, False

    for n, line in enumerate(_po_lines(lang), 1):
        where = f"{lang} nvda.po line {n}"
        if not line.strip():
            finish()
        elif line.startswith("#"):
            if field is not None:
                finish()
            if line.startswith("#,") and "fuzzy" in line:
                fuzzy = True
        elif line.startswith(_KEYWORDS):
            key, rest = line.split(" ", 1)
            if key == "msgid" and field is not None:
                finish()
            field = key
            entry[key] = _unquote(rest, where)
        else:
            assert field is not None, f"{where}: text outside an entry: {line!r}"
            entry[field] += _unquote(line, where)
    finish()
    return catalog


def _interface_strings():
    """Every literal string the add-on passes to _(), with where it is."""
    found = {}
    for path in sorted(ADDON.rglob("*.py")):
        tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
        for node in ast.walk(tree):
            if (isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == "_"
                    and node.args and isinstance(node.args[0], ast.Constant)
                    and isinstance(node.args[0].value, str)):
                found.setdefault(node.args[0].value, f"{path.relative_to(ADDON)}:{node.lineno}")
    return found


_PANEL = ADDON / "synthDrivers" / "tgSpeechBox" / "langPackSettingsPanel.py"


def _panel_setting_labels():
    """The labels the language-pack settings panel shows for setting keys.
    It builds each from the key (_autoDisplayName: "rateCompEnabled" ->
    "Rate Comp Enabled") and translates that (_displayNameForKey), so they
    are not literal _() strings.  Keys: default.yaml's settings
    (langPackYaml.listKnownSettingKeys) plus the panel's _extraKeys."""
    import importlib.util
    spec = importlib.util.spec_from_file_location(
        "_tgsb_langPackYaml", ADDON / "synthDrivers" / "tgSpeechBox" / "langPackYaml.py")
    yaml_mod = importlib.util.module_from_spec(spec)
    import sys
    sys.modules[spec.name] = yaml_mod  # its dataclasses look themselves up there
    spec.loader.exec_module(yaml_mod)
    keys = set(yaml_mod.listKnownSettingKeys(str(REPO / "packs")))

    source = _PANEL.read_text(encoding="utf-8")
    tree = ast.parse(source)
    helpers = {}
    for node in ast.walk(tree):
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "_extraKeys"
                                                for t in node.targets):
            keys |= {e.value for e in node.value.elts if isinstance(e, ast.Constant)}
        if isinstance(node, ast.FunctionDef) and node.name in ("_splitCamelCase", "_prettifyField",
                                                               "_autoDisplayName"):
            helpers[node.name] = ast.get_source_segment(source, node)
    assert len(helpers) == 3, f"label helpers not found: {sorted(helpers)}"
    import re
    ns = {"_re": re}
    exec("\n\n".join(helpers.values()), ns)
    return {ns["_autoDisplayName"](k): k for k in keys}


def test_there_are_translations_to_check():
    assert LANGS, "no nvdaAddon/locale/*/LC_MESSAGES/nvda.po found"
    assert len(_interface_strings()) > 100
    assert len(_panel_setting_labels()) > 200


@pytest.mark.parametrize("lang", LANGS)
def test_every_setting_label_is_in_the_po(lang):
    catalog = _parse_po(lang)
    missing = {label: key for label, key in _panel_setting_labels().items() if label not in catalog}
    assert not missing, f"{lang}: {len(missing)} setting labels no translator has seen:\n" + "\n".join(
        f"  {key}  {label!r}" for label, key in sorted(missing.items()))


@pytest.mark.parametrize("lang", LANGS)
def test_the_po_file_is_well_formed(lang):
    # A line break typed inside a string, instead of \n, breaks Poedit and msgfmt.
    catalog = _parse_po(lang)
    assert "" in catalog, f"{lang}: no header entry"
    assert "charset=UTF-8" in catalog[""][0]


@pytest.mark.parametrize("lang", LANGS)
def test_every_interface_string_is_in_the_po(lang):
    catalog = _parse_po(lang)
    missing = {s: where for s, where in _interface_strings().items() if s not in catalog}
    assert not missing, f"{lang}: {len(missing)} strings no translator has seen:\n" + "\n".join(
        f"  {where}  {s!r}" for s, where in sorted(missing.items(), key=lambda kv: kv[1]))


def _placeholders(s):
    import re
    return sorted(re.findall(r"\{[^{}]*\}|%[sdr]", s))


@pytest.mark.parametrize("lang", LANGS)
def test_a_translation_keeps_its_placeholders(lang):
    # The code fills {} with .format(); a translation without it drops the
    # file name or the error message from the dialog.
    bad = {k: v for k, (v, fuzzy) in _parse_po(lang).items()
           if k and v and not fuzzy and _placeholders(k) != _placeholders(v)}
    assert not bad, f"{lang}: placeholders lost or added:\n" + "\n".join(
        f"  {k!r}\n    -> {v!r}" for k, v in bad.items())


def _ini(path):
    """NVDA's manifest.ini, enough of it for the checks below: top-level
    keys, and the [symbolDictionaries] [[name]] subsections."""
    top, dicts, section, sub = {}, {}, None, None
    for n, raw in enumerate(path.read_text(encoding="utf-8-sig").splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[["):
            sub = line.strip("[]")
            dicts.setdefault(sub, {})
        elif line.startswith("["):
            section, sub = line.strip("[]"), None
        else:
            assert "=" in line, f"{path}: line {n} is not key = value: {raw!r}"
            k, v = (x.strip() for x in line.split("=", 1))
            if section is None:
                top[k] = v
            else:
                assert section == "symbolDictionaries" and sub, f"{path}: line {n} outside a known section"
                dicts[sub][k] = v
    return top, dicts


MANIFEST_LANGS = sorted(p.parent.name for p in ADDON.glob("locale/*/manifest.ini"))


@pytest.mark.parametrize("lang", MANIFEST_LANGS)
def test_a_translated_manifest_says_only_what_nvda_reads(lang):
    """NVDA takes summary, description and changelog from
    locale/<lang>/manifest.ini, and a symbol dictionary's displayName
    (addonHandler.AddonManifest, NVDA 2026.2); anything else there is
    ignored, so it is a mistake."""
    top, dicts = _ini(ADDON / "locale" / lang / "manifest.ini")
    _, main_dicts = _ini(ADDON / "manifest.ini")
    assert set(top) <= {"summary", "description", "changelog"}, f"{lang}: {sorted(top)}"
    assert top.get("summary") and top.get("description"), f"{lang}: summary and description are needed"
    assert set(dicts) <= set(main_dicts), f"{lang}: dictionaries the add-on does not have: {sorted(dicts)}"
    for name, fields in dicts.items():
        assert set(fields) == {"displayName"} and fields["displayName"], f"{lang} {name}: {fields}"


@pytest.mark.parametrize("lang", LANGS)
def test_the_mo_is_the_compiled_po(lang):
    # NVDA reads the .mo; a .po edited without recompiling changes nothing.
    want = {k: v for k, (v, fuzzy) in _parse_po(lang).items() if k and v and not fuzzy}
    with open(_po_path(lang).with_suffix(".mo"), "rb") as f:
        have = {k: v for k, v in gettext.GNUTranslations(f)._catalog.items() if k}
    assert have == want, (
        f"{lang}: nvda.mo is not nvda.po compiled: "
        f"{len(set(want) - set(have))} translations missing from the .mo, "
        f"{len(set(have) - set(want))} in the .mo only, "
        f"{sum(1 for k in set(want) & set(have) if want[k] != have[k])} different")


def _panel_title():
    for node in ast.walk(ast.parse(_PANEL.read_text(encoding="utf-8"))):
        if isinstance(node, ast.ClassDef) and node.name == "TGSpeechBoxLanguagePacksPanel":
            for stmt in node.body:
                if isinstance(stmt, ast.Assign) and any(getattr(t, "id", None) == "title" for t in stmt.targets):
                    return stmt.value.args[0].value
    raise AssertionError("the settings panel's title was not found")


@pytest.mark.parametrize("lang", LANGS)
def test_the_settings_category_is_the_add_on_name(lang):
    """Leonardo (#154): the category holds voice profiles, updates and the
    source code link as well as the language packs, so it is called
    TGSpeechBox, in every language."""
    assert _panel_title() == "TGSpeechBox"
    with open(_po_path(lang).with_suffix(".mo"), "rb") as f:
        assert gettext.GNUTranslations(f).gettext("TGSpeechBox") == "TGSpeechBox"
