#!/usr/bin/env python
"""Compile .po files to .mo for the NVDA add-on locale.

Usage:  python tools/compile_mo.py

Walks nvdaAddon/locale/*/LC_MESSAGES/nvda.po and compiles each to nvda.mo.
Equivalent to running ``msgfmt -o nvda.mo nvda.po`` from GNU gettext.
"""
import os
import re
import struct
import sys

def _unescape(s):
    s = s.replace('\\\\', '\x00BS\x00')
    s = s.replace('\\n', '\n')
    s = s.replace('\\t', '\t')
    s = s.replace('\\"', '"')
    s = s.replace('\x00BS\x00', '\\')
    return s

def parse_po(path):
    with open(path, 'r', encoding='utf-8') as f:
        content = f.read()
    pairs = []
    for block in re.split(r'\n\n+', content):
        mid_parts, mstr_parts, cur = [], [], None
        for line in block.strip().split('\n'):
            line = line.strip()
            if line.startswith('#'):
                continue
            if line.startswith('msgid '):
                cur = 'id'
                v = line[6:].strip()
                if v.startswith('"') and v.endswith('"'):
                    mid_parts.append(v[1:-1])
            elif line.startswith('msgstr '):
                cur = 'str'
                v = line[7:].strip()
                if v.startswith('"') and v.endswith('"'):
                    mstr_parts.append(v[1:-1])
            elif line.startswith('"') and line.endswith('"'):
                (mid_parts if cur == 'id' else mstr_parts).append(line[1:-1])
        if mid_parts or mstr_parts:
            pairs.append((_unescape(''.join(mid_parts)), _unescape(''.join(mstr_parts))))
    return pairs

def compile_mo(po_path, mo_path):
    pairs = parse_po(po_path)
    pairs.sort(key=lambda x: x[0].encode('utf-8'))
    n = len(pairs)
    hdr = 28
    orig_off, trans_off = hdr, hdr + n * 8
    data_off = trans_off + n * 8
    orig_enc = [p[0].encode('utf-8') for p in pairs]
    trans_enc = [p[1].encode('utf-8') for p in pairs]
    off = data_off
    oe, te = [], []
    for s in orig_enc:
        oe.append((len(s), off)); off += len(s) + 1
    for s in trans_enc:
        te.append((len(s), off)); off += len(s) + 1
    buf = bytearray()
    buf += struct.pack('<7I', 0x950412de, 0, n, orig_off, trans_off, 0, 0)
    for l, o in oe: buf += struct.pack('<II', l, o)
    for l, o in te: buf += struct.pack('<II', l, o)
    for s in orig_enc: buf += s + b'\x00'
    for s in trans_enc: buf += s + b'\x00'
    with open(mo_path, 'wb') as f:
        f.write(bytes(buf))
    return n

def main():
    base = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        'nvdaAddon', 'locale')
    if not os.path.isdir(base):
        print(f"Locale directory not found: {base}", file=sys.stderr)
        sys.exit(1)
    for lang in sorted(os.listdir(base)):
        po = os.path.join(base, lang, 'LC_MESSAGES', 'nvda.po')
        mo = os.path.join(base, lang, 'LC_MESSAGES', 'nvda.mo')
        if os.path.isfile(po):
            n = compile_mo(po, mo)
            print(f"{lang}: {n} entries -> {mo}")

if __name__ == '__main__':
    main()
