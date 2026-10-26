"""Minimal minidump reader: exception code/address, owning module + offset, and the stack words
that point into modules (a poor man's backtrace). python3 -I minidump.py crash.dmp"""
import struct, sys

b = open(sys.argv[1], 'rb').read()
sig, ver, nstreams, dir_rva = struct.unpack_from('<IIII', b, 0)
streams = {}
for i in range(nstreams):
    t, size, rva = struct.unpack_from('<III', b, dir_rva + 12 * i)
    streams[t] = (size, rva)

def mdstr(rva):
    n = struct.unpack_from('<I', b, rva)[0]
    return b[rva + 4:rva + 4 + n].decode('utf-16le')

mods = []
if 4 in streams:
    _, rva = streams[4]
    n = struct.unpack_from('<I', b, rva)[0]
    for i in range(n):
        o = rva + 4 + 108 * i
        base, _, size, _, ts, name_rva = struct.unpack_from('<QIIIII', b, o)[:6] if False else (
            struct.unpack_from('<Q', b, o)[0], 0, struct.unpack_from('<I', b, o + 8)[0], 0,
            struct.unpack_from('<I', b, o + 16)[0], struct.unpack_from('<I', b, o + 20)[0])
        mods.append((base, size, mdstr(name_rva).split('\\')[-1]))

def where(a):
    for base, size, name in mods:
        if base <= a < base + size:
            return f'{name}+{a - base:#x}'
    return '?'

ctx_rva = None
if 6 in streams:
    _, rva = streams[6]
    tid = struct.unpack_from('<I', b, rva)[0]
    code, flags = struct.unpack_from('<II', b, rva + 8)
    addr = struct.unpack_from('<Q', b, rva + 8 + 16)[0]
    nparams = struct.unpack_from('<I', b, rva + 8 + 24)[0]
    params = struct.unpack_from('<4Q', b, rva + 8 + 32)
    ctx_size, ctx_rva = struct.unpack_from("<II", b, rva + 8 + 152)
    print(f'thread {tid} exception {code:#010x} at {addr:#x} = {where(addr)} params {[hex(p) for p in params[:nparams]]}')
    # x86 CONTEXT: Edi..Eax at 0x9c.., Ebp 0xb4, Eip 0xb8, Esp 0xc4
    regs = dict(zip(['edi', 'esi', 'ebx', 'edx', 'ecx', 'eax', 'ebp', 'eip', 'cs', 'eflags', 'esp'],
                    struct.unpack_from('<11I', b, ctx_rva + 0x9c)))
    print(' '.join(f'{k}={v:08x}' for k, v in regs.items() if k not in ('cs', 'eflags')))
    # stack: find the memory range holding esp
    esp = regs['esp']
    if 5 in streams:
        _, rva = streams[5]
        n = struct.unpack_from('<I', b, rva)[0]
        for i in range(n):
            start, dsize, drva = struct.unpack_from('<QII', b, rva + 4 + 16 * i)
            if start <= esp < start + dsize:
                off = drva + (esp - start)
                print('stack words pointing into modules:')
                shown = 0
                for k in range(0, min(4096, start + dsize - esp), 4):
                    v = struct.unpack_from('<I', b, off + k)[0]
                    w = where(v)
                    if w != '?' and ('Thief2MP' in w or 'dinput' in w):
                        print(f'  [esp+{k:#05x}] {v:08x} {w}')
                        shown += 1
                        if shown > 25: break
for base, size, name in mods:
    if name.lower() in ('thief2mp.exe', 'dinput.dll'):
        print(f'module {name} base {base:#x} size {size:#x}')
