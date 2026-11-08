# Crea ICON0.PNG (320x176) para PS3 a partir del ícono cuadrado (PPM) del juego.
import sys
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from ppm2png import read_ppm, write_png
w, h, rgb = read_ppm(sys.argv[1]); out = sys.argv[2]
W, H, S = 320, 176, 176
sea = bytes((0x3f, 0xa7, 0xe0))
canvas = bytearray(sea * (W * H))
ox = (W - S) // 2
for y in range(S):
    sy = int(y * h / S)
    for x in range(S):
        sx = int(x * w / S); i = (sy * w + sx) * 3; j = (y * W + ox + x) * 3
        canvas[j:j+3] = rgb[i:i+3]
write_png(out, W, H, bytes(canvas))
