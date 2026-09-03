import sys, zlib, struct
d = open(sys.argv[1],'rb').read()
p = d.split(b'\n', 3)
w, h = map(int, p[1].split())
px = p[3] if len(p) > 3 else b''
raw = b''.join(b'\x00' + px[y*w*3:(y+1)*w*3] for y in range(h))
def ch(t, data):
    c = struct.pack('>I', len(data)) + t + data
    return c + struct.pack('>I', zlib.crc32(t + data) & 0xFFFFFFFF)
png = (b'\x89PNG\r\n\x1a\n'
       + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
       + ch(b'IDAT', zlib.compress(raw, 9)) + ch(b'IEND', b''))
open(sys.argv[2],'wb').write(png)
print(sys.argv[2], w, 'x', h)
