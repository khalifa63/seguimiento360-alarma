#!/usr/bin/env python3
from pathlib import Path
import re

base = Path(__file__).parent
files = list(base.glob('*.md'))
print(f'DOCS_FILES: {len(files)}')

for f in files:
    txt = f.read_text()
    links = re.findall(r'\[([^\]]+)\]\(([^)]+)\)', txt)
    for label, target in links:
        if target.startswith('http'): continue
        if target.startswith('#'): continue
        t = base / target
        if not t.exists():
            print(f'BROKEN_LINK: {f.name} -> {target} ({label})')
        else:
            print(f'OK_LINK: {f.name} -> {target}')
print('LINK_CHECK_DONE')