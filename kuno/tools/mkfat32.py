#!/usr/bin/env python3
"""Crea una imagen FAT32 (superfloppy, 512 B/sector, 1 sector/cluster).
Uso: mkfat32.py salida.img ruta=contenido ...   (ruta con un nivel de subdirectorio;
     contenido '@N' = N bytes de patron; soporta nombres largos LFN)"""
import struct, sys, math
SPC, RSV, NCL = 1, 32, 65600
FATSZ = math.ceil((NCL + 2) * 4 / 512); TOTAL = RSV + 2 * FATSZ + NCL
img = bytearray(TOTAL * 512); fat = [0] * (NCL + 2)
fat[0], fat[1] = 0x0FFFFFF8, 0x0FFFFFFF
nxt = [2]

def alloc(nbytes):
    n = max(1, math.ceil(nbytes / 512)); cl = list(range(nxt[0], nxt[0] + n)); nxt[0] += n
    for a, b in zip(cl, cl[1:]): fat[a] = b
    fat[cl[-1]] = 0x0FFFFFFF; return cl
def cl_off(c): return (RSV + 2 * FATSZ + (c - 2)) * 512
def write(cl, data):
    for i, c in enumerate(cl):
        chunk = data[i * 512:(i + 1) * 512]; img[cl_off(c):cl_off(c) + len(chunk)] = chunk

def short(name, used):
    base, _, ext = name.rpartition('.') if '.' in name else (name, '', '')
    b = ''.join(ch for ch in base.upper() if ch.isalnum())[:8]; e = ''.join(ch for ch in ext.upper() if ch.isalnum())[:3]
    exact = (name.upper() == name and len(base) <= 8 and len(ext) <= 3 and ' ' not in name)
    if exact: return (b.ljust(8) + e.ljust(3)), False
    n = 1
    while True:
        s = (b[:6] + '~%d' % n).ljust(8) + e.ljust(3)
        if s not in used: return s, True
        n += 1
def entries(name, used, cl, size, isdir):
    s, need_lfn = short(name, used); used.add(s); out = b''
    if need_lfn:
        ck = 0
        for ch in s.encode(): ck = (((ck & 1) << 7) + (ck >> 1) + ch) & 0xFF
        u = name.encode('utf-16-le') + (b'\0\0' if len(name) % 13 else b'')
        u += b'\xff\xff' * ((-len(u) // 2) % 13); parts = [u[i:i + 26] for i in range(0, len(u), 26)]
        for i in range(len(parts) - 1, -1, -1):
            p = parts[i]; o = (i + 1) | (0x40 if i == len(parts) - 1 else 0)
            out += bytes([o]) + p[0:10] + b'\x0f\x00' + bytes([ck]) + p[10:22] + b'\0\0' + p[22:26]
    out += s.encode() + bytes([0x10 if isdir else 0x20]) + bytes(8) + struct.pack('<H', cl >> 16) + bytes(4) + struct.pack('<HI', cl & 0xFFFF, size)
    return out

dirs = {'': (2, set(), bytearray())}          # nombre -> (cluster, nombres cortos usados, contenido)
def get_dir(d):
    if d not in dirs:
        c = alloc(512)[0]; ents = bytearray(); dirs[d] = (c, set(), ents)
        ents += b'.          \x10' + bytes(8) + struct.pack('<H', c >> 16) + bytes(4) + struct.pack('<HI', c & 0xFFFF, 0)
        ents += b'..         \x10' + bytes(8) + bytes(2) + bytes(4) + struct.pack('<HI', 0, 0)
        r = dirs['']; r[2].extend(entries(d, r[1], c, 0, True))
    return dirs[d]

dirs[''] = (alloc(512)[0], set(), bytearray())     # raiz = cluster 2
for arg in sys.argv[2:]:
    path, _, content = arg.partition('=')
    data = bytes((i * 7 + 3) & 0xFF for i in range(int(content[1:]))) if content.startswith('@') else content.encode()
    d, _, fname = path.rpartition('/'); c, used, ents = get_dir(d) if d else dirs['']
    cl = alloc(len(data)); write(cl, data); ents.extend(entries(fname, used, cl[0], len(data), False))
for name, (c, used, ents) in dirs.items():
    assert len(ents) <= 512, "directorio demasiado grande para esta herramienta"
    write([c], bytes(ents))

bpb = bytearray(512)
bpb[0:3] = b'\xeb\x58\x90'; bpb[3:11] = b'KUNO    '
struct.pack_into('<HBHBHHBHHHII', bpb, 11, 512, SPC, RSV, 2, 0, 0, 0xF8, 0, 63, 255, 0, TOTAL)
struct.pack_into('<IHHI', bpb, 36, FATSZ, 0, 0, 2); struct.pack_into('<HH', bpb, 48, 1, 6)
bpb[64] = 0x80; bpb[66] = 0x29; struct.pack_into('<I', bpb, 67, 0x4B554E4F)
bpb[71:82] = b'KUNO       '; bpb[82:90] = b'FAT32   '; bpb[510:512] = b'\x55\xaa'
img[0:512] = bpb
fb = b''.join(struct.pack('<I', v) for v in fat)
for k in range(2): img[(RSV + k * FATSZ) * 512:(RSV + k * FATSZ) * 512 + len(fb)] = fb
open(sys.argv[1], 'wb').write(img)
print(f"{sys.argv[1]}: {TOTAL*512//1024} KiB, {nxt[0]-2} clusters usados")
