#!/usr/bin/env bash
# Pack the release zip: dist/T2MPFix-<version>.zip with T2MPFix/{dinput.dll,T2MPFix.ini,INSTALL.txt}.
#   tools/package.sh 0.2
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
ver=$1
mkdir -p "$root/dist"
python3 -I - "$root" "$ver" <<'EOF'
import sys, zipfile
root, ver = sys.argv[1:]
out = f'{root}/dist/T2MPFix-{ver}.zip'
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for src, name in (('mod/build/dinput.dll', 'dinput.dll'), ('mod/T2MPFix.ini', 'T2MPFix.ini'),
                      ('mod/INSTALL.txt', 'INSTALL.txt')):
        z.write(f'{root}/{src}', f'T2MPFix/{name}')
print(out)
EOF
sha256sum "$root/mod/build/dinput.dll" "$root/dist/T2MPFix-$ver.zip"
