#!/usr/bin/env python3
"""
simple_yaml.py - Flexible YAML parser for TGSpeechBox packs

A lenient YAML parser that handles:
- Unquoted special characters (@, ʃ, ɪ, etc.) as keys
- Both nested and flat key structures
- Comments (# style)
- Basic types: strings, numbers, booleans, lists, nested maps

This avoids PyYAML's strict parsing that chokes on unquoted IPA symbols.

Usage:
    from simple_yaml import load_yaml, load_yaml_file
    
    data = load_yaml_file("phonemes.yaml")
    data = load_yaml(yaml_string)
"""

from __future__ import annotations
from pathlib import Path
from typing import Any, Dict, List, Optional, Union
import re


def load_yaml_file(path: Union[str, Path]) -> Dict[str, Any]:
    """Load and parse a YAML file."""
    text = Path(path).read_text(encoding="utf-8")
    return load_yaml(text)


def load_yaml(text: str) -> Dict[str, Any]:
    """Parse YAML text into a dictionary."""
    parser = _YamlParser(text)
    return parser.parse()


class _YamlParser:
    """
    Simple recursive-descent YAML parser.
    
    Handles indentation-based nesting, supports:
    - Maps (key: value)
    - Lists (- item)
    - Scalars (strings, numbers, booleans, null)
    - Inline lists [a, b, c]
    - Inline maps {a: 1, b: 2}  (basic support)
    """
    
    def __init__(self, text: str):
        self.lines = text.splitlines()
        self.pos = 0
        self.indent_stack = [0]
    
    def parse(self) -> Dict[str, Any]:
        """Parse the entire document as a map."""
        return self._parse_map(0)
    
    def _current_line(self) -> Optional[str]:
        """Get current line or None if EOF."""
        if self.pos >= len(self.lines):
            return None
        return self.lines[self.pos]
    
    def _peek_indent(self) -> int:
        """Get indentation of current line (skipping blanks/comments)."""
        while self.pos < len(self.lines):
            line = self.lines[self.pos]
            stripped = line.lstrip()
            
            # Skip blank lines and comments
            if not stripped or stripped.startswith("#"):
                self.pos += 1
                continue
            
            return len(line) - len(stripped)
        return -1  # EOF
    
    def _consume_line(self) -> tuple[int, str]:
        """Consume current line, return (indent, content)."""
        line = self.lines[self.pos]
        self.pos += 1
        
        # Strip comment from end
        content = line
        if "#" in content:
            # Be careful not to strip # inside quoted strings
            in_quote = None
            for i, ch in enumerate(content):
                if ch in ('"', "'") and (i == 0 or content[i-1] != "\\"):
                    if in_quote == ch:
                        in_quote = None
                    elif in_quote is None:
                        in_quote = ch
                elif ch == "#" and in_quote is None:
                    content = content[:i]
                    break
        
        stripped = content.lstrip()
        indent = len(content) - len(stripped)
        return indent, stripped.rstrip()
    
    def _parse_map(self, min_indent: int) -> Dict[str, Any]:
        """Parse a map at the given indentation level."""
        result = {}
        expected_indent = None  # Track the actual indent level of this map's keys
        
        while True:
            indent = self._peek_indent()
            if indent < 0 or indent < min_indent:
                break
            
            # Set expected indent on first key, then enforce consistency
            if expected_indent is None:
                expected_indent = indent
            elif indent != expected_indent:
                # This line has different indentation than our map's keys
                # If it's less indented, we're done with this map
                # If it's more indented, it belongs to a nested structure (shouldn't happen here)
                if indent < expected_indent:
                    break
                # More indented lines are handled by nested parsing, skip for now
                # This shouldn't normally be reached, but be lenient
                break
            
            line_indent, content = self._consume_line()
            
            if not content:
                continue
            
            # Check for list item
            if content.startswith("- "):
                # This is a list, backtrack and let caller handle
                self.pos -= 1
                break
            
            # Parse key: value
            key, value = self._parse_key_value(content, line_indent)
            if key is not None:
                result[key] = value
        
        return result
    
    def _parse_key_value(self, content: str, line_indent: int) -> tuple[Optional[str], Any]:
        """Parse a 'key: value' line."""
        # Find the colon separating key from value
        # Be careful with colons inside quoted strings
        colon_pos = self._find_key_colon(content)
        
        if colon_pos < 0:
            # No colon found - might be a bare key or invalid
            return None, None
        
        key_part = content[:colon_pos].strip()
        value_part = content[colon_pos + 1:].strip()
        
        # Unquote key if needed
        key = self._unquote(key_part)
        
        # Parse value
        if not value_part:
            # Value is on following lines (nested map or list)
            next_indent = self._peek_indent()
            if next_indent > line_indent:
                # Check if it's a list or map
                next_line = self._current_line()
                if next_line and next_line.lstrip().startswith("- "):
                    value = self._parse_list(next_indent)
                else:
                    value = self._parse_map(next_indent)
            else:
                value = None
        else:
            value = self._parse_value(value_part)
        
        return key, value
    
    def _find_key_colon(self, content: str) -> int:
        """Find the colon that separates key from value."""
        in_quote = None
        for i, ch in enumerate(content):
            if ch in ('"', "'"):
                if in_quote == ch:
                    in_quote = None
                elif in_quote is None:
                    in_quote = ch
            elif ch == ":" and in_quote is None:
                # Make sure it's not part of a time or other value
                # Key colon should be followed by space, newline, or end
                if i + 1 >= len(content) or content[i + 1] in (" ", "\t"):
                    return i
        return -1
    
    def _parse_list(self, min_indent: int) -> List[Any]:
        """Parse a list at the given indentation level."""
        result = []
        expected_indent = None
        
        while True:
            indent = self._peek_indent()
            if indent < 0 or indent < min_indent:
                break
            
            # Set expected indent on first item
            if expected_indent is None:
                expected_indent = indent
            elif indent != expected_indent:
                if indent < expected_indent:
                    break
                # More indented - belongs to nested structure, shouldn't happen here
                break
            
            line = self._current_line()
            if not line or not line.lstrip().startswith("- "):
                break
            
            line_indent, content = self._consume_line()
            
            # Remove "- " prefix
            item_content = content[2:].strip() if content.startswith("- ") else content[1:].strip()
            
            if not item_content:
                # Nested structure
                next_indent = self._peek_indent()
                if next_indent > line_indent:
                    next_line = self._current_line()
                    if next_line and next_line.lstrip().startswith("- "):
                        item = self._parse_list(next_indent)
                    else:
                        item = self._parse_map(next_indent)
                else:
                    item = None
            else:
                # Check if it's an inline map (for list items like "- from: x")
                if ":" in item_content and not item_content.startswith("[") and not item_content.startswith("{"):
                    # Could be a single-line map or start of nested map
                    colon_pos = self._find_key_colon(item_content)
                    if colon_pos > 0:
                        # It's a map item
                        key = self._unquote(item_content[:colon_pos].strip())
                        val_part = item_content[colon_pos + 1:].strip()
                        
                        # Start building the map
                        item_map = {}
                        if val_part:
                            item_map[key] = self._parse_value(val_part)
                        else:
                            # Value continues on next lines
                            next_indent = self._peek_indent()
                            if next_indent > line_indent:
                                item_map[key] = self._parse_map(next_indent)
                            else:
                                item_map[key] = None
                        
                        # Check for more keys at same level (be lenient about exact indent)
                        while True:
                            next_indent = self._peek_indent()
                            if next_indent < 0 or next_indent <= line_indent:
                                break
                            
                            next_line = self._current_line()
                            if not next_line or next_line.lstrip().startswith("- "):
                                break
                            
                            # Accept any indent greater than the list item's dash
                            _, next_content = self._consume_line()
                            k, v = self._parse_key_value(next_content, next_indent)
                            if k is not None:
                                item_map[k] = v
                        
                        item = item_map
                    else:
                        item = self._parse_value(item_content)
                else:
                    item = self._parse_value(item_content)
            
            result.append(item)
        
        return result
    
    def _parse_value(self, content: str) -> Any:
        """Parse a scalar value."""
        if not content:
            return None
        
        # Inline list [a, b, c]
        if content.startswith("[") and content.endswith("]"):
            return self._parse_inline_list(content)
        
        # Inline map {a: 1, b: 2}
        if content.startswith("{") and content.endswith("}"):
            return self._parse_inline_map(content)
        
        # Quoted string
        if (content.startswith('"') and content.endswith('"')) or \
           (content.startswith("'") and content.endswith("'")):
            return self._unquote(content)
        
        # Boolean
        lower = content.lower()
        if lower in ("true", "yes", "on"):
            return True
        if lower in ("false", "no", "off"):
            return False
        
        # Null
        if lower in ("null", "~", ""):
            return None
        
        # Number
        try:
            if "." in content or "e" in content.lower():
                return float(content)
            return int(content)
        except ValueError:
            pass
        
        # Plain string (unquoted)
        return content
    
    def _parse_inline_list(self, content: str) -> List[Any]:
        """Parse an inline list like [a, b, c]."""
        inner = content[1:-1].strip()
        if not inner:
            return []
        
        items = []
        current = ""
        depth = 0
        in_quote = None
        
        for ch in inner:
            if ch in ('"', "'") and in_quote is None:
                in_quote = ch
                current += ch
            elif ch == in_quote:
                in_quote = None
                current += ch
            elif in_quote:
                current += ch
            elif ch == "[" or ch == "{":
                depth += 1
                current += ch
            elif ch == "]" or ch == "}":
                depth -= 1
                current += ch
            elif ch == "," and depth == 0:
                items.append(self._parse_value(current.strip()))
                current = ""
            else:
                current += ch
        
        if current.strip():
            items.append(self._parse_value(current.strip()))
        
        return items
    
    def _parse_inline_map(self, content: str) -> Dict[str, Any]:
        """Parse an inline map like {a: 1, b: 2}."""
        inner = content[1:-1].strip()
        if not inner:
            return {}
        
        result = {}
        # Simple split on comma (doesn't handle nested structures well)
        parts = inner.split(",")
        for part in parts:
            if ":" in part:
                colon = part.index(":")
                key = self._unquote(part[:colon].strip())
                value = self._parse_value(part[colon + 1:].strip())
                result[key] = value
        
        return result
    
    def _unquote(self, s: str) -> str:
        """Remove surrounding quotes from a string."""
        if len(s) >= 2:
            if (s[0] == '"' and s[-1] == '"') or (s[0] == "'" and s[-1] == "'"):
                return s[1:-1]
        return s


# =============================================================================
# Convenience functions for common patterns
# =============================================================================

def get_nested(data: Dict[str, Any], *keys, default: Any = None) -> Any:
    """Safely get a nested value from a dict."""
    current = data
    for key in keys:
        if not isinstance(current, dict):
            return default
        current = current.get(key)
        if current is None:
            return default
    return current


def get_bool(data: Dict[str, Any], key: str, default: bool = False) -> bool:
    """Get a boolean value, handling various representations."""
    val = data.get(key)
    if val is None:
        return default
    if isinstance(val, bool):
        return val
    if isinstance(val, str):
        return val.lower() in ("true", "yes", "on", "1")
    return bool(val)


def get_number(data: Dict[str, Any], key: str, default: float = 0.0) -> float:
    """Get a numeric value."""
    val = data.get(key)
    if val is None:
        return default
    try:
        return float(val)
    except (ValueError, TypeError):
        return default


def get_string(data: Dict[str, Any], key: str, default: str = "") -> str:
    """Get a string value."""
    val = data.get(key)
    if val is None:
        return default
    return str(val)


# =============================================================================
# Test
# =============================================================================

if __name__ == "__main__":
    # Test with some edge cases
    test_yaml = '''
# Test YAML with special characters
phonemes:
  a:
    cf1: 730
    cf2: 1090
    _isVowel: true
  
  # Unquoted special character key
  @:
    cf1: 500
    cf2: 1500
    _isVowel: true
  
  ʃ:
    fricationAmplitude: 0.9
    pf1: 2000
  
  "quoted:key":
    value: 123

settings:
  primaryStressDiv: 1.4
  stopClosureMode: vowel-and-cluster
  coarticulationEnabled: true
  
  # Nested block
  trajectoryLimit:
    enabled: true
    windowMs: 25
    applyTo: [cf2, cf3]
    maxHzPerMs:
      cf2: 18
      cf3: 22

replacements:
  - from: aa
    to: a
  - from: "special:char"
    to: x
    when:
      atWordStart: true

inlineList: [one, two, three]
inlineMap: {a: 1, b: 2}

# Test mixed flat/nested at root level
flatKey1: value1
nestedKey:
  child1: a
  child2: b
flatKey2: value2
anotherNested:
  deep:
    deeper: found
flatKey3: value3
'''
    
    data = load_yaml(test_yaml)
    
    import json
    print(json.dumps(data, indent=2, default=str))
    
    # Test specific lookups
    print("\n--- Tests ---")
    print(f"phonemes/@/cf1: {get_nested(data, 'phonemes', '@', 'cf1')}")
    print(f"phonemes/ʃ/fricationAmplitude: {get_nested(data, 'phonemes', 'ʃ', 'fricationAmplitude')}")
    print(f"settings/trajectoryLimit/enabled: {get_nested(data, 'settings', 'trajectoryLimit', 'enabled')}")
    print(f"settings/trajectoryLimit/applyTo: {get_nested(data, 'settings', 'trajectoryLimit', 'applyTo')}")
    print(f"replacements[0]/from: {data.get('replacements', [{}])[0].get('from')}")
    
    # Test the fix for mixed flat/nested keys
    print("\n--- Mixed flat/nested key tests ---")
    print(f"flatKey1: {data.get('flatKey1')}")
    print(f"flatKey2: {data.get('flatKey2')}")
    print(f"flatKey3: {data.get('flatKey3')}")
    print(f"nestedKey/child1: {get_nested(data, 'nestedKey', 'child1')}")
    print(f"anotherNested/deep/deeper: {get_nested(data, 'anotherNested', 'deep', 'deeper')}")
