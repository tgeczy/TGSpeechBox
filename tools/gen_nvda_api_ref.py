#!/usr/bin/env python3
"""Generate a searchable API reference from NVDA source docstrings.

Compares multiple NVDA source trees to show when APIs were added, changed,
or removed across versions.

Usage:
    python tools/gen_nvda_api_ref.py

Outputs:
    tools/nvda_api_ref.md  — full reference with version annotations
"""

import ast
import os
import sys
from pathlib import Path


# ── NVDA source trees to compare (oldest first) ────────────────────────
VERSIONS = [
    ("2023.3",  Path("C:/git/nvda-release-2023.3.4/source")),
    ("2024.4",  Path("C:/git/nvda-release-2024.4/source")),
    ("2025.3",  Path("C:/git/nvda-release-2025.3.1/source")),
    ("2026.1+", Path("C:/Users/Tomi/nvda/source")),
]

# Modules most relevant for synth driver / add-on development.
FOCUS_MODULES = [
    # Core synth/speech infrastructure
    "synthDriverHandler.py",
    "synthDrivers/__init__.py",
    "synthDrivers/_espeak.py",
    "synthDrivers/espeak.py",
    "synthDrivers/oneCore.py",
    "synthDrivers/sapi5.py",
    # New in 2026.1: out-of-process synth bridge
    "synthDrivers/sapi4_32.py",
    "synthDrivers/sapi5_32.py",
    "_bridge/base.py",
    "_bridge/components/proxies/synthDriver.py",
    "_bridge/components/services/synthDriver.py",
    # Speech pipeline
    "speech/__init__.py",
    "speech/commands.py",
    "speech/extensions.py",
    "speech/manager.py",
    "speech/priorities.py",
    "speech/sayAll.py",
    "speech/types.py",
    "speech/speech.py",
    "speech/speechWithoutPauses.py",
    # Audio
    "nvwave.py",
    "wasapi.py",
    "audio/__init__.py",
    "audio/soundSplit.py",
    # Language handling
    "languageHandler.py",
    "speech/languageHandling.py",
    # Driver base classes and settings
    "driverHandler.py",
    "autoSettingsUtils/__init__.py",
    "autoSettingsUtils/autoSettings.py",
    "autoSettingsUtils/driverSetting.py",
    # Config
    "config/__init__.py",
    "config/configFlags.py",
    # Core APIs commonly used by add-ons
    "api.py",
    "baseObject.py",
    "extensionPoints/__init__.py",
    "extensionPoints/util.py",
    "winVersion.py",
    "globalVars.py",
    # Process isolation (new in 2026.1)
    "jobObject.py",
    # GUI / settings panels
    "gui/settingsDialogs.py",
]


def extract_entries(filepath: Path, module_name: str) -> dict:
    """Parse a .py file and return a dict of qualified_name -> entry_info."""
    try:
        source = filepath.read_text(encoding="utf-8", errors="replace")
        tree = ast.parse(source, filename=str(filepath))
    except (SyntaxError, UnicodeDecodeError):
        return {}

    entries = {}

    # Module docstring
    mod_doc = ast.get_docstring(tree)
    if mod_doc:
        entries[module_name] = {
            "kind": "module",
            "name": module_name,
            "doc": mod_doc.strip(),
            "line": 1,
            "sig": "",
        }

    def visit_body(body, prefix=""):
        for node in body:
            if isinstance(node, ast.ClassDef):
                cname = f"{prefix}{node.name}" if prefix else node.name
                doc = ast.get_docstring(node)
                entries[cname] = {
                    "kind": "class",
                    "name": cname,
                    "doc": (doc or "").strip(),
                    "line": node.lineno,
                    "bases": [_name(b) for b in node.bases],
                    "sig": "",
                }
                visit_body(node.body, prefix=cname + ".")

            elif isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                fname = f"{prefix}{node.name}" if prefix else node.name
                doc = ast.get_docstring(node)
                sig = _format_args(node.args)
                ret = ""
                if node.returns:
                    try:
                        ret = f" -> {ast.unparse(node.returns)}"
                    except Exception:
                        pass
                entries[fname] = {
                    "kind": "method" if prefix and "." in prefix else "function",
                    "name": fname,
                    "doc": (doc or "").strip(),
                    "line": node.lineno,
                    "sig": f"({sig}){ret}",
                    "decorators": [_name(d) for d in node.decorator_list],
                }

    visit_body(tree.body)
    return entries


def _name(node) -> str:
    try:
        return ast.unparse(node)
    except Exception:
        if isinstance(node, ast.Name):
            return node.id
        if isinstance(node, ast.Attribute):
            return f"{_name(node.value)}.{node.attr}"
        return "?"


def _format_args(args: ast.arguments) -> str:
    parts = []
    defaults_offset = len(args.args) - len(args.defaults)
    for i, arg in enumerate(args.args):
        s = arg.arg
        if arg.annotation:
            try:
                s += f": {ast.unparse(arg.annotation)}"
            except Exception:
                pass
        di = i - defaults_offset
        if di >= 0 and di < len(args.defaults):
            try:
                s += f"={ast.unparse(args.defaults[di])}"
            except Exception:
                pass
        parts.append(s)
    if args.vararg:
        s = f"*{args.vararg.arg}"
        if args.vararg.annotation:
            try:
                s += f": {ast.unparse(args.vararg.annotation)}"
            except Exception:
                pass
        parts.append(s)
    elif args.kwonlyargs:
        parts.append("*")
    for i, arg in enumerate(args.kwonlyargs):
        s = arg.arg
        if arg.annotation:
            try:
                s += f": {ast.unparse(arg.annotation)}"
            except Exception:
                pass
        if i < len(args.kw_defaults) and args.kw_defaults[i] is not None:
            try:
                s += f"={ast.unparse(args.kw_defaults[i])}"
            except Exception:
                pass
        parts.append(s)
    if args.kwarg:
        s = f"**{args.kwarg.arg}"
        if args.kwarg.annotation:
            try:
                s += f": {ast.unparse(args.kwarg.annotation)}"
            except Exception:
                pass
        parts.append(s)
    return ", ".join(parts)


def generate_ref(versions: list, output: Path, focus_only: bool = True):
    """Generate the multi-version comparison reference."""
    ver_names = [v[0] for v in versions]

    # Step 1: Extract entries from all versions for all modules.
    # module_rel -> version_label -> {qualified_name -> entry}
    all_data = {}

    modules_seen = set()
    for ver_label, src_path in versions:
        if not src_path.exists():
            print(f"  WARNING: {src_path} not found, skipping {ver_label}")
            continue
        if focus_only:
            file_list = []
            for rel in FOCUS_MODULES:
                p = src_path / rel.replace("/", os.sep)
                if p.exists():
                    file_list.append((rel.replace("\\", "/"), p))
        else:
            file_list = []
            for root, dirs, fnames in os.walk(src_path):
                dirs[:] = [d for d in dirs if not d.startswith((".", "__")) and d != "tests"]
                for fn in sorted(fnames):
                    if fn.endswith(".py"):
                        fp = Path(root) / fn
                        rel = fp.relative_to(src_path).as_posix()
                        file_list.append((rel, fp))

        for rel, fp in file_list:
            modules_seen.add(rel)
            entries = extract_entries(fp, rel)
            if rel not in all_data:
                all_data[rel] = {}
            all_data[rel][ver_label] = entries

    # Step 2: For each module, build a unified list of all entry names across
    # all versions, then annotate each with availability + changes.

    lines = []
    lines.append("# NVDA API Reference — Multi-Version Comparison")
    lines.append("")
    lines.append(f"Versions compared: **{' | '.join(ver_names)}**  ")
    lines.append(f"Modules scanned: {len(modules_seen)}  ")
    lines.append("")
    lines.append("### Legend")
    lines.append("")
    lines.append("| Tag | Meaning |")
    lines.append("|-----|---------|")
    lines.append(f"| `ALL` | Present in all versions ({', '.join(ver_names)}) |")
    lines.append(f"| `since X` | Added in version X (not in earlier) |")
    lines.append(f"| `until X` | Removed after version X |")
    lines.append(f"| `changed X` | Signature changed in version X |")
    lines.append(f"| `NEW MODULE` | Entire module is new in that version |")
    lines.append("")
    lines.append("---")
    lines.append("")

    stats = {"added": 0, "removed": 0, "changed": 0, "stable": 0}

    # Sort modules for consistent output
    for rel in sorted(all_data.keys()):
        ver_entries = all_data[rel]  # ver_label -> {name -> entry}

        # Collect all entry names across all versions
        all_names = {}
        for ver_label in ver_names:
            if ver_label in ver_entries:
                for name in ver_entries[ver_label]:
                    if name not in all_names:
                        all_names[name] = []

        if not all_names:
            continue

        # Check if this module is new (not in the first version)
        first_ver = ver_names[0]
        module_is_new = first_ver not in ver_entries or not ver_entries.get(first_ver)
        new_in = None
        if module_is_new:
            for vl in ver_names:
                if vl in ver_entries and ver_entries[vl]:
                    new_in = vl
                    break

        header = f"## `{rel}`"
        if new_in and new_in != first_ver:
            header += f"  — `NEW MODULE since {new_in}`"
        lines.append(header)
        lines.append("")

        # Show module docstring from latest version that has it
        for vl in reversed(ver_names):
            if vl in ver_entries and rel in ver_entries[vl]:
                mod_entry = ver_entries[vl][rel]
                if mod_entry.get("doc"):
                    lines.append(f"> {mod_entry['doc'].replace(chr(10), chr(10) + '> ')}")
                    lines.append("")
                break

        # Now process each entry (skip module-level docstring entry)
        for name in sorted(all_names.keys()):
            if name == rel:
                continue  # skip module docstring entry

            # Determine which versions have this entry
            present_in = []
            entry_per_ver = {}
            for vl in ver_names:
                if vl in ver_entries and name in ver_entries[vl]:
                    present_in.append(vl)
                    entry_per_ver[vl] = ver_entries[vl][name]

            if not present_in:
                continue

            # Use the latest available entry for display
            latest_entry = entry_per_ver[present_in[-1]]
            kind = latest_entry["kind"]
            doc = latest_entry.get("doc", "")
            line_no = latest_entry.get("line", "")
            sig = latest_entry.get("sig", "")
            bases = latest_entry.get("bases", [])
            decorators = latest_entry.get("decorators", [])

            # Compute version tag
            if set(present_in) == set(ver_names):
                # Present in all versions — check for signature changes
                sigs_by_ver = []
                for vl in ver_names:
                    s = entry_per_ver[vl].get("sig", "")
                    sigs_by_ver.append((vl, s))
                sig_changes = []
                for i in range(1, len(sigs_by_ver)):
                    if sigs_by_ver[i][1] != sigs_by_ver[i - 1][1]:
                        sig_changes.append(sigs_by_ver[i][0])
                if sig_changes:
                    ver_tag = f"`changed {', '.join(sig_changes)}`"
                    stats["changed"] += 1
                else:
                    ver_tag = "`ALL`"
                    stats["stable"] += 1
            elif present_in[0] != ver_names[0]:
                # Not in earliest version — it was added
                ver_tag = f"`since {present_in[0]}`"
                stats["added"] += 1
            elif present_in[-1] != ver_names[-1]:
                # Not in latest version — it was removed
                ver_tag = f"`until {present_in[-1]}`"
                stats["removed"] += 1
            else:
                # Present in some but not all (gap)
                ver_tag = f"`in {', '.join(present_in)}`"
                stats["stable"] += 1

            # Build header line
            prefix = ""
            if "staticmethod" in decorators:
                prefix = "@staticmethod "
            elif "classmethod" in decorators:
                prefix = "@classmethod "
            elif "property" in decorators:
                prefix = "@property "

            if kind == "class":
                base_str = f"({', '.join(bases)})" if bases else ""
                lines.append(f"### class `{name}`{base_str}  — {ver_tag}  *(line {line_no})*")
            elif kind in ("function", "method"):
                lines.append(f"### {prefix}`{name}`{sig}  — {ver_tag}  *(line {line_no})*")

            # Show signature change details
            if "changed" in ver_tag:
                lines.append("")
                lines.append("  **Signature history:**")
                prev_sig = None
                for vl in ver_names:
                    if vl in entry_per_ver:
                        s = entry_per_ver[vl].get("sig", "")
                        if s != prev_sig:
                            lines.append(f"  - **{vl}:** `{name}{s}`")
                            prev_sig = s

            if doc:
                lines.append("")
                for dl in doc.split("\n"):
                    lines.append(f"  {dl}")

            lines.append("")

        lines.append("---")
        lines.append("")

    # Summary
    lines.append("## Summary")
    lines.append("")
    lines.append(f"| Metric | Count |")
    lines.append(f"|--------|-------|")
    lines.append(f"| Stable across all versions | {stats['stable']} |")
    lines.append(f"| Added in newer version | {stats['added']} |")
    lines.append(f"| Removed in newer version | {stats['removed']} |")
    lines.append(f"| Signature changed | {stats['changed']} |")
    lines.append(f"| **Total entries** | **{sum(stats.values())}** |")
    lines.append("")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")
    print(f"Written {len(lines)} lines to {output}")
    print(f"  Stable: {stats['stable']}, Added: {stats['added']}, "
          f"Removed: {stats['removed']}, Changed: {stats['changed']}")


def main():
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent / "nvda_api_ref.md"
    focus_only = "--all" not in sys.argv
    generate_ref(VERSIONS, output, focus_only=focus_only)


if __name__ == "__main__":
    main()
