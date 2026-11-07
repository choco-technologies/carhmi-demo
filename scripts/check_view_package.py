#!/usr/bin/env python3
"""Verify that the release package contains both current compiled view bundles."""
import struct
import sys
import zipfile
from pathlib import Path

base = Path(sys.argv[1])
with zipfile.ZipFile(base / 'packages/carhmi_view.zip') as package:
    modules = [name for name in package.namelist() if name.endswith('.dmf')]
    if modules != ['carhmi_view.dmf']:
        raise SystemExit(f'Expected one shared view module, got {modules}')
    for screen, size in [('480x272', (480, 272)), ('320x240', (320, 240))]:
        name = f'views/{screen}/carhmi.dmv'
        view = package.read(name)
        if struct.unpack_from('<HH', view, 12) != size:
            raise SystemExit(f'Wrong view size: {name}')
        for resource in (base / 'views' / screen).iterdir():
            if resource.is_file() and package.read(f'views/{screen}/{resource.name}') != resource.read_bytes():
                raise SystemExit(f'Stale packaged resource: {resource}')
print('carhmi_view: both screen bundles verified')

# The controller commits a snapshot through game.revision. Claim properties are
# applied in declaration order, so revision must follow every text it publishes.
import json
import re
vocabulary = json.loads((Path(__file__).resolve().parents[1] / 'assets/games/literki/words.json').read_text())
for screen in ('480x272', '320x240'):
    source = (base / 'view-src' / screen / 'carhmi.dmvs').read_text()
    properties = re.findall(r'^\.prop\s+\$[^,]+,.*"(game\.[^"]+)"$', source, re.MULTILINE)
    commit = properties.index('game.revision')
    for label in ['game.letter' + str(i) for i in range(3)] + ['game.tile' + str(i) for i in range(16)]:
        if properties.index(label) >= commit:
            raise SystemExit(f'{screen}: {label} must precede the snapshot commit')
    # Every illustrated word must have all the size variants drawn by this view.
    ini = (base / 'view-src' / screen / 'arbuz.png.ini').read_text()
    sizes = re.findall(r'^\[arbuz([^]]+)\]$', ini, re.MULTILINE)
    for group in vocabulary['groups']:
        for word in group['words']:
            for suffix in sizes:
                image = base / 'views' / screen / ('lit_' + word['id'] + suffix + '.dmvi')
                if not image.is_file():
                    raise SystemExit(f'Missing game illustration: {image}')
print('Literki: picture variants and snapshot order verified')
