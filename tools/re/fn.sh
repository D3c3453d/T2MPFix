#!/usr/bin/env bash
# fn.sh <addr-hex-without-0x> [file]: print one decompiled function from decomp.c
f=${2:-$(dirname "$0")/../../re/t2mp129/decomp.c}
awk -v a="//==== 00$1 " 'index($0,a)==1{p=1;print;next} /^\/\/==== /{if(p)exit} p' "$f"
