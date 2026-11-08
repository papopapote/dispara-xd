/* Prueba del núcleo sin ventana: simula partidas, guarda capturas PPM y revisa el audio. */
#include "../src/game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static uint32_t px[GW * GH];
static void save_ppm(const char *path, int w, int h) {
    FILE *f = fopen(path, "wb"); if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) { unsigned char c[3] = {(unsigned char)(px[i] >> 16), (unsigned char)(px[i] >> 8), (unsigned char)px[i]}; fwrite(c, 1, 3, f); }
    fclose(f);
}
static GameInput in;
static void step(int n) { for (int i = 0; i < n; i++) { game_update(&in, 1.0f / 60); game_render(px); memset(&in.up, 0, sizeof(int) * 6); } }
static int audio_check(int frames) {
    static int16_t buf[48000 * 2]; int peak = 0;
    game_audio(buf, frames, 48000);
    for (int i = 0; i < frames * 2; i++) { int v = abs(buf[i]); if (v > peak) peak = v; }
    return peak;
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : ".";
    char path[512];
    game_init(42, PLAT_CONSOLE);
    step(30);
    snprintf(path, sizeof path, "%s/menu.ppm", out); save_ppm(path, GW, GH);
    printf("audio menú (pico): %d\n", audio_check(24000));

    for (int d = 0; d < 3; d++) {
        /* elegir dificultad d desde el menú */
        while (1) { step(1); if (game_debug_state() == 0) break; }
        static int pos = 1; /* el menú recuerda la última dificultad */
        while (pos != d) { in.down = 1; step(1); pos = (pos + 1) % 3; }
        in.confirm = 1; step(1);
        game_debug_god(1);
        in.fire = 1; in.missile = 1;
        int seen[6] = {0}, t = 0, shots = 0;
        while (game_debug_state() == 1 && t < 60 * 600) {
            in.mx = sinf(t * 0.02f); in.my = cosf(t * 0.013f) * 0.5f;
            step(1); t++;
            seen[game_debug_level()] = 1;
            if (d == 1 && t % 600 == 0 && shots < 12) { snprintf(path, sizeof path, "%s/juego_%02d.ppm", out, shots++); save_ppm(path, GW, GH); }
            if (t % 3000 == 0) { int pk = audio_check(4800); if (pk == 0) printf("  aviso: audio silencioso en t=%d\n", t); }
        }
        printf("dificultad %d: estado=%d niveles=%d%d%d%d%d puntos=%d tiempo=%ds récord=%d\n", d, game_debug_state(),
               seen[1], seen[2], seen[3], seen[4], seen[5], game_debug_score(), t / 60, game_get_best(d));
        if (d == 1) { snprintf(path, sizeof path, "%s/victoria.ppm", out); step(40); save_ppm(path, GW, GH); }
        in.fire = in.missile = 0; in.mx = in.my = 0;
        game_debug_god(0);
        step(30); in.confirm = 1; step(1);
    }
    /* partida real (sin dios) en normal, quieto: debe perder */
    in.confirm = 1; step(1);
    int t = 0; while (game_debug_state() == 1 && t < 60 * 400) { step(1); t++; }
    printf("partida sin disparar: estado=%d tras %ds\n", game_debug_state(), t / 60);
    snprintf(path, sizeof path, "%s/derribado.ppm", out); step(30); save_ppm(path, GW, GH);
    printf("audio derribado (pico): %d\n", audio_check(24000));
    in.confirm = 1; step(1);
    /* jefe del nivel 5 y pausa */
    in.confirm = 1; step(1); game_debug_god(1); game_debug_goto_level(5); in.fire = 1;
    step(60 * 5);
    snprintf(path, sizeof path, "%s/jefe5.ppm", out); save_ppm(path, GW, GH);
    in.pause = 1; step(1); step(10);
    snprintf(path, sizeof path, "%s/pausa.ppm", out); save_ppm(path, GW, GH);
    in.back = 1; step(1);
    printf("tras pausa + volver: estado=%d\n", game_debug_state());
    printf("audio juego (pico): %d\n", audio_check(48000));
    game_render_icon(px);
    snprintf(path, sizeof path, "%s/icon.ppm", out); save_ppm(path, GW, GW);
    printf("OK\n");
    return 0;
}
