#!/usr/bin/env python3
"""Re-extract the checked-in 3x3 picture atlases with ffmpeg (no AI/API call)."""
import concurrent.futures
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / 'assets/games/literki'


def extract(job):
    source, width, height, cell, name = job
    x, y = (cell % 3) * width // 3, (cell // 3) * height // 3
    w, h = ((cell % 3) + 1) * width // 3 - x, ((cell // 3) + 1) * height // 3 - y
    subprocess.run(['ffmpeg', '-v', 'error', '-y', '-threads', '1', '-filter_threads', '1',
                    '-i', str(source), '-vf', f'crop={w}:{h}:{x}:{y},scale=256:256:flags=lanczos',
                    '-frames:v', '1', str(ROOT / 'images' / (name + '.png'))], check=True)


def main():
    manifest = json.loads((ROOT / 'atlases/manifest.json').read_text())
    jobs = []
    for filename, names in manifest.items():
        if len(names) != 9:
            raise SystemExit(f'{filename}: expected 9 cells')
        source = ROOT / 'atlases' / filename
        info = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                     '-show_entries', 'stream=width,height', '-of', 'json', str(source)]))['streams'][0]
        jobs += [(source, info['width'], info['height'], i, name) for i, name in enumerate(names)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        list(executor.map(extract, jobs))
    print(f'Extracted {len(jobs)} pictures')


if __name__ == '__main__':
    main()
