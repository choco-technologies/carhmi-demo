#!/usr/bin/env python3
"""Give every vocabulary image the same size variants as the HTML's arbuz.
The converter retains ownership of geometry; this only expands its image INI.
"""
import argparse
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def copy(source, target):
    if not target.exists() or source.read_bytes() != target.read_bytes():
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('out', type=Path)
    ap.add_argument('--audio', action='store_true')
    args = ap.parse_args()
    if args.audio:
        source = ROOT / 'assets/games/literki/audio'
        target = args.out / 'literki/audio'
        target.mkdir(parents=True, exist_ok=True)
        files = list(source.glob('*.mp3'))
        for file in files:
            copy(file, target / file.name)
        for stale in target.glob('*.mp3'):
            if not (source / stale.name).exists():
                stale.unlink()
        return
    data = json.loads((ROOT / 'assets/games/literki/words.json').read_text())
    ini = (args.out / 'arbuz.png.ini').read_text()
    for group in data['groups']:
        for word in group['words']:
            source = ROOT / 'assets/games/literki/images' / (word['id'] + '.png')
            if not source.is_file():
                raise SystemExit(f'Missing picture for {word["word"]}: {source}')
            target = args.out / ('lit_' + word['id'] + '.png')
            copy(source, target)
            text = ini.replace('arbuz', 'lit_' + word['id'])
            config = target.with_suffix('.png.ini')
            if not config.exists() or config.read_text() != text:
                config.write_text(text)


if __name__ == '__main__':
    main()
