#!/usr/bin/env python3
"""Record exact binary checksums for every compiled game picture variant."""
import sys
from pathlib import Path
folder, output = map(Path, sys.argv[1:])
files = sorted(folder.glob('lit_*.dmvi'))
if len(files) != 324:
    raise SystemExit(f'Expected 81 x 4 picture variants, got {len(files)}')
lines = ['/* Generated from the exact picture files uploaded to the board. */',
         'static const struct { char name[80]; unsigned size, hash; } pictures[] = {']
for path in files:
    data = path.read_bytes()
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    lines.append(f'    {{"{path.name}", {len(data)}u, 0x{value:08x}u}},')
lines.append('};')
text = '\n'.join(lines)+'\n'
if not output.exists() or output.read_text() != text:
    output.write_text(text)
