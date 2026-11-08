import sys, zlib, struct
def read_ppm(p):
    d = open(p, 'rb').read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split()); return w, h, parts[3]
def write_png(p, w, h, rgb, scale=1):
    if scale != 1:
        # escalado al vecino más cercano
        nw, nh = int(w*scale), int(h*scale); out = bytearray()
        for y in range(nh):
            sy = min(h-1, int(y/scale)); row = rgb[sy*w*3:(sy+1)*w*3]
            for x in range(nw):
                sx = min(w-1, int(x/scale)); out += row[sx*3:sx*3+3]
        w, h, rgb = nw, nh, bytes(out)
    raw = b''.join(b'\x00' + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def chunk(t, b): return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(p, 'wb').write(png)
if __name__ == '__main__':
    src, dst = sys.argv[1], sys.argv[2]; scale = float(sys.argv[3]) if len(sys.argv) > 3 else 1
    w, h, rgb = read_ppm(src); write_png(dst, w, h, rgb, scale)
