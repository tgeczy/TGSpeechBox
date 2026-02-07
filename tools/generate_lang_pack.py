#!/usr/bin/env python3
"""
generate_lang_pack.py — Auto-generate lang_pack.py from pack.h and pack.cpp

Parses:
  - pack.h  : LanguagePack struct fields (types + defaults)
  - pack.cpp: mergeSettings() YAML key→field mapping (flat + nested)

Emits a complete, working lang_pack.py that stays in sync with the C++ source.

Usage:
    python generate_lang_pack.py --header pack.h --impl pack.cpp --out lang_pack.py

Re-run whenever you add/change LanguagePack fields or mergeSettings() calls.
"""

from __future__ import annotations

import argparse
import re
import sys
import textwrap
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Optional


# =============================================================================
# Step 1: Parse pack.h — LanguagePack struct fields
# =============================================================================

@dataclass
class CppField:
    """A single field from the C++ LanguagePack struct."""
    cpp_type: str       # e.g. "double", "bool", "std::string"
    name: str           # e.g. "primaryStressDiv"
    default: str        # e.g. "1.4", "false", '"espeak_style"'
    py_type: str = ""   # resolved Python type
    py_default: str = ""  # resolved Python default
    comment: str = ""   # trailing comment if any
    is_complex: bool = False  # maps, vectors of complex types


def parse_language_pack_fields(header_text: str) -> list[CppField]:
    """Extract fields from 'struct LanguagePack { ... };' in pack.h."""
    # Find the struct body
    match = re.search(r'struct\s+LanguagePack\s*\{(.*?)^\};', header_text,
                      re.DOTALL | re.MULTILINE)
    if not match:
        raise ValueError("Could not find 'struct LanguagePack' in header")

    body = match.group(1)
    fields = []

    # Patterns for different field types
    # double/int/bool with = default
    pat_simple = re.compile(
        r'^\s*'   # zero or more leading whitespace (pack.h has inconsistent indent)
        r'(double|bool|int|char|std::string|std::u32string|std::uint64_t)\s+'
        r'(\w+)\s*=\s*(.+?)\s*;',
        re.MULTILINE
    )
    # vector<string> with = {...}
    pat_vec_str = re.compile(
        r'^\s*'
        r'std::vector<std::string>\s+'
        r'(\w+)\s*=\s*\{([^}]*)\}\s*;',
        re.MULTILINE
    )
    # vector<...> without initializer (will be empty)
    pat_vec_bare = re.compile(
        r'^\s+'
        r'std::vector<(\w+(?:::\w+)*)>\s+'
        r'(\w+)\s*;',
        re.MULTILINE
    )
    # unordered_map<...> (complex, skip for codegen — these stay manual)
    pat_map = re.compile(
        r'^\s+'
        r'std::unordered_map<[^>]+>\s+'
        r'(\w+)\s*;',
        re.MULTILINE
    )
    # std::array<double, N> with lambda init
    pat_array = re.compile(
        r'^\s*'
        r'std::array<double,\s*\w+>\s+'
        r'(\w+)\s*=',
        re.MULTILINE
    )

    # Parse simple scalar fields
    for m in pat_simple.finditer(body):
        cpp_type, name, default_val = m.group(1), m.group(2), m.group(3).strip()
        f = CppField(cpp_type=cpp_type, name=name, default=default_val)
        _resolve_python_type(f)
        fields.append(f)

    # Parse vector<string> fields
    for m in pat_vec_str.finditer(body):
        name, init = m.group(1), m.group(2).strip()
        items = [s.strip().strip('"') for s in init.split(',') if s.strip()]
        f = CppField(
            cpp_type="std::vector<std::string>",
            name=name,
            default=repr(items),
            py_type="List[str]",
            py_default=repr(items),
        )
        fields.append(f)

    # Parse vector<int> bare (headSteps etc — part of IntonationClause, not LanguagePack)
    # Parse vector<ReplacementRule> etc — complex types, skip

    # Parse array fields
    for m in pat_array.finditer(body):
        name = m.group(1)
        f = CppField(
            cpp_type="std::array<double>",
            name=name,
            default="(lambda)",
            py_type="List[float]",
            py_default="field(default_factory=lambda: _default_traj_rates())",
            is_complex=True,
        )
        fields.append(f)

    # Mark complex map/vector fields (these won't be in the simple dataclass)
    for m in pat_map.finditer(body):
        name = m.group(1)
        # We handle these manually in the template
        pass

    return fields


def _resolve_python_type(f: CppField):
    """Map C++ type + default to Python type + default."""
    t, d = f.cpp_type, f.default

    if t == "double":
        f.py_type = "float"
        # Handle special defaults
        if d == "NAN":
            f.py_default = "float('nan')"
        else:
            f.py_default = d
    elif t == "bool":
        f.py_type = "bool"
        f.py_default = "True" if d in ("true", "1") else "False"
    elif t == "int":
        f.py_type = "int"
        f.py_default = d
    elif t == "char":
        f.py_type = "str"
        if d == "0":
            f.py_default = "''"
        elif d.startswith("'"):
            f.py_default = d
        else:
            f.py_default = "''"
    elif t == "std::string":
        f.py_type = "str"
        # Strip C++ string literal quotes
        if d.startswith('"') and d.endswith('"'):
            f.py_default = repr(d[1:-1])
        else:
            f.py_default = repr(d)
    elif t == "std::u32string":
        f.py_type = "str"
        # U"h" -> "h"
        m = re.match(r'U"(.*)"', d)
        if m:
            f.py_default = repr(m.group(1))
        else:
            f.py_default = "''"
    elif t == "std::uint64_t":
        f.py_type = "int"
        # Complex bitmask expressions like (1ULL << ...) | (1ULL << ...)
        # We'll just default to 0 and let merge handle it
        if "<<" in d:
            f.py_default = "0  # bitmask, set by _default_traj_mask()"
            f.is_complex = True
        else:
            f.py_default = d.replace("ULL", "").replace("ull", "")
    else:
        f.py_type = "Any"
        f.py_default = "None"


# =============================================================================
# Step 2: Parse pack.cpp — mergeSettings() calls
# =============================================================================

@dataclass
class MergeCall:
    """A single getNum/getBool/getStr call in mergeSettings."""
    func: str       # "getNum", "getBool", "getStr", "getNumFrom", etc.
    yaml_key: str   # the YAML key string
    field_name: str # lp.fieldName (without 'lp.' prefix)
    nesting: list[str] = field(default_factory=list)  # nesting path for nested blocks


@dataclass
class NestedBlock:
    """A nested settings block like trajectoryLimit, liquidDynamics, etc."""
    yaml_key: str  # e.g. "trajectoryLimit"
    var_name: str  # e.g. "tl"
    calls: list[MergeCall] = field(default_factory=list)
    sub_blocks: list['NestedBlock'] = field(default_factory=list)


def parse_merge_settings(impl_text: str) -> tuple[list[MergeCall], list[NestedBlock], list[str]]:
    """
    Parse mergeSettings() from pack.cpp.
    Returns (flat_calls, nested_blocks, special_lines).
    """
    # Find the mergeSettings function body
    start = impl_text.find("static void mergeSettings(")
    if start < 0:
        raise ValueError("Could not find mergeSettings() in pack.cpp")

    # Find the end by matching braces
    body_start = impl_text.index("{", start)
    depth = 0
    end = body_start
    for i in range(body_start, len(impl_text)):
        if impl_text[i] == "{":
            depth += 1
        elif impl_text[i] == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                break

    body = impl_text[body_start:end]

    # Parse flat getNum/getBool/getStr calls
    flat_calls = []
    pat_flat = re.compile(
        r'(getNum|getBool|getStr)\s*\(\s*"(\w+)"\s*,\s*lp\.(\w+)\s*\)',
    )
    for m in pat_flat.finditer(body):
        func, yaml_key, field_name = m.group(1), m.group(2), m.group(3)
        flat_calls.append(MergeCall(func=func, yaml_key=yaml_key, field_name=field_name))

    # Parse nested blocks: settings.get("blockName")
    # This is trickier — we'll extract them structurally
    nested_blocks = _parse_nested_blocks(body)

    return flat_calls, nested_blocks, []


def _parse_nested_blocks(body: str) -> list[NestedBlock]:
    """Parse nested if-blocks in mergeSettings."""
    blocks = []

    # Pattern: if (const yaml_min::Node* VAR = settings.get("KEY"); VAR && VAR->isMap())
    pat_top = re.compile(
        r'if\s*\(\s*const\s+yaml_min::Node\*\s+(\w+)\s*=\s*settings\.get\(\s*"(\w+)"\s*\)\s*;\s*\1\s*&&\s*\1->isMap\(\)\s*\)'
    )

    for m in pat_top.finditer(body):
        var_name = m.group(1)
        yaml_key = m.group(2)
        block = NestedBlock(yaml_key=yaml_key, var_name=var_name)

        # Find the block body
        block_start = body.index("{", m.end())
        depth = 0
        block_end = block_start
        for i in range(block_start, len(body)):
            if body[i] == "{":
                depth += 1
            elif body[i] == "}":
                depth -= 1
                if depth == 0:
                    block_end = i + 1
                    break

        block_body = body[block_start:block_end]

        # Parse calls within this block: getNumFrom(*VAR, "key", lp.field)
        pat_from = re.compile(
            rf'(getNumFrom|getBoolFrom|getStrFrom|getStrListFrom)\s*\(\s*\*{var_name}\s*,\s*"(\w+)"\s*,\s*lp\.(\w+)\s*\)'
        )
        for cm in pat_from.finditer(block_body):
            block.calls.append(MergeCall(
                func=cm.group(1), yaml_key=cm.group(2), field_name=cm.group(3)
            ))

        # Parse sub-blocks within this block
        pat_sub = re.compile(
            rf'if\s*\(\s*const\s+yaml_min::Node\*\s+(\w+)\s*=\s*{var_name}->get\(\s*"(\w+)"\s*\)\s*;\s*\1\s*&&\s*\1->isMap\(\)\s*\)'
        )
        for sm in pat_sub.finditer(block_body):
            sub_var = sm.group(1)
            sub_key = sm.group(2)
            sub_block = NestedBlock(yaml_key=sub_key, var_name=sub_var)

            # Find sub-block body
            sub_start = block_body.index("{", sm.end())
            sub_depth = 0
            sub_end = sub_start
            for i in range(sub_start, len(block_body)):
                if block_body[i] == "{":
                    sub_depth += 1
                elif block_body[i] == "}":
                    sub_depth -= 1
                    if sub_depth == 0:
                        sub_end = i + 1
                        break
            sub_body = block_body[sub_start:sub_end]

            pat_sub_from = re.compile(
                rf'(getNumFrom|getBoolFrom|getStrFrom|getStrListFrom)\s*\(\s*\*{sub_var}\s*,\s*"(\w+)"\s*,\s*lp\.(\w+)\s*\)'
            )
            for scm in pat_sub_from.finditer(sub_body):
                sub_block.calls.append(MergeCall(
                    func=scm.group(1), yaml_key=scm.group(2), field_name=scm.group(3)
                ))

            block.sub_blocks.append(sub_block)

        blocks.append(block)

    return blocks


# =============================================================================
# Step 3: Parse FieldId enum and PhonemeFlagBits
# =============================================================================

def parse_field_ids(header_text: str) -> list[tuple[str, int]]:
    """Parse FieldId enum entries."""
    match = re.search(r'enum\s+class\s+FieldId\s*:\s*int\s*\{(.*?)\}',
                      header_text, re.DOTALL)
    if not match:
        return []
    body = match.group(1)
    fields = []
    for m in re.finditer(r'(\w+)\s*=\s*(\d+)', body):
        fields.append((m.group(1), int(m.group(2))))
    return fields


def parse_phoneme_flags(header_text: str) -> list[tuple[str, str]]:
    """Parse PhonemeFlagBits enum."""
    match = re.search(r'enum\s+PhonemeFlagBits\s*:\s*std::uint32_t\s*\{(.*?)\}',
                      header_text, re.DOTALL)
    if not match:
        return []
    body = match.group(1)
    flags = []
    for m in re.finditer(r'(\w+)\s*=\s*(1u?\s*<<\s*\d+)', body):
        name = m.group(1)
        expr = m.group(2)
        # Convert "1u << 3" to "1 << 3"
        expr = re.sub(r'1u\s*', '1 ', expr)
        flags.append((name, expr))
    return flags


# =============================================================================
# Step 4: Generate lang_pack.py
# =============================================================================

def generate_lang_pack(
    fields: list[CppField],
    flat_calls: list[MergeCall],
    nested_blocks: list[NestedBlock],
    field_ids: list[tuple[str, int]],
    phoneme_flags: list[tuple[str, str]],
) -> str:
    """Generate the complete lang_pack.py source."""

    # Map C++ flag constant names to YAML flag key names
    flag_yaml_map = {
        "kIsAfricate": "_isAfricate",
        "kIsLiquid": "_isLiquid",
        "kIsNasal": "_isNasal",
        "kIsSemivowel": "_isSemivowel",
        "kIsStop": "_isStop",
        "kIsTap": "_isTap",
        "kIsTrill": "_isTrill",
        "kIsVoiced": "_isVoiced",
        "kIsVowel": "_isVowel",
        "kCopyAdjacent": "_copyAdjacent",
    }

    # Build field_ids section
    field_names_list = ", ".join(f'"{name}"' for name, _ in field_ids)
    frame_field_count = len(field_ids)

    # Build phoneme flags
    flags_lines = []
    for const_name, expr in phoneme_flags:
        yaml_key = flag_yaml_map.get(const_name, f"_{const_name}")
        flags_lines.append(f'    "{yaml_key}": {expr},')

    # Build LanguagePack dataclass fields
    # We need to convert CppField names from camelCase to snake_case for Python
    lp_fields = []
    field_name_map = {}  # cpp_name -> python_name

    for f in fields:
        py_name = _camel_to_snake(f.name)
        field_name_map[f.name] = py_name

        if f.is_complex:
            lp_fields.append(f"    {py_name}: {f.py_type} = {f.py_default}")
        elif f.py_type in ("List[str]",):
            lp_fields.append(f"    {py_name}: {f.py_type} = field(default_factory=lambda: {f.py_default})")
        else:
            lp_fields.append(f"    {py_name}: {f.py_type} = {f.py_default}")

    # Edge-case fields that can't be auto-parsed from pack.h:
    # - voiceProfileName: bare std::string declaration (no = initializer)
    # - trajectoryLimitApplyMask: multi-line bitmask lambda init
    _auto_names = {_camel_to_snake(f.name) for f in fields}
    if "voice_profile_name" not in _auto_names:
        # Insert after secondary_stress_div
        _insert_after(lp_fields, "secondary_stress_div", '    voice_profile_name: str = ""')
    if "trajectory_limit_apply_mask" not in _auto_names:
        # Insert after trajectory_limit_enabled
        _insert_after(lp_fields, "trajectory_limit_enabled",
                      "    trajectory_limit_apply_mask: int = (1 << 8) | (1 << 9)  # cf2 | cf3")

    # Build _merge_settings flat calls
    merge_flat_lines = []
    seen_flat = set()
    for call in flat_calls:
        if call.field_name in seen_flat:
            continue
        seen_flat.add(call.field_name)
        py_field = _camel_to_snake(call.field_name)
        func_map = {"getNum": "gn", "getBool": "gb", "getStr": "gs"}
        fn = func_map.get(call.func, "gn")
        merge_flat_lines.append(
            f'    lp.{py_field} = {fn}("{call.yaml_key}", lp.{py_field})'
        )

    # Build nested block code
    merge_nested_lines = []
    for block in nested_blocks:
        merge_nested_lines.append("")
        merge_nested_lines.append(
            f'    if "{block.yaml_key}" in s and isinstance(s["{block.yaml_key}"], dict):'
        )
        merge_nested_lines.append(f'        _{block.var_name} = s["{block.yaml_key}"]')

        for call in block.calls:
            py_field = _camel_to_snake(call.field_name)
            fn_map = {
                "getNumFrom": "_gn_from",
                "getBoolFrom": "_gb_from",
                "getStrFrom": "_gs_from",
                "getStrListFrom": "_gsl_from",
            }
            fn = fn_map.get(call.func, "_gn_from")
            merge_nested_lines.append(
                f'        lp.{py_field} = {fn}(_{block.var_name}, "{call.yaml_key}", lp.{py_field})'
            )

        for sub in block.sub_blocks:
            merge_nested_lines.append(
                f'        if "{sub.yaml_key}" in _{block.var_name} and isinstance(_{block.var_name}["{sub.yaml_key}"], dict):'
            )
            merge_nested_lines.append(
                f'            _{sub.var_name} = _{block.var_name}["{sub.yaml_key}"]'
            )
            for call in sub.calls:
                py_field = _camel_to_snake(call.field_name)
                fn_map = {
                    "getNumFrom": "_gn_from",
                    "getBoolFrom": "_gb_from",
                    "getStrFrom": "_gs_from",
                    "getStrListFrom": "_gsl_from",
                }
                fn = fn_map.get(call.func, "_gn_from")
                merge_nested_lines.append(
                    f'            lp.{py_field} = {fn}(_{sub.var_name}, "{call.yaml_key}", lp.{py_field})'
                )

    # Build the trajectory limit special handling
    # (maxHzPerMs nested map + applyTo list → mask)
    # This needs manual code since it's a complex conversion

    # Assemble the output using marker replacement (avoids .format() brace escaping)
    out = _TEMPLATE
    out = out.replace("@@FRAME_FIELD_COUNT@@", str(frame_field_count))
    out = out.replace("@@FIELD_NAMES_LIST@@", field_names_list)
    out = out.replace("@@PHONEME_FLAGS@@", "\n".join(flags_lines))
    out = out.replace("@@LP_FIELDS@@", "\n".join(lp_fields))
    out = out.replace("@@MERGE_FLAT@@", "\n".join(merge_flat_lines))
    out = out.replace("@@MERGE_NESTED@@", "\n".join(merge_nested_lines))
    # Template was written with {{/}} for .format(); unescape now
    out = out.replace("{{", "{").replace("}}", "}")
    return out


def _camel_to_snake(name: str) -> str:
    """Convert camelCase to snake_case."""
    # Handle runs of uppercase (e.g. "F2Scale" -> "f2_scale", "huShortA" -> "hu_short_a")
    s = re.sub(r'([A-Z]+)([A-Z][a-z])', r'\1_\2', name)
    s = re.sub(r'([a-z\d])([A-Z])', r'\1_\2', s)
    return s.lower()


def _insert_after(lines: list[str], after_substr: str, new_line: str):
    """Insert new_line after the first line containing after_substr."""
    for i, line in enumerate(lines):
        if after_substr in line:
            lines.insert(i + 1, new_line)
            return
    # Fallback: append at end
    lines.append(new_line)


# =============================================================================
# Template
# =============================================================================

_TEMPLATE = r'''#!/usr/bin/env python3
"""
lang_pack.py - Complete TGSpeechBox Language Pack Parser

AUTO-GENERATED by generate_lang_pack.py from pack.h + pack.cpp.
Do not edit the LanguagePack dataclass or _merge_settings() by hand.
Re-run generate_lang_pack.py to sync with C++ changes.

Hand-maintained sections are marked with "# --- MANUAL ---".

Usage:
    from lang_pack import load_pack_set, PackSet
    pack = load_pack_set("/path/to/packs", "en-us")
    print(pack.lang.coarticulation_strength)
"""

from __future__ import annotations
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional

# Use our lenient YAML parser that handles unquoted IPA symbols
from simple_yaml import load_yaml_file, get_bool, get_number, get_string

# =============================================================================
# Constants (auto-generated from pack.h FieldId enum)
# =============================================================================

FRAME_FIELD_COUNT = @@FRAME_FIELD_COUNT@@

FIELD_NAMES = [
    @@FIELD_NAMES_LIST@@,
]

FIELD_ID = {{name: idx for idx, name in enumerate(FIELD_NAMES)}}

PHONEME_FLAGS = {{
@@PHONEME_FLAGS@@
}}


# =============================================================================
# Data Classes — structural types (hand-maintained)
# --- MANUAL ---
# =============================================================================

@dataclass
class PhonemeDef:
    """Phoneme definition from phonemes.yaml"""
    key: str
    flags: int = 0
    set_mask: int = 0
    fields: List[float] = field(default_factory=lambda: [0.0] * FRAME_FIELD_COUNT)

    # FrameEx per-phoneme overrides
    has_creakiness: bool = False
    has_breathiness: bool = False
    has_jitter: bool = False
    has_shimmer: bool = False
    has_sharpness: bool = False
    has_end_cf1: bool = False
    has_end_cf2: bool = False
    has_end_cf3: bool = False
    has_end_pf1: bool = False
    has_end_pf2: bool = False
    has_end_pf3: bool = False
    creakiness: float = 0.0
    breathiness: float = 0.0
    jitter: float = 0.0
    shimmer: float = 0.0
    sharpness: float = 1.0
    end_cf1: float = float('nan')
    end_cf2: float = float('nan')
    end_cf3: float = float('nan')
    end_pf1: float = float('nan')
    end_pf2: float = float('nan')
    end_pf3: float = float('nan')

    @property
    def is_vowel(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isVowel"])
    @property
    def is_voiced(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isVoiced"])
    @property
    def is_stop(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isStop"])
    @property
    def is_affricate(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isAfricate"])
    @property
    def is_nasal(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isNasal"])
    @property
    def is_liquid(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isLiquid"])
    @property
    def is_semivowel(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isSemivowel"])
    @property
    def is_tap(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isTap"])
    @property
    def is_trill(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_isTrill"])
    @property
    def copy_adjacent(self) -> bool: return bool(self.flags & PHONEME_FLAGS["_copyAdjacent"])

    def get_field(self, name: str) -> float:
        idx = FIELD_ID.get(name)
        return self.fields[idx] if idx is not None else 0.0

    def has_field(self, name: str) -> bool:
        idx = FIELD_ID.get(name)
        return bool(self.set_mask & (1 << idx)) if idx is not None else False


@dataclass
class RuleWhen:
    at_word_start: bool = False
    at_word_end: bool = False
    before_class: str = ""
    after_class: str = ""
    not_before_class: str = ""
    not_after_class: str = ""


@dataclass
class ReplacementRule:
    from_str: str
    to_list: List[str]
    when: RuleWhen = field(default_factory=RuleWhen)


@dataclass
class TransformRule:
    is_vowel: int = -1
    is_voiced: int = -1
    is_stop: int = -1
    is_affricate: int = -1
    is_nasal: int = -1
    is_liquid: int = -1
    is_semivowel: int = -1
    is_tap: int = -1
    is_trill: int = -1
    is_fricative_like: int = -1
    set_ops: Dict[int, float] = field(default_factory=dict)
    scale_ops: Dict[int, float] = field(default_factory=dict)
    add_ops: Dict[int, float] = field(default_factory=dict)


@dataclass
class IntonationClause:
    pre_head_start: int = 46
    pre_head_end: int = 57
    head_extend_from: int = 4
    head_start: int = 80
    head_end: int = 50
    head_steps: List[int] = field(default_factory=lambda: [100, 75, 50, 25, 0, 63, 38, 13, 0])
    head_stress_end_delta: int = -16
    head_unstressed_run_start_delta: int = -8
    head_unstressed_run_end_delta: int = -5
    nucleus0_start: int = 64
    nucleus0_end: int = 8
    nucleus_start: int = 70
    nucleus_end: int = 18
    tail_start: int = 24
    tail_end: int = 8


# =============================================================================
# LanguagePack dataclass (auto-generated from pack.h LanguagePack struct)
# =============================================================================

def _default_traj_rates() -> List[float]:
    """Default trajectoryLimitMaxHzPerMs array (matches pack.h lambda init)."""
    a = [0.0] * FRAME_FIELD_COUNT
    a[FIELD_ID["cf2"]] = 18.0
    a[FIELD_ID["cf3"]] = 22.0
    return a


@dataclass
class LanguagePack:
    """Complete language pack — auto-generated from pack.h LanguagePack struct."""
    lang_tag: str = ""

@@LP_FIELDS@@

    # --- MANUAL: complex collection types not auto-generated ---
    aliases: Dict[str, str] = field(default_factory=dict)
    pre_replacements: List[ReplacementRule] = field(default_factory=list)
    replacements: List[ReplacementRule] = field(default_factory=list)
    classes: Dict[str, List[str]] = field(default_factory=dict)
    transforms: List[TransformRule] = field(default_factory=list)
    intonation: Dict[str, IntonationClause] = field(default_factory=dict)
    tone_contours: Dict[str, List[int]] = field(default_factory=dict)


@dataclass
class PackSet:
    """Top-level pack container."""
    phonemes: Dict[str, PhonemeDef] = field(default_factory=dict)
    lang: LanguagePack = field(default_factory=LanguagePack)

    def get_phoneme(self, key: str) -> Optional[PhonemeDef]:
        return self.phonemes.get(key)


# =============================================================================
# Parsing helpers
# --- MANUAL ---
# =============================================================================

def _parse_bool(val) -> bool:
    if isinstance(val, bool):
        return val
    if isinstance(val, str):
        return val.lower() in ("true", "yes", "on", "1")
    return bool(val)


def _parse_phoneme(key: str, data: dict) -> PhonemeDef:
    """Parse a single phoneme definition from YAML dict."""
    pdef = PhonemeDef(key=key)

    for field_name, val in data.items():
        # Flags
        if field_name.startswith("_"):
            yaml_key = field_name
            if yaml_key in PHONEME_FLAGS:
                if _parse_bool(val):
                    pdef.flags |= PHONEME_FLAGS[yaml_key]
            continue

        # FrameEx block
        if field_name == "frameEx" and isinstance(val, dict):
            fx_map = {
                "creakiness": ("has_creakiness", "creakiness"),
                "breathiness": ("has_breathiness", "breathiness"),
                "jitter": ("has_jitter", "jitter"),
                "shimmer": ("has_shimmer", "shimmer"),
                "sharpness": ("has_sharpness", "sharpness"),
                "endCf1": ("has_end_cf1", "end_cf1"),
                "endCf2": ("has_end_cf2", "end_cf2"),
                "endCf3": ("has_end_cf3", "end_cf3"),
                "endPf1": ("has_end_pf1", "end_pf1"),
                "endPf2": ("has_end_pf2", "end_pf2"),
                "endPf3": ("has_end_pf3", "end_pf3"),
            }
            for fx_key, (has_attr, val_attr) in fx_map.items():
                if fx_key in val:
                    try:
                        setattr(pdef, has_attr, True)
                        setattr(pdef, val_attr, float(val[fx_key]))
                    except (ValueError, TypeError):
                        pass
            continue

        # Frame fields
        if field_name in FIELD_ID:
            try:
                idx = FIELD_ID[field_name]
                pdef.fields[idx] = float(val)
                pdef.set_mask |= (1 << idx)
            except (ValueError, TypeError):
                pass

    return pdef


def _parse_intonation(data: dict) -> IntonationClause:
    """Parse an intonation clause from YAML."""
    ic = IntonationClause()
    def gi(k, d):
        v = data.get(k)
        return int(v) if v is not None else d

    ic.pre_head_start = gi("preHeadStart", ic.pre_head_start)
    ic.pre_head_end = gi("preHeadEnd", ic.pre_head_end)
    ic.head_extend_from = gi("headExtendFrom", ic.head_extend_from)
    ic.head_start = gi("headStart", ic.head_start)
    ic.head_end = gi("headEnd", ic.head_end)
    ic.head_stress_end_delta = gi("headStressEndDelta", ic.head_stress_end_delta)
    ic.head_unstressed_run_start_delta = gi("headUnstressedRunStartDelta", ic.head_unstressed_run_start_delta)
    ic.head_unstressed_run_end_delta = gi("headUnstressedRunEndDelta", ic.head_unstressed_run_end_delta)
    ic.nucleus0_start = gi("nucleus0Start", ic.nucleus0_start)
    ic.nucleus0_end = gi("nucleus0End", ic.nucleus0_end)
    ic.nucleus_start = gi("nucleusStart", ic.nucleus_start)
    ic.nucleus_end = gi("nucleusEnd", ic.nucleus_end)
    ic.tail_start = gi("tailStart", ic.tail_start)
    ic.tail_end = gi("tailEnd", ic.tail_end)

    if "headSteps" in data and isinstance(data["headSteps"], list):
        ic.head_steps = [int(x) for x in data["headSteps"]]

    return ic


def _parse_replacement(data: dict) -> Optional[ReplacementRule]:
    """Parse a single replacement rule."""
    from_str = data.get("from")
    to_val = data.get("to")
    if from_str is None or to_val is None:
        return None
    to_list = to_val if isinstance(to_val, list) else [str(to_val)]
    to_list = [str(x) for x in to_list]
    when = RuleWhen()
    if "when" in data and isinstance(data["when"], dict):
        w = data["when"]
        when.at_word_start = _parse_bool(w.get("atWordStart", False))
        when.at_word_end = _parse_bool(w.get("atWordEnd", False))
        when.before_class = str(w.get("beforeClass", ""))
        when.after_class = str(w.get("afterClass", ""))
        when.not_before_class = str(w.get("notBeforeClass", ""))
        when.not_after_class = str(w.get("notAfterClass", ""))
    return ReplacementRule(from_str=str(from_str), to_list=to_list, when=when)


def _parse_transform(data: dict) -> Optional[TransformRule]:
    """Parse a single transform rule."""
    tr = TransformRule()
    # Accept either top-level keys or nested 'match:' map
    match_data = data.get("match", data) if isinstance(data.get("match"), dict) else data

    flag_map = {
        "isVowel": "is_vowel", "isVoiced": "is_voiced", "isStop": "is_stop",
        "isAfricate": "is_affricate", "isNasal": "is_nasal", "isLiquid": "is_liquid",
        "isSemivowel": "is_semivowel", "isTap": "is_tap", "isTrill": "is_trill",
        "isFricativeLike": "is_fricative_like",
    }
    for yaml_key, py_attr in flag_map.items():
        if yaml_key in match_data:
            setattr(tr, py_attr, 1 if _parse_bool(match_data[yaml_key]) else 0)

    def parse_field_ops(key):
        ops = {{}}
        if key in data and isinstance(data[key], dict):
            for fn, v in data[key].items():
                if fn in FIELD_ID:
                    try:
                        ops[FIELD_ID[fn]] = float(v)
                    except (ValueError, TypeError):
                        pass
        return ops

    tr.set_ops = parse_field_ops("set")
    tr.scale_ops = parse_field_ops("scale")
    tr.add_ops = parse_field_ops("add")
    return tr


# =============================================================================
# Merge helpers (used by auto-generated _merge_settings)
# =============================================================================

def _gn_from(d: dict, key: str, default: float) -> float:
    """Get number from nested dict."""
    v = d.get(key)
    if v is None:
        return default
    try:
        return float(v)
    except (ValueError, TypeError):
        return default


def _gb_from(d: dict, key: str, default: bool) -> bool:
    """Get bool from nested dict."""
    v = d.get(key)
    if v is None:
        return default
    return _parse_bool(v)


def _gs_from(d: dict, key: str, default: str) -> str:
    """Get string from nested dict."""
    v = d.get(key)
    if v is None:
        return default
    return str(v)


def _gsl_from(d: dict, key: str, default: List[str]) -> List[str]:
    """Get string list from nested dict."""
    v = d.get(key)
    if v is None:
        return default
    if isinstance(v, list):
        return [str(x) for x in v]
    if isinstance(v, str):
        return [s.strip() for s in v.split(",") if s.strip()]
    return default


# =============================================================================
# Settings merge (auto-generated from pack.cpp mergeSettings)
# =============================================================================

def _merge_settings(lp: LanguagePack, s: dict):
    """Merge settings section into LanguagePack.

    Auto-generated from pack.cpp mergeSettings(). Flat keys first, then
    nested blocks.
    """
    def gn(k, d):
        v = s.get(k)
        if v is None: return d
        try: return float(v)
        except (ValueError, TypeError): return d

    def gb(k, d):
        v = s.get(k)
        if v is None: return d
        return _parse_bool(v)

    def gs(k, d):
        v = s.get(k)
        if v is None: return d
        return str(v)

    # --- Flat keys (auto-generated) ---
@@MERGE_FLAT@@

    # --- Special: legacyPitchMode string/bool hybrid ---
    raw = s.get("legacyPitchMode")
    if raw is not None:
        raw_str = str(raw).lower()
        if raw_str in ("true", "1"):
            lp.legacy_pitch_mode = "legacy"
        elif raw_str in ("false", "0"):
            lp.legacy_pitch_mode = "espeak_style"
        else:
            lp.legacy_pitch_mode = str(raw)

    # --- Special: postStopAspirationPhoneme (stored as plain string) ---
    v = s.get("postStopAspirationPhoneme")
    if v is not None:
        lp.post_stop_aspiration_phoneme = str(v)

    # --- Special: singleWordClauseTypeOverride (single char) ---
    v = s.get("singleWordClauseTypeOverride")
    if v is not None:
        sv = str(v)
        lp.single_word_clause_type_override = sv[0] if sv else ''

    # --- Special: spellingDiphthongMode (validated enum) ---
    v = s.get("spellingDiphthongMode")
    if v is not None:
        m = str(v).lower()
        if m in ("none", "monophthong"):
            lp.spelling_diphthong_mode = m

    # --- Special: toneContoursMode -> toneContoursAbsolute ---
    v = s.get("toneContoursMode")
    if v is not None:
        m = str(v).lower()
        if m == "relative":
            lp.tone_contours_absolute = False
        elif m == "absolute":
            lp.tone_contours_absolute = True

    # --- Special: trajectoryLimit flat-key parsing ---
    # trajectoryLimitApplyTo: "[cf2, cf3]" or "cf2, cf3"
    v = s.get("trajectoryLimitApplyTo")
    if v is not None:
        cleaned = str(v).replace("[", "").replace("]", "")
        mask = 0
        for part in cleaned.split(","):
            fn = part.strip()
            if fn in FIELD_ID:
                mask |= (1 << FIELD_ID[fn])
        if mask:
            lp.trajectory_limit_apply_mask = mask

    # trajectoryLimitMaxHzPerMs flat keys
    for suffix, fid_name in [("Cf2", "cf2"), ("Cf3", "cf3"), ("Pf2", "pf2"), ("Pf3", "pf3")]:
        v = s.get(f"trajectoryLimitMaxHzPerMs{{suffix}}")
        if v is not None:
            try:
                fv = float(v)
                if fv > 0.0:
                    lp.trajectory_limit_max_hz_per_ms[FIELD_ID[fid_name]] = fv
            except (ValueError, TypeError):
                pass

    # --- Nested blocks (auto-generated) ---
@@MERGE_NESTED@@

    # --- Special nested: trajectoryLimit maxHzPerMs map + applyTo list ---
    if "trajectoryLimit" in s and isinstance(s["trajectoryLimit"], dict):
        _tl = s["trajectoryLimit"]
        if "applyTo" in _tl and isinstance(_tl["applyTo"], list):
            mask = 0
            for fn in _tl["applyTo"]:
                if str(fn) in FIELD_ID:
                    mask |= (1 << FIELD_ID[str(fn)])
            if mask:
                lp.trajectory_limit_apply_mask = mask
        if "maxHzPerMs" in _tl and isinstance(_tl["maxHzPerMs"], dict):
            for fn, v in _tl["maxHzPerMs"].items():
                if fn in FIELD_ID:
                    try:
                        lp.trajectory_limit_max_hz_per_ms[FIELD_ID[fn]] = float(v)
                    except (ValueError, TypeError):
                        pass


# =============================================================================
# Normalization / Intonation / Tone merge
# --- MANUAL ---
# =============================================================================

def _merge_norm(lp: LanguagePack, n: dict):
    if "aliases" in n and isinstance(n["aliases"], dict):
        for k, v in n["aliases"].items():
            lp.aliases[str(k)] = str(v)
    if "classes" in n and isinstance(n["classes"], dict):
        for cn, items in n["classes"].items():
            if isinstance(items, list):
                lp.classes[str(cn)] = [str(x) for x in items]
    if "preReplacements" in n and isinstance(n["preReplacements"], list):
        for item in n["preReplacements"]:
            if isinstance(item, dict):
                r = _parse_replacement(item)
                if r:
                    lp.pre_replacements.append(r)
    if "replacements" in n and isinstance(n["replacements"], list):
        for item in n["replacements"]:
            if isinstance(item, dict):
                r = _parse_replacement(item)
                if r:
                    lp.replacements.append(r)
    if "stripAllophoneDigits" in n:
        lp.strip_allophone_digits = _parse_bool(n["stripAllophoneDigits"])
    if "stripHyphen" in n:
        lp.strip_hyphen = _parse_bool(n["stripHyphen"])


def _merge_intonation(lp: LanguagePack, data: dict):
    for k, v in data.items():
        if k and k[0] in ".?!," and isinstance(v, dict):
            lp.intonation[k[0]] = _parse_intonation(v)


def _merge_tones(lp: LanguagePack, data: dict):
    for k, v in data.items():
        pts = [int(x) for x in v] if isinstance(v, list) else [int(v)] if isinstance(v, (int, float)) else []
        if pts:
            lp.tone_contours[str(k)] = pts


def _merge_transforms(lp: LanguagePack, data):
    if not isinstance(data, list):
        return
    for item in data:
        if isinstance(item, dict):
            tr = _parse_transform(item)
            if tr:
                lp.transforms.append(tr)


# =============================================================================
# Default intonation (matches applyLanguageDefaults in pack.cpp)
# --- MANUAL ---
# =============================================================================

def _apply_defaults(lp: LanguagePack):
    lp.intonation["."] = IntonationClause(46,57,4,80,50,[100,75,50,25,0,63,38,13,0],-16,-8,-5,64,8,70,18,24,8)
    lp.intonation[","] = IntonationClause(46,57,4,80,60,[100,75,50,25,0,63,38,13,0],-16,-8,-5,34,52,78,34,34,52)
    lp.intonation["?"] = IntonationClause(45,56,3,75,43,[100,75,50,20,60,35,11,0],-16,-7,0,34,68,86,21,34,68)
    lp.intonation["!"] = IntonationClause(46,57,3,90,50,[100,75,50,16,82,50,32,16],-16,-9,0,92,4,92,80,76,4)


# =============================================================================
# Main loading functions
# --- MANUAL ---
# =============================================================================

def find_packs_root(pack_dir: str) -> Path:
    p = Path(pack_dir)
    if (p / "phonemes.yaml").exists():
        return p
    if (p / "packs" / "phonemes.yaml").exists():
        return p / "packs"
    raise FileNotFoundError(f"phonemes.yaml not found under {{pack_dir}}")


def load_pack_set(pack_dir: str, lang_tag: str = "default") -> PackSet:
    """Load complete pack set with phonemes and merged language settings."""
    root = find_packs_root(pack_dir)
    pack = PackSet()

    # Load phonemes
    data = load_yaml_file(root / "phonemes.yaml")
    if data and "phonemes" in data:
        for k, v in data["phonemes"].items():
            if isinstance(v, dict):
                pack.phonemes[k] = _parse_phoneme(k, v)

    # Initialize language pack
    pack.lang = LanguagePack()
    pack.lang.lang_tag = lang_tag.lower().replace("_", "-")
    _apply_defaults(pack.lang)

    # Build chain: default -> base -> base-region
    chain = ["default"]
    parts = pack.lang.lang_tag.split("-")
    cur = ""
    for p in parts:
        cur = f"{{cur}}-{{p}}" if cur else p
        if cur not in chain:
            chain.append(cur)

    # Load each file in chain
    for name in chain:
        lf = root / "lang" / f"{{name}}.yaml"
        if lf.exists():
            data = load_yaml_file(lf)
            if not data:
                continue
            if "settings" in data:
                _merge_settings(pack.lang, data["settings"])
            if "normalization" in data:
                _merge_norm(pack.lang, data["normalization"])
            if "transforms" in data:
                _merge_transforms(pack.lang, data["transforms"])
            if "intonation" in data:
                _merge_intonation(pack.lang, data["intonation"])
            if "toneContours" in data:
                _merge_tones(pack.lang, data["toneContours"])
            if "phonemes" in data:
                for k, v in data["phonemes"].items():
                    if isinstance(v, dict):
                        pack.phonemes[k] = _parse_phoneme(k, v)

    return pack


# =============================================================================
# Utilities
# --- MANUAL ---
# =============================================================================

def format_pack_summary(pack: PackSet) -> str:
    """Return a human-readable summary of the pack."""
    lp = pack.lang
    return f"""=== Pack: {{lp.lang_tag}} ===
Phonemes: {{len(pack.phonemes)}}

Timing:
  primaryStressDiv: {{lp.primary_stress_div}}
  secondaryStressDiv: {{lp.secondary_stress_div}}
  defaultVowelDurationMs: {{lp.default_vowel_duration_ms}}
  defaultFadeMs: {{lp.default_fade_ms}}
  stopDurationMs: {{lp.stop_duration_ms}}

Pitch:
  legacyPitchMode: {{lp.legacy_pitch_mode}}
  fujisakiPhraseAmp: {{lp.fujisaki_phrase_amp}}
  fujisakiAccentMode: {{lp.fujisaki_accent_mode}}

Stop Closure:
  mode: {{lp.stop_closure_mode}}
  vowelGapMs: {{lp.stop_closure_vowel_gap_ms}}
  clusterGapMs: {{lp.stop_closure_cluster_gap_ms}}

Coarticulation:
  enabled: {{lp.coarticulation_enabled}}
  strength: {{lp.coarticulation_strength}}
  mitalkK: {{lp.coarticulation_mitalk_k}}
  labialF2Locus: {{lp.coarticulation_labial_f2_locus}}
  alveolarF2Locus: {{lp.coarticulation_alveolar_f2_locus}}
  velarF2Locus: {{lp.coarticulation_velar_f2_locus}}

Trajectory Limiting:
  enabled: {{lp.trajectory_limit_enabled}}
  windowMs: {{lp.trajectory_limit_window_ms}}

Defaults:
  preFormantGain: {{lp.default_pre_formant_gain}}
  outputGain: {{lp.default_output_gain}}
"""


if __name__ == "__main__":
    import sys
    if len(sys.argv) < 2:
        print("Usage: python lang_pack.py <packs_dir> [lang_tag]")
        sys.exit(1)
    lang = sys.argv[2] if len(sys.argv) > 2 else "default"
    pack = load_pack_set(sys.argv[1], lang)
    print(format_pack_summary(pack))
'''


# =============================================================================
# Main
# =============================================================================

def main():
    ap = argparse.ArgumentParser(description="Generate lang_pack.py from pack.h + pack.cpp")
    ap.add_argument("--header", default="pack.h", help="Path to pack.h")
    ap.add_argument("--impl", default="pack.cpp", help="Path to pack.cpp")
    ap.add_argument("--out", default="lang_pack.py", help="Output path")
    args = ap.parse_args()

    header_text = Path(args.header).read_text(encoding="utf-8")
    impl_text = Path(args.impl).read_text(encoding="utf-8")

    print("Parsing pack.h...")
    fields = parse_language_pack_fields(header_text)
    field_ids = parse_field_ids(header_text)
    phoneme_flags = parse_phoneme_flags(header_text)

    print(f"  Found {len(fields)} LanguagePack fields")
    print(f"  Found {len(field_ids)} FieldId entries")
    print(f"  Found {len(phoneme_flags)} PhonemeFlagBits")

    print("\nParsing pack.cpp mergeSettings()...")
    flat_calls, nested_blocks, _ = parse_merge_settings(impl_text)
    print(f"  Found {len(flat_calls)} flat merge calls")
    print(f"  Found {len(nested_blocks)} nested blocks:")
    for b in nested_blocks:
        sub_count = sum(len(sb.calls) for sb in b.sub_blocks)
        print(f"    {b.yaml_key}: {len(b.calls)} calls, {len(b.sub_blocks)} sub-blocks ({sub_count} sub-calls)")

    print("\nGenerating lang_pack.py...")
    output = generate_lang_pack(fields, flat_calls, nested_blocks, field_ids, phoneme_flags)

    Path(args.out).write_text(output, encoding="utf-8")
    print(f"Wrote: {args.out}")

    # Quick sanity check: can we import it?
    print("\nSanity check...")
    try:
        compile(output, args.out, "exec")
        print("  Syntax OK")
    except SyntaxError as e:
        print(f"  SYNTAX ERROR: {e}")
        return 1

    # Count fields in generated LanguagePack
    lp_count = output.count("    lp.") 
    print(f"  ~{lp_count} merge assignments in _merge_settings()")

    return 0


if __name__ == "__main__":
    sys.exit(main())
