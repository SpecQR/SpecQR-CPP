#!/usr/bin/env python3
"""Conservative publication/runtime-dependency guard for tracked source files."""
import re
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
files=[root/name for name in subprocess.check_output(['git','-C',str(root),'ls-files'],text=True).splitlines()]
errors=[]
for path in files:
    if path.suffix in ('.png','.zip','.dll','.so','.dylib','.a','.exe'):
        errors.append(f'{path.relative_to(root)}: generated binary in public tree')
    data=path.read_text(encoding='utf-8',errors='replace')
    if re.search(r'/(?:Users|home)/[^\s/]+/|[A-Z]:\\Users\\|gh[pousr]_[A-Za-z0-9]{20,}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY',data):
        errors.append(f'{path.relative_to(root)}: private path or credential pattern')
    if path.parent==root/'src' or root/'include' in path.parents:
        if re.search(r'#\s*include\s*[<"](?:boost/|zlib|png\.h|unicode/|iconv|qrencode|ZXing|qrcodegen)',data,re.I):
            errors.append(f'{path.relative_to(root)}: external runtime include')
cmake=(root/'CMakeLists.txt').read_text()
if re.search(r'FetchContent|ExternalProject|find_package\((?!Threads\b)',cmake):
    errors.append('CMakeLists.txt: unexpected dependency/download')
if errors: raise SystemExit('\n'.join(errors))
print(f'publication audit: {len(files)} tracked files, no forbidden binary/private-path/credential/runtime-dependency pattern')
