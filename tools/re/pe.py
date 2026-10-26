"""Tiny PE helpers (no deps): section map, VA<->offset, find string VA, find dword refs."""
import struct, sys

class PE:
    def __init__(self, path):
        self.b = open(path, 'rb').read()
        b = self.b
        pe = struct.unpack_from('<I', b, 0x3c)[0]
        nsec = struct.unpack_from('<H', b, pe + 6)[0]
        optsz = struct.unpack_from('<H', b, pe + 20)[0]
        self.base = struct.unpack_from('<I', b, pe + 24 + 28)[0]
        self.secs = []
        o = pe + 24 + optsz
        for i in range(nsec):
            name = b[o:o+8].rstrip(b'\0').decode()
            vsz, va, rsz, roff = struct.unpack_from('<IIII', b, o + 8)
            self.secs.append((name, self.base + va, vsz, roff, rsz))
            o += 40

    def off2va(self, off):
        for n, va, vsz, ro, rs in self.secs:
            if ro <= off < ro + rs: return va + off - ro
    def va2off(self, va):
        for n, sva, vsz, ro, rs in self.secs:
            if sva <= va < sva + rs: return ro + va - sva
    def sec(self, va):
        for n, sva, vsz, ro, rs in self.secs:
            if sva <= va < sva + vsz: return n
    def cstr_va(self, s):
        out = []; needle = b'\0' + s.encode() + b'\0'; i = 0
        while (i := self.b.find(needle, i)) >= 0:
            out.append(self.off2va(i + 1)); i += 1
        return out
    def refs(self, va):
        needle = struct.pack('<I', va); out = []; i = 0
        while (i := self.b.find(needle, i)) >= 0:
            out.append(self.off2va(i)); i += 1
        return out
    def dw(self, va): return struct.unpack_from('<I', self.b, self.va2off(va))[0]
    def cstr(self, va, n=80):
        o = self.va2off(va); e = self.b.find(b'\0', o, o + n); return self.b[o:e].decode('latin1')
