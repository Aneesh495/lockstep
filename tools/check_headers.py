#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile

p=argparse.ArgumentParser()
p.add_argument('--format-only',action='store_true')
p.add_argument('--clang-format',default='clang-format')
p.add_argument('--compiler',default=os.environ.get('CXX','c++'))
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
if a.format_only:
    tool=shutil.which(a.clang_format)
    if not tool: raise SystemExit('Required clang-format executable missing')
    files=sorted(str(f) for directory in ['include','src','apps','tests','bench','fuzz'] for f in (root/directory).rglob('*') if f.suffix in ['.hpp','.cpp'])
    if not files: raise SystemExit('Zero formatting inputs')
    subprocess.run([tool,'--dry-run','--Werror',*files],check=True)
    print(f'Formatted inputs checked: {len(files)}')
else:
    files=sorted((root/'include').rglob('*.hpp'))
    if not files: raise SystemExit('Zero public headers')
    with tempfile.TemporaryDirectory(prefix='lockstep-headers-') as temp:
        for f in files:
            source=Path(temp)/'header.cpp'
            source.write_text(f'#include "{f.relative_to(root / "include")}"\nint main() {{}}\n')
            subprocess.run([*shlex.split(a.compiler),'-std=c++20','-Wall','-Wextra','-Wpedantic','-Werror','-Wno-unused-parameter','-I',str(root/'include'),'-fsyntax-only',str(source)],check=True)
    print(f'Standalone headers compiled: {len(files)}')
