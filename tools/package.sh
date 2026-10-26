#!/usr/bin/env bash
# Pack the release zip: dist/thief2-ghostfix-<version>.zip with ghostfix/{dinput.dll,ghostfix.ini,INSTALL.txt}.
#   tools/package.sh 0.1
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
ver=$1
mkdir -p "$root/dist"
python3 -I - "$root" "$ver" <<'EOF'
import sys, zipfile
root, ver = sys.argv[1:]
out = f'{root}/dist/thief2-ghostfix-{ver}.zip'
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for src, name in (('mod/build/dinput.dll', 'dinput.dll'), ('mod/ghostfix.ini', 'ghostfix.ini'),
                      ('mod/INSTALL.txt', 'INSTALL.txt')):
        z.write(f'{root}/{src}', f'ghostfix/{name}')
print(out)
EOF
sha256sum "$root/mod/build/dinput.dll" "$root/dist/thief2-ghostfix-$ver.zip"
