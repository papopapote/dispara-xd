/* Dispara XD — núcleo portátil: lógica, dibujo por software y sonido sintetizado. */
#include "game.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define TAU 6.2831853f
#define PI  3.14159265f
#define DARK 0x222222u
#define WHITE 0xFFFFFFu

/* =====================================================================
 *  Utilidades
 * ===================================================================== */
static uint32_t rng = 12345u;
static float frand(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng >> 8) * (1.0f / 16777216.0f); }
static float rnd(float a, float b) { return a + frand() * (b - a); }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float d2(float ax, float ay, float bx, float by) { float dx = ax - bx, dy = ay - by; return dx * dx + dy * dy; }

/* =====================================================================
 *  Rasterizador
 * ===================================================================== */
static uint32_t *fb;
static int g_alpha = 255;
static float tfx, tfy, tsx = 1, tsy = 1, camx, camy;
static const float OW = 1.6f; /* grosor del contorno caricaturesco */

static void set_tf(float x, float y, float sx, float sy) { tfx = x + camx; tfy = y + camy; tsx = sx; tsy = sy; }
static void reset_tf(void) { set_tf(0, 0, 1, 1); }
#define LX(x) (tfx + (x) * tsx)
#define LY(y) (tfy + (y) * tsy)

static inline uint32_t blend(uint32_t d, uint32_t c, int a) {
    uint32_t rb = ((((c & 0xFF00FFu) * a) + ((d & 0xFF00FFu) * (255 - a))) >> 8) & 0xFF00FFu;
    uint32_t g  = ((((c & 0x00FF00u) * a) + ((d & 0x00FF00u) * (255 - a))) >> 8) & 0x00FF00u;
    return 0xFF000000u | rb | g;
}
static void span(int y, int x0, int x1, uint32_t c) {
    if (y < 0 || y >= GH) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= GW) x1 = GW - 1;
    if (x0 > x1) return;
    uint32_t *p = fb + y * GW;
    if (g_alpha >= 255) { uint32_t v = 0xFF000000u | c; for (int x = x0; x <= x1; x++) p[x] = v; }
    else if (g_alpha > 0) for (int x = x0; x <= x1; x++) p[x] = blend(p[x], c, g_alpha);
}
/* caja en pantalla con bordes [x0,x1) [y0,y1) */
static void s_box(float x0, float y0, float x1, float y1, uint32_t c) {
    int ya = (int)floorf(y0 + 0.5f), yb = (int)floorf(y1 + 0.5f), xa = (int)floorf(x0 + 0.5f), xb = (int)floorf(x1 + 0.5f);
    for (int y = ya; y < yb; y++) span(y, xa, xb - 1, c);
}
static void s_ellipse(float cx, float cy, float rx, float ry, uint32_t c) {
    if (rx <= 0.2f || ry <= 0.2f) return;
    int y0 = (int)ceilf(cy - ry - 0.5f), y1 = (int)floorf(cy + ry - 0.5f);
    if (y0 < 0) y0 = 0;
    if (y1 >= GH) y1 = GH - 1;
    for (int y = y0; y <= y1; y++) {
        float dy = (y + 0.5f - cy) / ry, k = 1 - dy * dy;
        if (k <= 0) continue;
        float w = rx * sqrtf(k);
        span(y, (int)floorf(cx - w + 0.5f), (int)floorf(cx + w + 0.5f) - 1, c);
    }
}
static void s_ering(float cx, float cy, float rxo, float ryo, float rxi, float ryi, uint32_t c) {
    if (rxo <= 0.2f || ryo <= 0.2f) return;
    int y0 = (int)ceilf(cy - ryo - 0.5f), y1 = (int)floorf(cy + ryo - 0.5f);
    if (y0 < 0) y0 = 0;
    if (y1 >= GH) y1 = GH - 1;
    for (int y = y0; y <= y1; y++) {
        float dy = (y + 0.5f - cy) / ryo, k = 1 - dy * dy;
        if (k <= 0) continue;
        float wo = rxo * sqrtf(k);
        int a = (int)floorf(cx - wo + 0.5f), b = (int)floorf(cx + wo + 0.5f) - 1;
        float di = ryi > 0 ? (y + 0.5f - cy) / ryi : 2, ki = 1 - di * di;
        if (ki > 0 && rxi > 0) {
            float wi = rxi * sqrtf(ki);
            span(y, a, (int)floorf(cx - wi + 0.5f) - 1, c);
            span(y, (int)floorf(cx + wi + 0.5f), b, c);
        } else span(y, a, b, c);
    }
}
static void s_rrect(float x, float y, float w, float h, float r, uint32_t c) {
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int y0 = (int)floorf(y + 0.5f), y1 = (int)floorf(y + h + 0.5f);
    for (int j = y0; j < y1; j++) {
        float py = j + 0.5f, dy = 0, inset = 0;
        if (py < y + r) dy = y + r - py; else if (py > y + h - r) dy = py - (y + h - r);
        if (dy > 0) { float k = r * r - dy * dy; inset = k > 0 ? r - sqrtf(k) : r; }
        span(j, (int)floorf(x + inset + 0.5f), (int)floorf(x + w - inset + 0.5f) - 1, c);
    }
}

/* --- primitivas en coordenadas locales (con transformación) --- */
static void f_ell(float x, float y, float rx, float ry, uint32_t c) { s_ellipse(LX(x), LY(y), rx * tsx, ry * tsy, c); }
static void ell(float x, float y, float rx, float ry, uint32_t c) { f_ell(x, y, rx + OW, ry + OW, DARK); f_ell(x, y, rx, ry, c); }
static void f_rect(float x, float y, float w, float h, uint32_t c) { s_box(LX(x), LY(y), LX(x + w), LY(y + h), c); }
static void f_rrect(float x, float y, float w, float h, float r, uint32_t c) {
    s_rrect(LX(x), LY(y), w * tsx, h * tsy, r * (tsx < tsy ? tsx : tsy), c);
}
static void rrect(float x, float y, float w, float h, float r, uint32_t c) {
    f_rrect(x - OW, y - OW, w + 2 * OW, h + 2 * OW, r + OW, DARK);
    f_rrect(x, y, w, h, r, c);
}
static void f_poly(const float *xy, int n, uint32_t c) {
    float px[16], py[16], miny = 1e9f, maxy = -1e9f;
    if (n > 16) n = 16;
    for (int i = 0; i < n; i++) {
        px[i] = LX(xy[2 * i]); py[i] = LY(xy[2 * i + 1]);
        if (py[i] < miny) miny = py[i];
        if (py[i] > maxy) maxy = py[i];
    }
    int y0 = (int)ceilf(miny - 0.5f), y1 = (int)floorf(maxy - 0.5f);
    if (y0 < 0) y0 = 0;
    if (y1 >= GH) y1 = GH - 1;
    for (int y = y0; y <= y1; y++) {
        float sy = y + 0.5f, xs[16]; int m = 0;
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n; float a = py[i], b = py[j];
            if ((a <= sy && b > sy) || (b <= sy && a > sy)) xs[m++] = px[i] + (sy - a) / (b - a) * (px[j] - px[i]);
        }
        for (int i = 1; i < m; i++) { float v = xs[i]; int k = i - 1; while (k >= 0 && xs[k] > v) { xs[k + 1] = xs[k]; k--; } xs[k + 1] = v; }
        for (int k = 0; k + 1 < m; k += 2) span(y, (int)floorf(xs[k] + 0.5f), (int)floorf(xs[k + 1] + 0.5f) - 1, c);
    }
}
static void thick_line(float x0, float y0, float x1, float y1, float w, uint32_t c) {
    float L = sqrtf(d2(x0, y0, x1, y1)) * (tsx > tsy ? tsx : tsy);
    int n = (int)(L / 1.5f) + 1;
    for (int i = 0; i <= n; i++) { float t = (float)i / n; f_ell(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, w / 2, w / 2, c); }
}
static void poly(const float *xy, int n, uint32_t c) {
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; thick_line(xy[2 * i], xy[2 * i + 1], xy[2 * j], xy[2 * j + 1], 2 * OW, DARK); }
    f_poly(xy, n, c);
}

/* =====================================================================
 *  Fuente 5x7 con contorno
 * ===================================================================== */
typedef struct { uint32_t cp; const char *r[7]; } GlyphDef;
static const GlyphDef GLYPHS[] = {
 {'A',{".###.","#...#","#...#","#####","#...#","#...#","#...#"}}, {'B',{"####.","#...#","#...#","####.","#...#","#...#","####."}},
 {'C',{".###.","#...#","#....","#....","#....","#...#",".###."}}, {'D',{"####.","#...#","#...#","#...#","#...#","#...#","####."}},
 {'E',{"#####","#....","#....","####.","#....","#....","#####"}}, {'F',{"#####","#....","#....","####.","#....","#....","#...."}},
 {'G',{".###.","#...#","#....","#.###","#...#","#...#",".###."}}, {'H',{"#...#","#...#","#...#","#####","#...#","#...#","#...#"}},
 {'I',{".###.","..#..","..#..","..#..","..#..","..#..",".###."}}, {'J',{"..###","...#.","...#.","...#.","...#.","#..#.",".##.."}},
 {'K',{"#...#","#..#.","#.#..","##...","#.#..","#..#.","#...#"}}, {'L',{"#....","#....","#....","#....","#....","#....","#####"}},
 {'M',{"#...#","##.##","#.#.#","#.#.#","#...#","#...#","#...#"}}, {'N',{"#...#","#...#","##..#","#.#.#","#..##","#...#","#...#"}},
 {'O',{".###.","#...#","#...#","#...#","#...#","#...#",".###."}}, {'P',{"####.","#...#","#...#","####.","#....","#....","#...."}},
 {'Q',{".###.","#...#","#...#","#...#","#.#.#","#..#.",".##.#"}}, {'R',{"####.","#...#","#...#","####.","#.#..","#..#.","#...#"}},
 {'S',{".####","#....","#....",".###.","....#","....#","####."}}, {'T',{"#####","..#..","..#..","..#..","..#..","..#..","..#.."}},
 {'U',{"#...#","#...#","#...#","#...#","#...#","#...#",".###."}}, {'V',{"#...#","#...#","#...#","#...#","#...#",".#.#.","..#.."}},
 {'W',{"#...#","#...#","#...#","#.#.#","#.#.#","#.#.#",".#.#."}}, {'X',{"#...#","#...#",".#.#.","..#..",".#.#.","#...#","#...#"}},
 {'Y',{"#...#","#...#",".#.#.","..#..","..#..","..#..","..#.."}}, {'Z',{"#####","....#","...#.","..#..",".#...","#....","#####"}},
 {'x',{".....",".....","#...#",".#.#.","..#..",".#.#.","#...#"}},
 {'0',{".###.","#...#","#..##","#.#.#","##..#","#...#",".###."}}, {'1',{"..#..",".##..","..#..","..#..","..#..","..#..",".###."}},
 {'2',{".###.","#...#","....#","...#.","..#..",".#...","#####"}}, {'3',{"####.","....#","....#",".###.","....#","....#","####."}},
 {'4',{"...#.","..##.",".#.#.","#..#.","#####","...#.","...#."}}, {'5',{"#####","#....","####.","....#","....#","#...#",".###."}},
 {'6',{".###.","#....","#....","####.","#...#","#...#",".###."}}, {'7',{"#####","....#","...#.","..#..",".#...",".#...",".#..."}},
 {'8',{".###.","#...#","#...#",".###.","#...#","#...#",".###."}}, {'9',{".###.","#...#","#...#",".####","....#","....#",".###."}},
 {'!',{"..#..","..#..","..#..","..#..","..#..",".....","..#.."}}, {0xA1,{"..#..",".....","..#..","..#..","..#..","..#..","..#.."}},
 {'?',{".###.","#...#","....#","...#.","..#..",".....","..#.."}}, {0xBF,{"..#..",".....","..#..",".#...","#....","#...#",".###."}},
 {':',{".....","..#..","..#..",".....","..#..","..#..","....."}}, {'.',{".....",".....",".....",".....",".....",".##..",".##.."}},
 {',',{".....",".....",".....",".....",".##..","..#..",".#..."}}, {'-',{".....",".....",".....","#####",".....",".....","....."}},
 {'+',{".....","..#..","..#..","#####","..#..","..#..","....."}}, {'/',{"....#","....#","...#.","..#..",".#...","#....","#...."}},
 {'(',{"...#.","..#..",".#...",".#...",".#...","..#..","...#."}}, {')',{".#...","..#..","...#.","...#.","...#.","..#..",".#..."}},
 {'\'',{"..#..","..#..",".....",".....",".....",".....","....."}}, {0xB7,{".....",".....",".....","..#..",".....",".....","....."}},
 {0x2665,{".....",".#.#.","#####","#####",".###.","..#..","....."}}, {0x25B6,{"#....","##...","###..","####.","###..","##...","#...."}},
 {0x25AE,{"####.","####.","####.","####.","####.","####.","####."}}, {0x2191,{"..#..",".###.","#.#.#","..#..","..#..","..#..","..#.."}},
 {0x2193,{"..#..","..#..","..#..","..#..","#.#.#",".###.","..#.."}}, {0x2715,{".....","#...#",".#.#.","..#..",".#.#.","#...#","....."}},
 {0x25A1,{".....","#####","#...#","#...#","#...#","#####","....."}}, {0x25B3,{".....","..#..",".#.#.",".#.#.","#...#","#####","....."}},
 {0x25CB,{".....",".###.","#...#","#...#","#...#",".###.","....."}},
};
#define NGLYPH (int)(sizeof(GLYPHS) / sizeof(GLYPHS[0]))
static uint8_t glyph_bits[NGLYPH][7];
static void font_init(void) {
    for (int g = 0; g < NGLYPH; g++)
        for (int r = 0; r < 7; r++) {
            uint8_t b = 0;
            for (int c = 0; c < 5; c++) if (GLYPHS[g].r[r][c] == '#') b |= (uint8_t)(1 << (4 - c));
            glyph_bits[g][r] = b;
        }
}
static int find_glyph(uint32_t cp) { for (int g = 0; g < NGLYPH; g++) if (GLYPHS[g].cp == cp) return g; return -1; }
static uint32_t utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s; uint32_t c = *p;
    if (c < 0x80) { *s += 1; return c; }
    if ((c & 0xE0) == 0xC0) { *s += 2; return ((c & 0x1F) << 6) | (p[1] & 0x3F); }
    if ((c & 0xF0) == 0xE0) { *s += 3; return ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); }
    *s += 4; return '?';
}
/* acento: 0 nada, 1 agudo, 2 virgulilla */
static uint32_t base_char(uint32_t cp, int *acc) {
    *acc = 0;
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    switch (cp) {
    case 0xC1: case 0xE1: *acc = 1; return 'A';
    case 0xC9: case 0xE9: *acc = 1; return 'E';
    case 0xCD: case 0xED: *acc = 1; return 'I';
    case 0xD3: case 0xF3: *acc = 1; return 'O';
    case 0xDA: case 0xFA: *acc = 1; return 'U';
    case 0xDC: case 0xFC: return 'U';
    case 0xD1: case 0xF1: *acc = 2; return 'N';
    }
    return cp;
}
static int text_len(const char *s) { int n = 0; while (*s) { utf8_next(&s); n++; } return n; }
/* size ~ altura en píxeles como en la versión HTML; align: 0 izq, 1 centro, 2 der; y = centro vertical */
static void draw_text(const char *s, float x, float y, float size, uint32_t col, int align) {
    float px = size / 9.0f; if (px < 1.5f) px = 1.5f;
    float o = px * 0.55f; if (o < 1.2f) o = 1.2f;
    int n = text_len(s);
    float w = n * 6 * px - px;
    float x0 = align == 1 ? x - w / 2 : align == 2 ? x - w : x;
    float y0 = y - 3.5f * px + camy;
    x0 += camx;
    for (int pass = 0; pass < 2; pass++) {
        const char *p = s; float cx = x0;
        float e = pass == 0 ? o : 0; uint32_t c = pass == 0 ? DARK : col;
        while (*p) {
            int acc = 0; uint32_t cp = utf8_next(&p);
            int g = find_glyph(cp);
            if (g < 0) { cp = base_char(cp, &acc); g = find_glyph(cp); }
            if (g >= 0) {
                for (int r = 0; r < 7; r++) for (int k = 0; k < 5; k++)
                    if (glyph_bits[g][r] & (1 << (4 - k)))
                        s_box(cx + k * px - e, y0 + r * px - e, cx + (k + 1) * px + e, y0 + (r + 1) * px + e, c);
                if (acc == 1) {
                    s_box(cx + 3 * px - e, y0 - 2.2f * px - e, cx + 4 * px + e, y0 - 1.2f * px + e, c);
                    s_box(cx + 2 * px - e, y0 - 1.6f * px - e, cx + 3 * px + e, y0 - 0.6f * px + e, c);
                } else if (acc == 2) {
                    s_box(cx + 0.5f * px - e, y0 - 1.6f * px - e, cx + 2 * px + e, y0 - 0.8f * px + e, c);
                    s_box(cx + 2.5f * px - e, y0 - 2.2f * px - e, cx + 4.5f * px + e, y0 - 1.4f * px + e, c);
                }
            }
            cx += 6 * px;
        }
    }
}
static void itoa_pad(int v, char *out, int pad) {
    char t[16]; int n = 0;
    if (v < 0) v = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 15);
    while (n < pad && n < 15) t[n++] = '0';
    for (int i = 0; i < n; i++) out[i] = t[n - 1 - i];
    out[n] = 0;
}

/* =====================================================================
 *  Sonido: sintetizador simple + secuenciador de música
 * ===================================================================== */
enum { W_SQ, W_SQ25, W_TRI, W_NOISE };
typedef struct { int on, wave; float f0, f1, dur, vol, t, delay, ph, hold; } Voice;
#define NV 24
static Voice voices[NV];
static Voice mvoice[2];
static int music_on = 1, music_track = 0, music_tr = 0; static float music_bpm = 140;
static double music_clock = 0; static int music_step = 0;
static uint32_t nrng = 777u;

static void voice_add(int wave, float f0, float f1, float dur, float vol, float delay) {
    int best = 0; float bk = -1;
    for (int i = 0; i < NV; i++) {
        if (!voices[i].on) { best = i; bk = 2; break; }
        float k = voices[i].t / voices[i].dur;
        if (k > bk) { bk = k; best = i; }
    }
    Voice *v = &voices[best];
    v->on = 1; v->wave = wave; v->f0 = f0; v->f1 = f1; v->dur = dur; v->vol = vol; v->t = 0; v->delay = delay; v->ph = 0; v->hold = 0;
}
enum { SFX_SHOOT, SFX_MISSILE, SFX_BOOM_S, SFX_BOOM_L, SFX_HIT, SFX_POWER, SFX_ALARM, SFX_LEVEL, SFX_WIN, SFX_OVER, SFX_MOVE, SFX_SELECT, SFX_PAUSE };
static void sfx(int id) {
    switch (id) {
    case SFX_SHOOT:   voice_add(W_SQ25, 1300, 520, 0.06f, 0.05f, 0); break;
    case SFX_MISSILE: voice_add(W_NOISE, 4000, 900, 0.35f, 0.10f, 0); voice_add(W_SQ, 260, 820, 0.22f, 0.04f, 0); break;
    case SFX_BOOM_S:  voice_add(W_NOISE, 2200, 180, 0.35f, 0.24f, 0); break;
    case SFX_BOOM_L:  voice_add(W_NOISE, 1100, 50, 1.0f, 0.38f, 0); voice_add(W_TRI, 130, 35, 0.7f, 0.35f, 0); break;
    case SFX_HIT:     voice_add(W_SQ, 620, 70, 0.45f, 0.14f, 0); voice_add(W_NOISE, 1600, 90, 0.4f, 0.22f, 0); break;
    case SFX_POWER:   for (int i = 0; i < 3; i++) voice_add(W_SQ25, 660.0f * (i == 0 ? 1 : i == 1 ? 1.335f : 2), 0, 0.09f, 0.09f, i * 0.08f); break;
    case SFX_ALARM:   for (int i = 0; i < 6; i++) voice_add(W_SQ, i % 2 ? 700 : 520, 0, 0.18f, 0.07f, i * 0.2f); break;
    case SFX_LEVEL:   { const float n[4] = {523, 659, 784, 1047}; for (int i = 0; i < 4; i++) voice_add(W_SQ25, n[i], 0, i == 3 ? 0.35f : 0.14f, 0.09f, i * 0.11f); } break;
    case SFX_WIN:     { const float n[6] = {523, 659, 784, 1047, 784, 1047}; for (int i = 0; i < 6; i++) voice_add(W_SQ25, n[i], 0, i == 5 ? 0.7f : 0.15f, 0.1f, i * 0.14f); } break;
    case SFX_OVER:    { const float n[4] = {392, 330, 262, 196}; for (int i = 0; i < 4; i++) voice_add(W_TRI, n[i], 0, i == 3 ? 0.6f : 0.2f, 0.22f, i * 0.22f); } break;
    case SFX_MOVE:    voice_add(W_SQ25, 880, 0, 0.04f, 0.06f, 0); break;
    case SFX_SELECT:  voice_add(W_SQ25, 660, 1320, 0.12f, 0.08f, 0); break;
    case SFX_PAUSE:   voice_add(W_SQ25, 440, 0, 0.08f, 0.07f, 0); break;
    }
}
/* melodías originales (MIDI; 0 = silencio), 32 pasos de semicorchea */
static const uint8_t LEAD_GAME[32] = {72,0,76,0,79,0,76,0, 74,0,77,0,81,0,77,0, 76,0,79,0,84,0,79,76, 74,0,71,0,74,0,0,0};
static const uint8_t BASS_GAME[32] = {48,0,0,48,55,0,48,0, 50,0,0,50,57,0,50,0, 52,0,0,52,59,0,52,0, 55,0,0,55,43,0,47,0};
static const uint8_t LEAD_MENU[32] = {76,0,0,79,0,0,81,0, 79,0,76,0,74,0,0,0, 72,0,0,74,0,0,76,0, 74,0,71,0,72,0,0,0};
static const uint8_t BASS_MENU[32] = {45,0,0,0,52,0,0,0, 43,0,0,0,50,0,0,0, 41,0,0,0,48,0,0,0, 43,0,0,0,47,0,0,0};
static float midi_hz(int n) { return 440.0f * powf(2.0f, (n - 69) / 12.0f); }

static float voice_sample(Voice *v, float inv_rate) {
    if (v->delay > 0) { v->delay -= inv_rate; return 0; }
    float k = v->t / v->dur;
    if (k >= 1) { v->on = 0; return 0; }
    float f = v->f1 > 0 ? v->f0 + (v->f1 - v->f0) * k : v->f0;
    v->ph += f * inv_rate;
    if (v->ph >= 1) { v->ph -= floorf(v->ph); if (v->wave == W_NOISE) { nrng ^= nrng << 13; nrng ^= nrng >> 17; nrng ^= nrng << 5; v->hold = (nrng >> 8) * (2.0f / 16777216.0f) - 1; } }
    float s;
    switch (v->wave) {
    case W_SQ: s = v->ph < 0.5f ? 1.f : -1.f; break;
    case W_SQ25: s = v->ph < 0.25f ? 1.f : -1.f; break;
    case W_TRI: s = 1 - 4 * fabsf(v->ph - 0.5f); break;
    default: s = v->hold; break;
    }
    float env = (v->t < 0.004f ? v->t / 0.004f : 1.f) * (1 - k);
    v->t += inv_rate;
    return s * env * v->vol;
}
void game_audio(int16_t *out, int frames, int rate) {
    float inv = 1.0f / (float)rate;
    double step_len = 60.0 / music_bpm / 4.0;
    for (int i = 0; i < frames; i++) {
        if (music_track && music_on) {
            music_clock += inv;
            if (music_clock >= step_len) {
                music_clock -= step_len;
                const uint8_t *L = music_track == 1 ? LEAD_MENU : LEAD_GAME, *B = music_track == 1 ? BASS_MENU : BASS_GAME;
                int s = music_step & 31;
                if (L[s]) { Voice *v = &mvoice[0]; memset(v, 0, sizeof *v); v->on = 1; v->wave = W_SQ25; v->f0 = midi_hz(L[s] + music_tr); v->dur = (float)step_len * 1.8f; v->vol = 0.045f; }
                if (B[s]) { Voice *v = &mvoice[1]; memset(v, 0, sizeof *v); v->on = 1; v->wave = W_TRI; v->f0 = midi_hz(B[s] + music_tr); v->dur = (float)step_len * 3.5f; v->vol = 0.12f; }
                music_step++;
            }
        }
        float m = 0;
        for (int k = 0; k < 2; k++) if (mvoice[k].on) m += voice_sample(&mvoice[k], inv);
        for (int k = 0; k < NV; k++) if (voices[k].on) m += voice_sample(&voices[k], inv);
        m = clampf(m, -1, 1);
        int16_t v = (int16_t)(m * 30000);
        out[2 * i] = v; out[2 * i + 1] = v;
    }
}
static void set_music(int track, int transpose, float bpm) {
    if (track != music_track) { music_step = 0; music_clock = 0; mvoice[0].on = mvoice[1].on = 0; }
    music_track = track; music_tr = transpose; music_bpm = bpm;
}
int game_music_enabled(void) { return music_on; }
void game_set_music(int on) { music_on = on; if (!on) mvoice[0].on = mvoice[1].on = 0; }

/* =====================================================================
 *  Datos del juego
 * ===================================================================== */
typedef struct { const char *name; uint32_t sea, sand, land, dot, cloud; int cloudA; uint32_t boss; float dur; int storm, transpose; } Theme;
static const Theme LEVELS[5] = {
    {"MAR EN CALMA",      0x3fa7e0, 0xf3d98b, 0x5cbf4a, 0x2f8f3a, 0xffffff, 190, 0xb8324f, 55, 0, 0},
    {"ATARDECER",         0x3b6fb6, 0xf7b267, 0xd9822b, 0x8c4a1c, 0xffd6be, 180, 0x7b2cbf, 60, 0, 2},
    {"DESIERTO",          0xe9c46a, 0xd4a24c, 0xb5793a, 0x4f9a3a, 0xffffff, 140, 0xc1440e, 65, 0, -3},
    {"SELVA",             0x3c8d40, 0x2a6b2e, 0x5fb84f, 0x1f5222, 0xffffff, 180, 0x335c81, 70, 0, 5},
    {"TORMENTA NOCTURNA", 0x1d3557, 0x5c677d, 0x2f4a3a, 0x1b2e24, 0x96a0be, 150, 0x9d0208, 75, 1, -1},
};
#define NLEVELS 5
typedef struct { const char *n, *desc; uint32_t col; int lives, mis; float hpM, fire, bspd, spawn; } Diff;
static const Diff DIFFS[3] = {
    {"FÁCIL",   "5 VIDAS · 10 MISILES · MÁS LENTOS", 0x7dffb0, 5, 10, 0.7f, 0.6f, 0.8f, 1.3f},
    {"NORMAL",  "3 VIDAS · 6 MISILES",                    0xffe14d, 3, 6,  1.0f, 1.0f, 1.0f, 1.0f},
    {"DIFÍCIL", "2 VIDAS · 4 MISILES · BALAS RÁPIDAS",    0xff6a3d, 2, 4,  1.4f, 1.5f, 1.2f, 0.75f},
};

enum { ST_MENU, ST_PLAY, ST_OVER, ST_WIN };
enum { E_FIGHTER, E_ZIG, E_SWOOP, E_BOMBER, E_BOSS };
static const struct { int hp; float r; int sc; } DEF[5] = {{2, 16, 100}, {2, 15, 150}, {3, 16, 200}, {18, 34, 800}, {220, 62, 5000}};

typedef struct { float x, y, vx, vy, r, t; int dmg, missile, dead; } Bullet;
typedef struct { int type, hp, maxhp, sc, pat, dead, gone; float x, y, vx, vy, r, t, fcd, flash, ax, ph; } Enemy;
typedef struct { int k; float x, y, vx, vy, r, t, life, max; uint32_t c; } Fx; /* k: 0 anillo, 1 bola, 2 humo */
typedef struct { int k, dead; float x, y, t; } Pow; /* k: 0 P, 1 M, 2 vida */
typedef struct { char s[28]; float x, y, t, life, size; } Txt;
typedef struct { float x, y, r, pts[10]; int palms; } Island;
typedef struct { float x, y, s, v; } Cloud;

#define MAX_PB 200
#define MAX_EB 400
#define MAX_EN 64
#define MAX_FX 350
#define MAX_PU 16
#define MAX_TX 16
static Bullet pb[MAX_PB]; static int npb;
static Bullet eb[MAX_EB]; static int neb;
static Enemy en[MAX_EN]; static int nen;
static Fx fx[MAX_FX]; static int nfx;
static Pow pu[MAX_PU]; static int npu;
static Txt tx[MAX_TX]; static int ntx;
static Island islands[3];
static Cloud clouds[5];

static struct { float x, y, r, inv, fcd, mcd, tilt; int lives, gun, mis; } P;
static int state = ST_MENU, paused = 0, score = 0, level = 1, dsel = 1, platform = 0, god = 0;
static int best[3], best_dirty = 0;
static float T, bgT, shake, spawnT, bossT, banner_t = -1;
static const Diff *D = &DIFFS[1];

static float diff_level(void) { return 1 + T / 60 + (level - 1) * 0.35f; }
static const Theme *theme(void) { return &LEVELS[state == ST_MENU ? 0 : level - 1]; }
static Enemy *boss_ptr(void) { for (int i = 0; i < nen; i++) if (en[i].type == E_BOSS && !en[i].dead && !en[i].gone) return &en[i]; return 0; }

static void add_text(const char *s, float x, float y, float life, float size) {
    if (ntx >= MAX_TX) { memmove(tx, tx + 1, sizeof(Txt) * (MAX_TX - 1)); ntx--; }
    Txt *t = &tx[ntx++]; strncpy(t->s, s, sizeof t->s - 1); t->s[sizeof t->s - 1] = 0;
    float hw = text_len(s) * 6 * (size / 9.0f) / 2 + 6;   /* que no se salga de la pantalla */
    if (x < hw) x = hw;
    if (x > GW - hw) x = GW - hw;
    if (y < size * 0.6f) y = size * 0.6f;
    t->x = x; t->y = y; t->t = 0; t->life = life; t->size = size;
}
static Fx *add_fx(void) {
    if (nfx >= MAX_FX) { memmove(fx, fx + 1, sizeof(Fx) * (MAX_FX - 1)); nfx--; }
    Fx *f = &fx[nfx++]; memset(f, 0, sizeof *f); return f;
}
static void new_island(Island *i, float y) {
    i->r = rnd(40, 95); for (int k = 0; k < 10; k++) i->pts[k] = rnd(0.72f, 1.1f);
    i->x = rnd(0, GW); i->y = y; i->palms = (int)rnd(0, 4);
}
static void new_cloud(Cloud *c, float y) { c->x = rnd(-40, GW + 40); c->y = y; c->s = rnd(0.6f, 1.4f); c->v = rnd(110, 190); }

static void level_banner(void) {
    char b[24] = "NIVEL "; b[6] = (char)('0' + level); b[7] = 0;
    add_text(b, GW / 2, GH / 2 - 20, 2.5f, 42);
    add_text(LEVELS[level - 1].name, GW / 2, GH / 2 + 30, 2.5f, 24);
}
static void start_game(void) {
    D = &DIFFS[dsel];
    memset(&P, 0, sizeof P);
    P.x = GW / 2; P.y = GH - 110; P.r = 16; P.lives = D->lives; P.inv = 2; P.gun = 1; P.mis = D->mis;
    npb = neb = nen = nfx = npu = ntx = 0;
    score = 0; T = 0; spawnT = 1.5f; bossT = LEVELS[0].dur; level = 1; paused = 0; banner_t = -1;
    state = ST_PLAY;
    level_banner();
    sfx(SFX_SELECT);
}

static Enemy *mk(int type, float x, float y) {
    if (nen >= MAX_EN) return 0;
    Enemy *e = &en[nen++]; memset(e, 0, sizeof *e);
    int hp = (int)lroundf(DEF[type].hp * (1 + (level - 1) * 0.3f) * D->hpM); if (hp < 1) hp = 1;
    e->type = type; e->x = x; e->y = y; e->hp = e->maxhp = hp; e->r = DEF[type].r; e->sc = DEF[type].sc; e->fcd = rnd(1, 2.5f);
    return e;
}
static void spawn_wave(void) {
    float d = diff_level(), r = frand();
    if (r < 0.38f) {
        float cx = rnd(70, GW - 70); int n = 3 + (int)rnd(0, 3);
        for (int i = 0; i < n; i++) { float off = i - (n - 1) / 2.0f; Enemy *e = mk(E_FIGHTER, cx + off * 38, -30 - fabsf(off) * 32); if (e) e->vy = 140 + 20 * d; }
    } else if (r < 0.66f) {
        float ax = frand() < 0.5f ? GW * 0.3f : GW * 0.7f;
        for (int i = 0; i < 5; i++) { Enemy *e = mk(E_ZIG, ax, -30 - i * 46.0f); if (e) { e->vy = 115 + 15 * d; e->ax = ax; e->ph = i * 0.55f; } }
    } else if (r < 0.86f) {
        for (int i = 0; i < 4; i++) { int L = i % 2 == 0; Enemy *e = mk(E_SWOOP, L ? -30 : GW + 30, 70 + i * 38.0f); if (e) { e->vx = L ? 210 : -210; e->vy = 20; } }
    } else {
        Enemy *e = mk(E_BOMBER, rnd(80, GW - 80), -70); if (e) e->vy = 55 + 5 * d;
    }
}
static void spawn_boss(void) {
    mk(E_BOSS, GW / 2, -120);
    add_text("¡ALERTA: ZEPELÍN!", GW / 2, GH / 2, 2, 30);
    sfx(SFX_ALARM);
}

static void fire_gun(void) {
    P.fcd = 0.11f;
    float sh[6][2] = {{-7, 0}, {7, 0}, {-12, -0.15f}, {12, 0.15f}, {-16, -0.32f}, {16, 0.32f}};
    int n = P.gun * 2;
    for (int i = 0; i < n && npb < MAX_PB; i++) {
        Bullet *b = &pb[npb++]; memset(b, 0, sizeof *b);
        b->x = P.x + sh[i][0]; b->y = P.y - 24; b->vx = sinf(sh[i][1]) * 620; b->vy = -cosf(sh[i][1]) * 620; b->r = 4; b->dmg = 1;
    }
    sfx(SFX_SHOOT);
}
static void fire_missile(void) {
    P.mcd = 0.5f; P.mis--;
    for (int s = -1; s <= 1; s += 2) {
        if (npb >= MAX_PB) break;
        Bullet *b = &pb[npb++]; memset(b, 0, sizeof *b);
        b->x = P.x + s * 16; b->y = P.y; b->vx = s * 140.0f; b->vy = -120; b->r = 6; b->dmg = 8; b->missile = 1;
    }
    sfx(SFX_MISSILE);
}
static void shoot(float x, float y, float a, float s) {
    if (neb >= MAX_EB) return;
    s *= D->bspd;
    Bullet *b = &eb[neb++]; memset(b, 0, sizeof *b);
    b->x = x; b->y = y; b->vx = cosf(a) * s; b->vy = sinf(a) * s; b->r = 5;
}
static float aim(const Enemy *e) { return atan2f(P.y - e->y, P.x - e->x); }
static void enemy_fire(Enemy *e) {
    float d = diff_level(); if (d > 2.6f) d = 2.6f;
    if (e->type == E_BOSS) {
        e->pat = (e->pat + 1) % 3;
        if (e->pat == 0) { for (int i = 0; i < 16; i++) shoot(e->x, e->y + 30, e->t + i * TAU / 16, 150 + 10 * d); e->fcd = 1.4f; }
        else if (e->pat == 1) { float a = aim(e); for (int i = -2; i <= 2; i++) shoot(e->x, e->y + 40, a + i * 0.16f, 230); e->fcd = 1.0f; }
        else {
            for (int s = -50; s <= 50; s += 100) { float a = atan2f(P.y - e->y, P.x - (e->x + s)); for (int k = 0; k < 3; k++) shoot(e->x + s, e->y + 45, a, 180 + k * 50.0f); }
            e->fcd = 1.2f;
        }
    } else if (e->type == E_BOMBER) {
        float a = aim(e); for (int i = -1; i <= 1; i++) shoot(e->x, e->y + 30, a + i * 0.22f, 190);
        e->fcd = rnd(1.4f, 2) / sqrtf(d);
    } else {
        if (frand() < 0.55f) shoot(e->x, e->y + 16, aim(e), 170 + 25 * d);
        e->fcd = rnd(2.2f, 4) / d;
    }
    e->fcd /= D->fire;
}

static void boom(float x, float y, float s, const char *txt) {
    if (s / 4 > shake) shake = s / 4;
    Fx *f = add_fx(); f->k = 0; f->x = x; f->y = y; f->r = 4; f->max = s * 1.7f; f->life = 0.35f;
    static const uint32_t cols[4] = {0xffd23f, 0xff8c1a, 0xff4b2b, 0xfff6d5};
    int n = (int)fminf(18, 7 + s / 5);
    for (int i = 0; i < n; i++) {
        float a = rnd(0, TAU), v = rnd(50, 140) * (0.6f + s / 40);
        f = add_fx(); f->k = 1; f->x = x; f->y = y; f->vx = cosf(a) * v; f->vy = sinf(a) * v; f->r = s * rnd(0.2f, 0.45f); f->life = rnd(0.45f, 0.8f); f->c = cols[i % 4];
    }
    if (txt) add_text(txt, x, y, 0.9f, s > 40 ? 34 : 22);
}
/* desaparición rápida: destello + pocas partículas de vida corta */
static void quick_pop(float x, float y, float s) {
    if (s / 5 > shake) shake = s / 5;
    Fx *f = add_fx(); f->k = 0; f->x = x; f->y = y; f->r = 4; f->max = s * 1.4f; f->life = 0.18f;
    static const uint32_t cols[3] = {0xffd23f, 0xff8c1a, 0xfff6d5};
    for (int i = 0; i < 6; i++) {
        float a = rnd(0, TAU), v = rnd(120, 240);
        f = add_fx(); f->k = 1; f->x = x; f->y = y; f->vx = cosf(a) * v; f->vy = sinf(a) * v; f->r = s * rnd(0.18f, 0.32f); f->life = rnd(0.15f, 0.25f); f->c = cols[i % 3];
    }
}
static void add_pow(float x, float y, int k) { if (npu < MAX_PU) { Pow *p = &pu[npu++]; p->x = x; p->y = y; p->k = k; p->t = 0; p->dead = 0; } }
static void save_best(void) { if (score > best[dsel]) { best[dsel] = score; best_dirty = 1; } }

static void kill_enemy(Enemy *e) {
    e->dead = 1; score += e->sc;
    if (e->type == E_BOSS) {
        for (int i = 0; i < 6; i++) boom(e->x + rnd(-70, 70), e->y + rnd(-40, 40), 45, 0);
        add_text("xD", e->x, e->y, 1.8f, 120);
        add_pow(e->x - 30, e->y, 0); add_pow(e->x + 30, e->y, 2); add_pow(e->x, e->y + 20, 1);
        neb = 0;
        sfx(SFX_BOOM_L);
        if (level >= NLEVELS) {
            state = ST_WIN;
            for (int i = 0; i < nen; i++) if (&en[i] != e) en[i].dead = 1;
            save_best();
            sfx(SFX_WIN);
        } else {
            level++; T = 0; bossT = LEVELS[level - 1].dur; spawnT = 3.5f; banner_t = 1.2f;
            sfx(SFX_LEVEL);
        }
    } else {
        int big = e->type == E_BOMBER;
        quick_pop(e->x, e->y, big ? 34 : 18);
        add_text("xD", e->x, e->y - 6, 0.7f, big ? 40 : 28);
        sfx(big ? SFX_BOOM_L : SFX_BOOM_S);
        if (frand() < (big ? 1.0f : 0.07f)) { float r = frand(); add_pow(e->x, e->y, r < 0.45f ? 0 : r < 0.85f ? 1 : 2); }
    }
}
static void hit_player(void) {
    if (P.inv > 0 || god) return;
    P.lives--; P.inv = 2.5f; if (P.gun > 1) P.gun--;
    boom(P.x, P.y, 30, "¡AUCH!"); neb = 0;
    sfx(SFX_HIT);
    if (P.lives <= 0) {
        boom(P.x, P.y, 50, 0); state = ST_OVER; save_best();
        sfx(SFX_OVER);
    }
}

/* =====================================================================
 *  Actualización
 * ===================================================================== */
void game_init(unsigned seed, int plat) {
    rng = seed ? seed : 12345u; platform = plat;
    font_init();
    for (int i = 0; i < 3; i++) new_island(&islands[i], rnd(0, GH));
    for (int i = 0; i < 5; i++) new_cloud(&clouds[i], rnd(0, GH));
    set_music(1, 0, 110);
}

static void update_music(void) {
    if (state == ST_MENU) set_music(1, 0, 110);
    else if (state == ST_PLAY) set_music(2, LEVELS[level - 1].transpose, boss_ptr() ? 168 : 140);
    else set_music(0, 0, 140);
}

void game_update(const GameInput *in, float dt) {
    if (dt > 0.033f) dt = 0.033f;
    if (in->music) game_set_music(!music_on);

    if (state == ST_MENU) {
        if (in->up) { dsel = (dsel + 2) % 3; sfx(SFX_MOVE); }
        if (in->down) { dsel = (dsel + 1) % 3; sfx(SFX_MOVE); }
        if (in->confirm) start_game();
    } else if (state == ST_OVER || state == ST_WIN) {
        if (in->confirm) { state = ST_MENU; sfx(SFX_MOVE); }
    } else if (state == ST_PLAY) {
        if (paused && in->back) { paused = 0; state = ST_MENU; sfx(SFX_MOVE); }
        else if (in->pause || in->back) { paused = !paused; sfx(SFX_PAUSE); }
    }
    update_music();
    if (paused) return;

    bgT += dt;
    for (int i = 0; i < 3; i++) { Island *s = &islands[i]; s->y += 40 * dt; if (s->y - s->r * 1.3f > GH) new_island(s, -160); }
    for (int i = 0; i < 5; i++) { Cloud *c = &clouds[i]; c->y += c->v * dt; if (c->y - 70 * c->s > GH) new_cloud(c, -90); }
    shake = fmaxf(0, shake - 30 * dt);
    if (state == ST_MENU) return;

    if (state == ST_PLAY) {
        T += dt;
        if (banner_t > 0) { banner_t -= dt; if (banner_t <= 0) level_banner(); }
        float mx = clampf(in->mx, -1, 1), my = clampf(in->my, -1, 1), len = sqrtf(mx * mx + my * my);
        if (len > 1) { mx /= len; my /= len; }
        P.x += mx * 280 * dt; P.y += my * 280 * dt;
        P.x = clampf(P.x, 24, GW - 24); P.y = clampf(P.y, 40, GH - 34);
        P.tilt += (mx - P.tilt) * fminf(1, 10 * dt);
        P.fcd -= dt; P.mcd -= dt; P.inv -= dt;
        if (in->fire && P.fcd <= 0) fire_gun();
        if (in->missile && P.mcd <= 0 && P.mis > 0) fire_missile();

        spawnT -= dt;
        if (!boss_ptr()) {
            bossT -= dt;
            if (bossT <= 0) spawn_boss();
            else if (spawnT <= 0) { spawn_wave(); spawnT = (fmaxf(0.6f, 2.3f - T / 70) + rnd(0, 0.6f)) * D->spawn; }
        } else if (spawnT <= 0) { spawn_wave(); spawnT = 4.5f; }
    }

    /* balas del jugador */
    for (int i = 0; i < npb; i++) {
        Bullet *b = &pb[i];
        if (b->missile) {
            b->t += dt;
            Enemy *tg = 0; float bd = 1e9f;
            for (int k = 0; k < nen; k++) { Enemy *e = &en[k]; if (e->dead || e->y < -20) continue; float dd = d2(b->x, b->y, e->x, e->y); if (dd < bd) { bd = dd; tg = e; } }
            float sp = fminf(520, 160 + b->t * 700), a = atan2f(b->vy, b->vx);
            if (tg) { float da = atan2f(tg->y - b->y, tg->x - b->x) - a; da = atan2f(sinf(da), cosf(da)); a += clampf(da, -6 * dt, 6 * dt); }
            else a += (-PI / 2 - a) * fminf(1, 3 * dt);
            b->vx = cosf(a) * sp; b->vy = sinf(a) * sp;
            if (frand() < 0.6f) { Fx *f = add_fx(); f->k = 2; f->x = b->x; f->y = b->y; f->vy = 30; f->r = rnd(3, 5); f->life = 0.4f; }
            if (b->t > 3) b->dead = 1;
        }
        b->x += b->vx * dt; b->y += b->vy * dt;
        if (b->y < -30 || b->y > GH + 30 || b->x < -30 || b->x > GW + 30) b->dead = 1;
    }

    /* enemigos */
    for (int i = 0; i < nen; i++) {
        Enemy *e = &en[i];
        e->t += dt; e->flash -= dt;
        switch (e->type) {
        case E_FIGHTER: e->y += e->vy * dt; break;
        case E_ZIG: e->y += e->vy * dt; e->x = e->ax + sinf(e->t * 2.6f + e->ph) * 95; break;
        case E_SWOOP: e->x += e->vx * dt; e->y += e->vy * dt; e->vy += 75 * dt; break;
        case E_BOMBER: e->y += e->vy * dt; e->x += sinf(e->t * 0.8f) * 25 * dt; break;
        case E_BOSS: if (e->y < 130) e->y += 60 * dt; else e->x = GW / 2 + sinf(e->t * 0.5f) * 140; break;
        }
        e->fcd -= dt;
        if (state == ST_PLAY && e->fcd <= 0 && e->y > 10 && e->y < GH * 0.7f && !(e->type == E_BOSS && e->y < 130)) enemy_fire(e);
        if (e->y > GH + 90 || (e->t > 2 && (e->x < -90 || e->x > GW + 90))) e->gone = 1;
    }

    /* colisiones: balas del jugador vs enemigos */
    for (int i = 0; i < npb; i++) {
        Bullet *b = &pb[i];
        if (b->dead) continue;
        for (int k = 0; k < nen; k++) {
            Enemy *e = &en[k];
            if (e->dead || d2(b->x, b->y, e->x, e->y) > (b->r + e->r) * (b->r + e->r)) continue;
            b->dead = 1;
            if (b->missile) {
                boom(b->x, b->y, 22, 0); sfx(SFX_BOOM_S);
                for (int j = 0; j < nen; j++) {
                    Enemy *o = &en[j];
                    if (!o->dead && d2(b->x, b->y, o->x, o->y) < (50 + o->r) * (50 + o->r)) {
                        if (o->type != E_BOSS) { kill_enemy(o); continue; }
                        o->hp -= b->dmg; o->flash = 0.08f; if (o->hp <= 0) kill_enemy(o);
                    }
                }
            } else {
                if (e->type != E_BOSS) kill_enemy(e);   /* un disparo basta: ¡xD! */
                else {
                    e->hp -= b->dmg; e->flash = 0.06f;
                    Fx *f = add_fx(); f->k = 1; f->x = b->x; f->y = b->y; f->vy = -40; f->r = 4; f->life = 0.15f; f->c = WHITE;
                    if (frand() < 0.08f) add_text("xD", b->x + rnd(-40, 40), b->y + rnd(-10, 10), 0.6f, 36);
                    if (e->hp <= 0) kill_enemy(e);
                }
            }
            break;
        }
    }
    /* balas enemigas */
    for (int i = 0; i < neb; i++) {
        Bullet *b = &eb[i];
        b->x += b->vx * dt; b->y += b->vy * dt;
        if (b->y < -20 || b->y > GH + 20 || b->x < -20 || b->x > GW + 20) b->dead = 1;
        if (state == ST_PLAY && !b->dead && d2(b->x, b->y, P.x, P.y) < (b->r + P.r * 0.55f) * (b->r + P.r * 0.55f)) { b->dead = 1; hit_player(); }
    }
    /* choque con enemigos */
    if (state == ST_PLAY)
        for (int i = 0; i < nen; i++) {
            Enemy *e = &en[i];
            float rr = e->r * 0.8f + P.r * 0.6f;
            if (!e->dead && P.inv <= 0 && !god && d2(e->x, e->y, P.x, P.y) < rr * rr) {
                if (e->type != E_BOSS) { e->hp -= 5; if (e->hp <= 0) kill_enemy(e); }
                hit_player();
            }
        }
    /* power-ups */
    for (int i = 0; i < npu; i++) {
        Pow *p = &pu[i];
        p->t += dt; p->y += 70 * dt; p->x += sinf(p->t * 3) * 40 * dt;
        if (p->y > GH + 30) p->dead = 1;
        if (state == ST_PLAY && !p->dead && d2(p->x, p->y, P.x, P.y) < 32 * 32) {
            p->dead = 1; sfx(SFX_POWER);
            if (p->k == 0) { if (P.gun < 3) P.gun++; else score += 1000; add_text("¡MÁS PODER!", p->x, p->y, 0.9f, 20); }
            if (p->k == 1) { P.mis += 4; add_text("+4 MISILES", p->x, p->y, 0.9f, 20); }
            if (p->k == 2) { if (P.lives < 5) P.lives++; add_text("+1 VIDA", p->x, p->y, 0.9f, 20); }
        }
    }
    for (int i = 0; i < nfx; i++) { Fx *f = &fx[i]; f->t += dt; f->x += f->vx * dt; f->y += f->vy * dt; f->vx *= 0.92f; f->vy *= 0.92f; }
    for (int i = 0; i < ntx; i++) tx[i].t += dt;

    /* compactar (manteniendo el orden) */
    int j;
    j = 0; for (int i = 0; i < npb; i++) if (!pb[i].dead) pb[j++] = pb[i]; npb = j;
    j = 0; for (int i = 0; i < neb; i++) if (!eb[i].dead) eb[j++] = eb[i]; neb = j;
    j = 0; for (int i = 0; i < nen; i++) if (!en[i].dead && !en[i].gone) en[j++] = en[i]; nen = j;
    j = 0; for (int i = 0; i < npu; i++) if (!pu[i].dead) pu[j++] = pu[i]; npu = j;
    j = 0; for (int i = 0; i < nfx; i++) if (fx[i].t < fx[i].life) fx[j++] = fx[i]; nfx = j;
    j = 0; for (int i = 0; i < ntx; i++) if (tx[i].t < tx[i].life) tx[j++] = tx[i]; ntx = j;
}

/* =====================================================================
 *  Dibujo
 * ===================================================================== */
static void prop(float x, float y, float w) {
    float s = fabsf(sinf(bgT * 55));
    g_alpha = 115; f_rect(x - w * s, y - 1.5f, w * 2 * s + 0.5f, 3, 0x282828); g_alpha = 255;
}
static void draw_bg(void) {
    const Theme *L = theme();
    reset_tf();
    for (int i = 0; i < GW * GH; i++) fb[i] = 0xFF000000u | L->sea;
    g_alpha = 90;
    for (int i = 0; i < 14; i++) {
        float y = fmodf(i * 61 + bgT * 40, GH + 60) - 30, x = (float)((i * 137) % GW);
        for (int a = 0; a < 2; a++)
            for (int k = 0; k <= 6; k++) {
                float ang = PI * (1.1f + 0.8f * k / 6.0f);
                f_ell(x + a * 18 + cosf(ang) * 9, y + sinf(ang) * 9, 1.1f, 1.1f, WHITE);
            }
    }
    g_alpha = 255;
    for (int n = 0; n < 3; n++) {
        Island *s = &islands[n];
        for (int pass = 0; pass < 3; pass++) {
            float sc = pass == 0 ? 1.18f : pass == 1 ? 1.18f : 0.9f, extra = pass == 0 ? 3 : 0;
            uint32_t c = pass == 0 ? DARK : pass == 1 ? L->sand : L->land;
            float xy[20];
            for (int k = 0; k < 10; k++) { float a = k / 10.0f * TAU, r = s->r * s->pts[k] * sc + extra; xy[2 * k] = s->x + cosf(a) * r; xy[2 * k + 1] = s->y + sinf(a) * r * 0.8f; }
            if (pass == 2) poly(xy, 10, c); else f_poly(xy, 10, c);
        }
        for (int k = 0; k < s->palms; k++) ell(s->x + (k - 1) * s->r * 0.35f, s->y + ((k % 2) - 0.5f) * s->r * 0.3f, 9, 9, L->dot);
    }
}
static void draw_clouds(void) {
    const Theme *L = theme();
    reset_tf(); g_alpha = L->cloudA;
    for (int i = 0; i < 5; i++) {
        Cloud *c = &clouds[i]; float s = c->s;
        /* nubes como unión de 3 elipses sin solaparse en alfa: se dibujan opacas en orden y con el mismo alfa */
        f_ell(c->x, c->y, 34 * s, 20 * s, L->cloud);
        f_ell(c->x - 28 * s, c->y + 6 * s, 22 * s, 15 * s, L->cloud);
        f_ell(c->x + 28 * s, c->y + 6 * s, 24 * s, 16 * s, L->cloud);
    }
    g_alpha = 255;
}
static void draw_player(void) {
    if (P.inv > 0 && ((int)(P.inv * 12)) % 2) return;
    set_tf(P.x, P.y, 1 - fabsf(P.tilt) * 0.22f, 1);
    g_alpha = 46; f_ell(14, 40, 26, 8, 0); g_alpha = 255;
    rrect(-36, -6, 72, 17, 8, 0xe8412c);
    f_ell(-24, 2.5f, 4, 4, WHITE); f_ell(24, 2.5f, 4, 4, WHITE);
    rrect(-15, 22, 30, 9, 4, 0xe8412c);
    ell(0, 4, 11, 31, 0xff6a3d);
    f_rect(-10, 14, 20, 4, WHITE);
    ell(0, -6, 6.5f, 9, 0x8fd6ff);
    f_ell(-2, -9, 2, 3, WHITE);
    ell(0, -27, 4, 4, 0xffd23f);
    prop(0, -29, 13);
    reset_tf();
}
static void draw_fighter(const Enemy *e, uint32_t col) {
    set_tf(e->x, e->y, 1, 1);
    uint32_t c = e->flash > 0 ? WHITE : col;
    rrect(-27, -9, 54, 14, 6, c); rrect(-12, -27, 24, 8, 4, c); ell(0, -2, 9.5f, 24, c);
    ell(-4, 11, 3.6f, 4.6f, WHITE); ell(4, 11, 3.6f, 4.6f, WHITE);
    f_ell(-4, 12.5f, 1.8f, 2, 0x111111); f_ell(4, 12.5f, 1.8f, 2, 0x111111);
    thick_line(-8, 4.5f, -1, 7.5f, 2.5f, DARK); thick_line(8, 4.5f, 1, 7.5f, 2.5f, DARK);
    prop(0, 23, 10);
    reset_tf();
}
static void draw_bomber(const Enemy *e) {
    set_tf(e->x, e->y, 1, 1);
    int f = e->flash > 0; uint32_t c = f ? WHITE : 0x7d8fa6;
    rrect(-60, -10, 120, 21, 10, c); rrect(-24, -44, 48, 12, 6, c);
    ell(-34, 8, 8, 13, f ? WHITE : 0x56657a); ell(34, 8, 8, 13, f ? WHITE : 0x56657a);
    ell(0, -4, 16, 42, c);
    ell(0, 25, 8, 9, 0xffe066);
    ell(-6, 6, 4.5f, 5.5f, WHITE); ell(6, 6, 4.5f, 5.5f, WHITE);
    f_ell(-6, 8, 2.2f, 2.5f, 0x111111); f_ell(6, 8, 2.2f, 2.5f, 0x111111);
    thick_line(-12, -2, -2, 2, 3, DARK); thick_line(12, -2, 2, 2, 3, DARK);
    prop(-34, 23, 11); prop(34, 23, 11);
    reset_tf();
}
static void draw_boss(const Enemy *e) {
    set_tf(e->x, e->y, 1, 1);
    int f = e->flash > 0;
    float finL[6] = {-40, -40, -75, -75, -20, -48}, finR[6] = {40, -40, 75, -75, 20, -48};
    poly(finL, 3, 0x7a1f35); poly(finR, 3, 0x7a1f35);
    f_ell(0, 0, 94.5f, 56.5f, DARK);
    f_ell(0, 0, 92, 54, f ? WHITE : LEVELS[level - 1].boss);
    g_alpha = 50;
    for (int i = 0; i < 2; i++) { float rx = i ? 50 : 20; s_ering(LX(0), LY(0), rx + 1.5f, 52, rx - 1.5f, 49, 0); }
    g_alpha = 255;
    rrect(-58, 28, 16, 24, 4, 0x444444); rrect(42, 28, 16, 24, 4, 0x444444);
    rrect(-30, 38, 60, 20, 8, 0xffcc4d);
    float a = atan2f(P.y - e->y, P.x - e->x);
    for (int s = -1; s <= 1; s += 2) {
        ell(s * 26.0f, -6, 15, 17, WHITE);
        f_ell(s * 26.0f + cosf(a) * 6, -6 + sinf(a) * 7, 6, 7, 0x111111);
        thick_line(s * 44.0f, -30, s * 10.0f, -18, 6, DARK);
    }
    rrect(-22, 16, 44, 12, 6, 0x5a0f22);
    for (int i = -18; i < 18; i += 9) { float t3[6] = {(float)i, 16, i + 4.5f, 22, (float)i + 9, 16}; f_poly(t3, 3, WHITE); }
    reset_tf();
}
static void draw_powerup(const Pow *p) {
    static const uint32_t cols[3] = {0xff4b2b, 0x3b7dd8, 0xe83e8c};
    static const char *lbl[3] = {"P", "M", "\xE2\x99\xA5"};
    float s = 1 + sinf(p->t * 8) * 0.08f;
    set_tf(p->x, p->y, s, s);
    ell(0, 0, 15, 15, cols[p->k]);
    g_alpha = 128; f_ell(-5, -6, 5, 3, WHITE); g_alpha = 255;
    reset_tf();
    draw_text(lbl[p->k], p->x, p->y + 1, 16, WHITE, 1);
}

static void hud_text(const char *s, float x, float y, float size, uint32_t c, int al) { float cx = camx, cy = camy; camx = camy = 0; draw_text(s, x, y, size, c, al); camx = cx; camy = cy; }
static void overlay(int a) { float cx = camx, cy = camy; camx = camy = 0; reset_tf(); g_alpha = a; f_rect(0, 0, GW, GH, 0); g_alpha = 255; camx = cx; camy = cy; }

static void draw_hud(void) {
    camx = camy = 0; reset_tf();
    char buf[64];
    if (state == ST_MENU) {
        overlay(90);
        hud_text("DISPARA", GW / 2, 115, 56, 0xffe14d, 1);
        hud_text("XD", GW / 2, 185, 74, 0xff6a3d, 1);
        hud_text("ELIGE DIFICULTAD", GW / 2, 262, 22, WHITE, 1);
        for (int i = 0; i < 3; i++) {
            float y = 330 + i * 58.0f; int sel = i == dsel;
            float lw = sel ? 3.5f : 1.6f;
            g_alpha = 255; f_rrect(GW / 2 - 185 - lw, y - 25 - lw, 370 + 2 * lw, 50 + 2 * lw, 14 + lw, sel ? DIFFS[i].col : DARK);
            /* interior: se reconstruye el fondo oscurecido/aclarado */
            g_alpha = 255; f_rrect(GW / 2 - 185, y - 25, 370, 50, 14, sel ? 0x6f8fa8 : 0x24526e);
            snprintf(buf, sizeof buf, "%s%s", sel ? "\xE2\x96\xB6 " : "", DIFFS[i].n);
            hud_text(buf, GW / 2, y - 7, 22, DIFFS[i].col, 1);
            hud_text(DIFFS[i].desc, GW / 2, y + 14, 12, WHITE, 1);
        }
        if (platform != PLAT_DESKTOP) {
            hud_text("\xE2\x86\x91\xE2\x86\x93: ELEGIR · \xE2\x9C\x95: JUGAR", GW / 2, 520, 15, WHITE, 1);
            hud_text("STICK / CRUCETA: MOVER", GW / 2, 546, 15, WHITE, 1);
            hud_text("\xE2\x9C\x95 / R2: AMETRALLADORA", GW / 2, 572, 15, WHITE, 1);
            hud_text("\xE2\x96\xA1 / R1: MISILES TELEDIRIGIDOS", GW / 2, 598, 15, WHITE, 1);
            hud_text(platform == PLAT_PS3 ? "START: PAUSA · \xE2\x96\xB3: MÚSICA" : "OPTIONS: PAUSA · \xE2\x96\xB3: MÚSICA", GW / 2, 624, 15, WHITE, 1);
        } else {
            hud_text("\xE2\x86\x91\xE2\x86\x93: ELEGIR · ENTER: JUGAR", GW / 2, 520, 15, WHITE, 1);
            hud_text("FLECHAS / WASD: MOVER", GW / 2, 546, 15, WHITE, 1);
            hud_text("Z / ESPACIO: AMETRALLADORA", GW / 2, 572, 15, WHITE, 1);
            hud_text("X / SHIFT: MISILES TELEDIRIGIDOS", GW / 2, 598, 15, WHITE, 1);
            hud_text("P: PAUSA · M: MÚSICA", GW / 2, 624, 15, WHITE, 1);
        }
        hud_text("5 NIVELES · UN ZEPELÍN JEFE EN CADA UNO", GW / 2, 656, 13, 0x8fd6ff, 1);
        snprintf(buf, sizeof buf, "RÉCORD (%s): %d", DIFFS[dsel].n, best[dsel]);
        hud_text(buf, GW / 2, 690, 17, 0xffe14d, 1);
        return;
    }
    itoa_pad(score, buf, 7); hud_text(buf, 12, 24, 20, WHITE, 0);
    snprintf(buf, sizeof buf, "REC %d", best[dsel]); hud_text(buf, GW - 12, 24, 14, 0xffe14d, 2);
    snprintf(buf, sizeof buf, "NIVEL %d/%d", level, NLEVELS); hud_text(buf, GW / 2, 18, 16, 0x7dffb0, 1);
    hud_text(D->n, GW / 2, 38, 11, D->col, 1);
    buf[0] = 0; for (int i = 0; i < P.lives && i < 5; i++) strcat(buf, "\xE2\x99\xA5");
    hud_text(buf, 12, GH - 22, 22, 0xff4b6e, 0);
    snprintf(buf, sizeof buf, "MISILES X%d", P.mis); hud_text(buf, GW - 12, GH - 22, 15, 0x8fd6ff, 2);
    strcpy(buf, "ARMA "); for (int i = 0; i < P.gun; i++) strcat(buf, "\xE2\x96\xAE"); hud_text(buf, GW / 2, GH - 22, 14, 0xffe14d, 1);
    Enemy *b = boss_ptr();
    if (b) { f_rect(GW / 2 - 122, 54, 244, 14, DARK); f_rect(GW / 2 - 120, 56, 240 * fmaxf(0, (float)b->hp / b->maxhp), 10, 0xff2e63); }
    int blink = ((int)(bgT * 2)) % 2;
    if (paused) {
        overlay(100); hud_text("PAUSA", GW / 2, GH / 2 - 20, 48, 0xffe14d, 1);
        hud_text(platform == PLAT_PS3 ? "START: SEGUIR · \xE2\x97\x8B: MENÚ" : platform == PLAT_CONSOLE ? "OPTIONS: SEGUIR · \xE2\x97\x8B: MENÚ" : "P: SEGUIR · ESC: MENÚ", GW / 2, GH / 2 + 40, 17, WHITE, 1);
    }
    const char *again = platform != PLAT_DESKTOP ? "\xE2\x9C\x95: VOLVER AL MENÚ" : "ENTER: VOLVER AL MENÚ";
    if (state == ST_WIN) {
        overlay(100);
        hud_text("¡MISIÓN", GW / 2, GH / 2 - 90, 50, 0x7dffb0, 1);
        hud_text("CUMPLIDA!", GW / 2, GH / 2 - 35, 50, 0x7dffb0, 1);
        snprintf(buf, sizeof buf, "PUNTOS: %d", score); hud_text(buf, GW / 2, GH / 2 + 25, 26, WHITE, 1);
        if (score >= best[dsel]) hud_text("¡NUEVO RÉCORD!", GW / 2, GH / 2 + 62, 22, 0xffe14d, 1);
        if (blink) hud_text(again, GW / 2, GH / 2 + 115, 19, 0xffe14d, 1);
    }
    if (state == ST_OVER) {
        overlay(100);
        hud_text("¡DERRIBADO!", GW / 2, GH / 2 - 60, 46, 0xff6a3d, 1);
        snprintf(buf, sizeof buf, "PUNTOS: %d", score); hud_text(buf, GW / 2, GH / 2, 26, WHITE, 1);
        if (score >= best[dsel] && score > 0) hud_text("¡NUEVO RÉCORD!", GW / 2, GH / 2 + 40, 22, 0x7dffb0, 1);
        if (blink) hud_text(again, GW / 2, GH / 2 + 100, 20, 0xffe14d, 1);
    }
}

void game_render(uint32_t *pixels) {
    fb = pixels;
    camx = shake > 0 ? rnd(-shake, shake) : 0;
    camy = shake > 0 ? rnd(-shake, shake) : 0;
    draw_bg();
    if (state != ST_MENU) {
        for (int i = 0; i < npu; i++) draw_powerup(&pu[i]);
        for (int i = 0; i < nen; i++) {
            Enemy *e = &en[i];
            if (e->type == E_BOSS) draw_boss(e);
            else if (e->type == E_BOMBER) draw_bomber(e);
            else draw_fighter(e, e->type == E_FIGHTER ? 0x6cbf3a : e->type == E_ZIG ? 0xf2c230 : 0x9b59d0);
        }
        reset_tf();
        for (int i = 0; i < npb; i++) {
            Bullet *b = &pb[i];
            if (b->missile) {
                int horiz = fabsf(b->vx) > fabsf(b->vy);
                float dx = b->vx / (fabsf(b->vx) + fabsf(b->vy) + 1e-3f), dy = b->vy / (fabsf(b->vx) + fabsf(b->vy) + 1e-3f);
                if (horiz) rrect(b->x - 9, b->y - 3.5f, 18, 7, 3, 0xdddddd); else rrect(b->x - 3.5f, b->y - 9, 7, 18, 3, 0xdddddd);
                f_ell(b->x - dx * 12, b->y - dy * 12, 3.5f, 3.5f, 0xffb03b);
                ell(b->x + dx * 9, b->y + dy * 9, 3.5f, 3.5f, 0xff4b2b);
            } else ell(b->x, b->y, 3.5f, 7, 0xffe14d);
        }
        for (int i = 0; i < nfx; i++) {
            Fx *f = &fx[i]; float k = f->t / f->life;
            if (f->k == 0) { float r = f->r + (f->max - f->r) * k; g_alpha = (int)(255 * (1 - k)); s_ering(LX(f->x), LY(f->y), r + 2, r + 2, r - 2, r - 2, WHITE); }
            else if (f->k == 2) { g_alpha = (int)(128 * (1 - k)); f_ell(f->x, f->y, f->r * (1 + k), f->r * (1 + k), 0xeeeeee); }
            else { g_alpha = 255; float r = f->r * (1 - k * 0.8f); f_ell(f->x, f->y, r, r, f->c); }
        }
        g_alpha = 255;
        if (state == ST_PLAY) draw_player();
        for (int i = 0; i < neb; i++) { Bullet *b = &eb[i]; ell(b->x, b->y, b->r, b->r, 0xff2e63); f_ell(b->x, b->y, 2, 2, WHITE); }
    }
    draw_clouds();
    if (theme()->storm && sinf(bgT * 1.3f) * sinf(bgT * 3.7f) > 0.985f) { reset_tf(); g_alpha = 90; f_rect(-20, -20, GW + 40, GH + 40, WHITE); g_alpha = 255; }
    for (int i = 0; i < ntx; i++) {
        Txt *t = &tx[i]; float k = t->t / t->life;
        float sz = t->size * (k < 0.15f ? 0.6f + k / 0.15f * 0.4f : 1);
        if (k > 0.7f) {
            /* desvanecer: se dibuja con alfa sobre el fondo */
            g_alpha = (int)(255 * (1 - k) / 0.3f);
        }
        draw_text(t->s, t->x, t->y - k * 30, sz, 0xffe14d, 1);
        g_alpha = 255;
    }
    draw_hud();
}

/* =====================================================================
 *  API de plataforma / depuración
 * ===================================================================== */
int game_get_best(int d) { return (d >= 0 && d < 3) ? best[d] : 0; }
void game_set_best(int d, int v) { if (d >= 0 && d < 3) best[d] = v; }
int game_best_changed(void) { int r = best_dirty; best_dirty = 0; return r; }
void game_debug_god(int on) { god = on; }
void game_debug_goto_level(int lv) { if (lv >= 1 && lv <= NLEVELS) { level = lv; T = 0; bossT = 1; } }
int game_debug_state(void) { return state; }
int game_debug_level(void) { return level; }
int game_debug_score(void) { return score; }

void game_render_icon(uint32_t *pixels) {
    /* ícono cuadrado GW x GW en la parte superior del buffer */
    fb = pixels; camx = camy = 0;
    const Theme *L = &LEVELS[0];
    for (int i = 0; i < GW * GH; i++) fb[i] = 0xFF000000u | L->sea;
    Island s = {70, 380, 70, {1, .9f, 1.05f, .8f, 1, .95f, .85f, 1.1f, .9f, 1}, 2};
    islands[0] = s;
    float xy[20];
    for (int pass = 0; pass < 3; pass++) {
        float sc = pass < 2 ? 1.18f : 0.9f, extra = pass == 0 ? 3 : 0;
        for (int k = 0; k < 10; k++) { float a = k / 10.0f * TAU, r = s.r * s.pts[k] * sc + extra; xy[2 * k] = s.x + cosf(a) * r; xy[2 * k + 1] = s.y + sinf(a) * r * 0.8f; }
        f_poly(xy, 10, pass == 0 ? DARK : pass == 1 ? L->sand : L->land);
    }
    g_alpha = 200; set_tf(380, 120, 1.4f, 1.4f);
    f_ell(0, 0, 34, 20, WHITE); f_ell(-28, 6, 22, 15, WHITE); f_ell(28, 6, 24, 16, WHITE); g_alpha = 255;
    P.x = 300; P.y = 300; P.inv = 0; P.tilt = 0;
    bgT = 0.37f;
    /* avión grande */
    set_tf(300, 262, 2.9f, 2.9f);
    g_alpha = 46; f_ell(14, 40, 26, 8, 0); g_alpha = 255;
    rrect(-36, -6, 72, 17, 8, 0xe8412c);
    f_ell(-24, 2.5f, 4, 4, WHITE); f_ell(24, 2.5f, 4, 4, WHITE);
    rrect(-15, 22, 30, 9, 4, 0xe8412c);
    ell(0, 4, 11, 31, 0xff6a3d);
    f_rect(-10, 14, 20, 4, WHITE);
    ell(0, -6, 6.5f, 9, 0x8fd6ff);
    f_ell(-2, -9, 2, 3, WHITE);
    ell(0, -27, 4, 4, 0xffd23f);
    reset_tf();
    draw_text("DISPARA", GW / 2, 50, 70, 0xffe14d, 1);
    draw_text("XD", GW / 2, 418, 96, 0xff6a3d, 1);
}
