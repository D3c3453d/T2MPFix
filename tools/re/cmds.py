"""Dump Dark engine command tables: records {char*name, int type, void*fn, char*help, int, int}."""
import sys; sys.path.insert(0, '.')
from pe import PE
p = PE(sys.argv[1])
def s(v):
    if p.sec(v) not in ('.rdata', '.data') or p.va2off(v) is None: return None
    t = p.cstr(v, 120)
    return t if t and t.isprintable() else None
n, va, vsz, ro, rs = [x for x in p.secs if x[0] == '.data'][0]
a = va
while a < va + rs - 24:
    name, typ, fn, hlp = p.dw(a), p.dw(a+4), p.dw(a+8), p.dw(a+12)
    nm = s(name)
    if nm and typ < 16 and 0x401000 <= fn < 0xd40000 and (hlp == 0 or s(hlp) is not None) and ' ' not in nm:
        print(f"{a:#x}\t{nm}\t{typ}\t{fn:#x}\t{s(hlp) or ''}")
        a += 24; continue
    a += 4
