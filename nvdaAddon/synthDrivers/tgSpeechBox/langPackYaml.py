"""Minimal YAML helpers for NV Speech Player language packs.

NV Speech Player language packs are YAML files (packs/lang/*.yaml). The full
YAML grammar is intentionally *not* implemented here; we only need to read and
write the ``settings:`` mapping (including nested keys).

Why not PyYAML?
    NVDA add-ons can't assume third-party Python dependencies are available.

So this module sticks to a conservative, line-based approach that preserves
most of the original file formatting.

Nested keys are supported and flattened with dot notation:
    settings:
      boundarySmoothing:
        enabled: true
        vowelToStopFadeMs: 25

    -> {"boundarySmoothing.enabled": "true", "boundarySmoothing.vowelToStopFadeMs": "25"}
"""

from __future__ import annotations

import os
import re
from dataclasses import dataclass, field
from typing import Dict, Iterable, List, Optional, Tuple


_SETTINGS_HEADER_RE = re.compile(r"^settings\s*:\s*(?:#.*)?$")
_KEY_VALUE_RE = re.compile(
    r"^(?P<indent>\s*)(?P<key>[-A-Za-z0-9_]+)\s*:\s*(?P<value>.*?)(?P<comment>\s+#.*)?$"
)
_KEY_ONLY_RE = re.compile(
    r"^(?P<indent>\s*)(?P<key>[-A-Za-z0-9_]+)\s*:\s*(?P<comment>#.*)?$"
)
_NUM_RE = re.compile(r"^-?(?:\d+)(?:\.\d+)?$")


def normalizeLangTag(tag: str) -> str:
    """Normalize a language tag to the form used by pack filenames.

    - NVDA sometimes uses underscores and upper-case region parts (e.g. en_US).
    - Pack files use hyphen-separated tags (e.g. en-us.yaml).
    """
    tag = (tag or "").strip()
    if not tag:
        return "default"
    # Packs use lowercase, hyphen-separated tags.
    tag = tag.replace("_", "-")
    tag = tag.lower()
    return tag


def getLangDir(packsDir: str) -> str:
    return os.path.join(packsDir, "lang")


def iterLangTagChain(langTag: str) -> Iterable[str]:
    """Yield the inheritance chain used by the frontend.

    For example:
        "en-us-nyc" -> ["default", "en", "en-us", "en-us-nyc"]
    """
    langTag = normalizeLangTag(langTag)
    yield "default"
    if langTag == "default":
        return
    parts = langTag.split("-")
    for i in range(1, len(parts) + 1):
        yield "-".join(parts[:i])


def langYamlPath(packsDir: str, langTag: str) -> str:
    langTag = normalizeLangTag(langTag)
    return os.path.join(getLangDir(packsDir), f"{langTag}.yaml")


@dataclass
class SettingsSection:
    settings: Dict[str, str]
    """Mapping of dotted key path -> raw scalar string."""

    # Line indices that bound the mapping in the source file.
    startLine: Optional[int] = None
    endLine: Optional[int] = None
    """Slice range [startLine, endLine) within the original file lines."""

    keyLineIndex: Optional[Dict[str, int]] = None
    """Maps dotted key paths to their line index (within the file)."""


def _stripBom(text: str) -> str:
    # Some editors may write UTF-8 BOM.
    return text[1:] if text.startswith("\ufeff") else text


def _readFileText(path: str) -> str:
    with open(path, "r", encoding="utf-8") as f:
        return _stripBom(f.read())


def _writeFileText(path: str, text: str) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def _getIndentLevel(line: str) -> int:
    """Return the number of leading spaces (tabs count as 2 spaces)."""
    count = 0
    for ch in line:
        if ch == ' ':
            count += 1
        elif ch == '\t':
            count += 2
        else:
            break
    return count


def parseSettingsSectionFromText(text: str) -> SettingsSection:
    """Parse the ``settings:`` mapping from YAML text, including nested keys.

    Nested keys are flattened with dot notation:
        boundarySmoothing:
          enabled: true
        -> "boundarySmoothing.enabled": "true"
    """
    lines = text.splitlines()
    settings: Dict[str, str] = {}
    keyLineIndex: Dict[str, int] = {}
    startLine: Optional[int] = None
    endLine: Optional[int] = None

    # Find a top-level "settings:" header.
    for i, line in enumerate(lines):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if line.startswith(" ") or line.startswith("\t"):
            continue
        if _SETTINGS_HEADER_RE.match(line.strip()):
            startLine = i
            break

    if startLine is None:
        return SettingsSection(settings=settings, startLine=None, endLine=None, keyLineIndex=None)

    # Parse indented key/value pairs, tracking nesting via indentation.
    # Stack of (indent_level, key_prefix)
    baseIndent: Optional[int] = None
    indentStack: List[Tuple[int, str]] = []  # (indent, prefix including trailing dot or empty)

    i = startLine + 1
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        # Skip blank lines and comments
        if not stripped or stripped.startswith("#"):
            i += 1
            continue

        # End of the settings mapping when indentation returns to column 0.
        if not (line.startswith(" ") or line.startswith("\t")):
            break

        indent = _getIndentLevel(line)

        # Set base indent from first real line
        if baseIndent is None:
            baseIndent = indent

        # Pop stack entries that are at same or deeper indent than current
        while indentStack and indentStack[-1][0] >= indent:
            indentStack.pop()

        # Build current prefix from stack
        currentPrefix = indentStack[-1][1] if indentStack else ""

        # Check for key: value or key: (nested mapping)
        m = _KEY_VALUE_RE.match(line)
        if m:
            key = m.group("key")
            val = (m.group("value") or "").strip()

            if val:
                # This is a leaf key with a value
                fullKey = currentPrefix + key
                settings[fullKey] = val
                keyLineIndex[fullKey] = i
            else:
                # This is a nested mapping (key with no value)
                # Push onto stack for subsequent lines
                indentStack.append((indent, currentPrefix + key + "."))

        i += 1

    endLine = i
    return SettingsSection(
        settings=settings,
        startLine=startLine,
        endLine=endLine,
        keyLineIndex=keyLineIndex,
    )


def parseSettingsSectionFromFile(path: str) -> SettingsSection:
    if not os.path.isfile(path):
        return SettingsSection(settings={}, startLine=None, endLine=None, keyLineIndex=None)
    try:
        return parseSettingsSectionFromText(_readFileText(path))
    except Exception:
        # Corrupt/unsupported YAML; treat as empty.
        return SettingsSection(settings={}, startLine=None, endLine=None, keyLineIndex=None)


def getEffectiveSettings(packsDir: str, langTag: str) -> Dict[str, str]:
    """Return effective settings for a language tag (merged by inheritance)."""
    effective: Dict[str, str] = {}
    for tag in iterLangTagChain(langTag):
        sec = parseSettingsSectionFromFile(langYamlPath(packsDir, tag))
        effective.update(sec.settings)
    return effective


def getEffectiveSettingValue(packsDir: str, langTag: str, key: str) -> Optional[str]:
    """Return the effective value for a given setting key (supports dotted paths).

    Returns None if the key is not present in any pack layer.
    """
    key = (key or "").strip()
    if not key:
        return None
    val: Optional[str] = None
    for tag in iterLangTagChain(langTag):
        sec = parseSettingsSectionFromFile(langYamlPath(packsDir, tag))
        if key in sec.settings:
            val = sec.settings[key]
    return val


def getSettingSource(packsDir: str, langTag: str, key: str) -> Optional[str]:
    """Return which pack layer provides the effective value for ``key``.

    Returns the tag ("default", "en", "en-us", ...) or None if not found.
    """
    key = (key or "").strip()
    if not key:
        return None
    found: Optional[str] = None
    for tag in iterLangTagChain(langTag):
        sec = parseSettingsSectionFromFile(langYamlPath(packsDir, tag))
        if key in sec.settings:
            found = tag
    return found


def listKnownSettingKeys(packsDir: str) -> List[str]:
    """List known setting keys (including nested dotted paths).

    Uses packs/lang/default.yaml as the source of truth.
    """
    defaultPath = langYamlPath(packsDir, "default")
    sec = parseSettingsSectionFromFile(defaultPath)
    keys = sorted(sec.settings.keys())
    return keys


def _formatYamlScalar(value) -> str:
    """Format a value as a safe-ish YAML scalar."""
    # Preserve values already passed as a bool.
    if isinstance(value, bool):
        return "true" if value else "false"

    if value is None:
        return "null"

    s = str(value)
    s = s.strip()

    if not s:
        return '""'

    lower = s.lower()
    if lower in {"true", "false", "null", "~"}:
        return lower

    # YAML 1.1 boolean-like scalars; quote to avoid accidental coercion.
    if lower in {"yes", "no", "on", "off"}:
        return f'"{lower}"'

    if _NUM_RE.match(s):
        return s

    # If the user already quoted it, keep it.
    if len(s) >= 2 and s[0] == s[-1] and s[0] in ("'", '"'):
        return s

    # Quote if it contains YAML-significant characters.
    if any(ch in s for ch in [":", "#", "{", "}", "[", "]", ",", "&", "*", "!", "|", ">", "@", "`"]):
        escaped = s.replace("\\", "\\\\").replace('"', '\\"')
        return f'"{escaped}"'

    return s


def _findNestedKeyLine(lines: List[str], startLine: int, endLine: int, keyPath: str) -> Optional[Tuple[int, str]]:
    """Find the line index for a dotted key path if it exists.
    
    Returns (line_index, indent_string) or None if not found.
    """
    parts = keyPath.split(".")
    if not parts:
        return None

    # For each part, we search for the key at the expected nesting level
    searchStart = startLine + 1
    searchEnd = endLine
    currentIndentLevel = 0

    for depth, part in enumerate(parts):
        expectedIndent = 2 * (depth + 1)  # 2 spaces per level
        found = False

        i = searchStart
        while i < searchEnd:
            line = lines[i]
            stripped = line.strip()

            if not stripped or stripped.startswith("#"):
                i += 1
                continue

            lineIndent = _getIndentLevel(line)

            # If indent is less than expected, we've exited this scope
            if lineIndent < expectedIndent and stripped:
                break

            if lineIndent == expectedIndent:
                m = _KEY_VALUE_RE.match(line)
                if m and m.group("key") == part:
                    if depth == len(parts) - 1:
                        # This is the leaf key
                        return (i, m.group("indent") or "  " * (depth + 1))
                    else:
                        # This is an intermediate key, search inside it
                        val = (m.group("value") or "").strip()
                        if not val:  # It's a mapping
                            searchStart = i + 1
                            # Find end of this mapping
                            j = i + 1
                            while j < searchEnd:
                                nextLine = lines[j]
                                nextStripped = nextLine.strip()
                                if nextStripped and not nextStripped.startswith("#"):
                                    nextIndent = _getIndentLevel(nextLine)
                                    if nextIndent <= expectedIndent:
                                        break
                                j += 1
                            searchEnd = j
                            found = True
                            break
            i += 1

        if not found and depth < len(parts) - 1:
            return None

    return None


def _insertNestedKey(lines: List[str], startLine: int, endLine: int, keyPath: str, value: str) -> List[str]:
    """Insert a new nested key, creating parent mappings as needed.
    
    Returns the modified lines list.
    """
    parts = keyPath.split(".")
    leafKey = parts[-1]
    yamlValue = _formatYamlScalar(value)

    if len(parts) == 1:
        # Simple flat key
        indent = "  "
        lines.insert(endLine, f"{indent}{leafKey}: {yamlValue}\n")
        return lines

    # For nested keys, find or create each level
    searchStart = startLine + 1
    searchEnd = endLine
    insertPoint = endLine
    currentIndent = "  "

    for depth, part in enumerate(parts[:-1]):
        expectedIndent = 2 * (depth + 1)
        expectedIndentStr = "  " * (depth + 1)
        found = False
        foundLine = -1

        i = searchStart
        while i < searchEnd:
            line = lines[i]
            stripped = line.strip()

            if not stripped or stripped.startswith("#"):
                i += 1
                continue

            lineIndent = _getIndentLevel(line)

            if lineIndent < expectedIndent and stripped:
                # Past this scope, key doesn't exist
                break

            if lineIndent == expectedIndent:
                m = _KEY_VALUE_RE.match(line)
                if m and m.group("key") == part:
                    val = (m.group("value") or "").strip()
                    if not val:  # It's a mapping
                        searchStart = i + 1
                        # Find end of this mapping
                        j = i + 1
                        while j < searchEnd:
                            nextLine = lines[j]
                            nextStripped = nextLine.strip()
                            if nextStripped and not nextStripped.startswith("#"):
                                nextIndent = _getIndentLevel(nextLine)
                                if nextIndent <= expectedIndent:
                                    break
                            j += 1
                        searchEnd = j
                        insertPoint = j
                        currentIndent = "  " * (depth + 2)
                        found = True
                        foundLine = i
                        break
            i += 1

        if not found:
            # Need to create this level
            newLine = f"{expectedIndentStr}{part}:\n"
            lines.insert(insertPoint, newLine)
            searchEnd += 1
            insertPoint += 1
            searchStart = insertPoint
            currentIndent = "  " * (depth + 2)

    # Now insert the leaf key
    lines.insert(insertPoint, f"{currentIndent}{leafKey}: {yamlValue}\n")
    return lines


def upsertSetting(packsDir: str, langTag: str, key: str, value) -> None:
    """Insert or update ``settings.<key>`` in the most specific language pack file.

    Supports dotted key paths for nested settings (e.g., "boundarySmoothing.enabled").
    If the target language YAML does not exist, it will be created.
    """
    key = (key or "").strip()
    if not key:
        raise ValueError("key is required")

    langTag = normalizeLangTag(langTag)
    targetPath = langYamlPath(packsDir, langTag)
    yamlValue = _formatYamlScalar(value)

    # Handle dotted keys
    parts = key.split(".")
    leafKey = parts[-1]

    if not os.path.isfile(targetPath):
        # Create new file with proper nesting
        if len(parts) == 1:
            _writeFileText(targetPath, f"settings:\n  {key}: {yamlValue}\n")
        else:
            # Build nested structure
            content = "settings:\n"
            for depth, part in enumerate(parts[:-1]):
                content += "  " * (depth + 1) + f"{part}:\n"
            content += "  " * len(parts) + f"{leafKey}: {yamlValue}\n"
            _writeFileText(targetPath, content)
        return

    text = _readFileText(targetPath)
    lines = text.splitlines(True)  # keep line endings
    sec = parseSettingsSectionFromText(text)

    # If no settings section exists, append one at the end.
    if sec.startLine is None or sec.endLine is None:
        if text and not text.endswith("\n"):
            lines.append("\n")
        if len(parts) == 1:
            lines.append("settings:\n")
            lines.append(f"  {key}: {yamlValue}\n")
        else:
            lines.append("settings:\n")
            for depth, part in enumerate(parts[:-1]):
                lines.append("  " * (depth + 1) + f"{part}:\n")
            lines.append("  " * len(parts) + f"{leafKey}: {yamlValue}\n")
        _writeFileText(targetPath, "".join(lines))
        return

    # Check if key already exists (exact dotted path match)
    keyLineIndex = (sec.keyLineIndex or {}).get(key)
    if keyLineIndex is not None:
        m = _KEY_VALUE_RE.match(lines[keyLineIndex].rstrip("\n"))
        indent = "  "
        comment = ""
        if m:
            indent = m.group("indent") or indent
            comment = m.group("comment") or ""
        lines[keyLineIndex] = f"{indent}{leafKey}: {yamlValue}{comment}\n"
        _writeFileText(targetPath, "".join(lines))
        return

    # Need to insert - use helper to create nested structure as needed
    lines = _insertNestedKey(lines, sec.startLine, sec.endLine, key, value)
    _writeFileText(targetPath, "".join(lines))


def removeSettingOverride(packsDir: str, langTag: str, key: str) -> None:
    """Remove ``settings.<key>`` from the specified language file, if present."""
    key = (key or "").strip()
    if not key:
        return

    langTag = normalizeLangTag(langTag)
    targetPath = langYamlPath(packsDir, langTag)
    if not os.path.isfile(targetPath):
        return

    text = _readFileText(targetPath)
    lines = text.splitlines(True)
    sec = parseSettingsSectionFromText(text)
    if sec.startLine is None or sec.keyLineIndex is None:
        return

    idx = sec.keyLineIndex.get(key)
    if idx is None:
        return

    del lines[idx]
    _writeFileText(targetPath, "".join(lines))


def parseBool(value, default: bool = False) -> bool:
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return bool(value)
    s = str(value).strip().lower()
    if s in {"1", "true", "t", "yes", "y", "on"}:
        return True
    if s in {"0", "false", "f", "no", "n", "off"}:
        return False
    return default


# -----------------------------------------------------------------------------
# Backwards-compatible aliases
# -----------------------------------------------------------------------------
# Early driver patches used different helper names. Keep aliases so that older
# driver builds don't crash if they call the old API.


def setSettingValue(*, packsDir: str, langTag: str, key: str, value) -> None:
    """Compatibility wrapper for older driver patches.

    Equivalent to ``upsertSetting(packsDir, langTag, key, value)``.
    """
    upsertSetting(packsDir, langTag, key, value)


def coerceToBool(value, default: bool = False) -> bool:
    """Compatibility wrapper for older driver patches.

    Equivalent to ``parseBool(value, default)``.
    """
    return parseBool(value, default)
