/* Capa de plataforma SDL2: Mac/PC (escritorio) y PS4 (OpenOrbis, -DPLATFORM_PS4). */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"

#ifdef PLATFORM_PS4
#define WIN_W 1920
#define WIN_H 1080
/* índices de botones del control de PS4 en el port de SDL2 de OpenOrbis */
enum { PB_CROSS = 0, PB_CIRCLE = 1, PB_SQUARE = 2, PB_TRIANGLE = 3, PB_L1 = 4, PB_R1 = 5, PB_OPTIONS = 9,
       PB_UP = 13, PB_DOWN = 14, PB_LEFT = 15, PB_RIGHT = 16, PB_L2 = 18, PB_R2 = 19 };
#else
#define WIN_W GW
#define WIN_H GH
#endif

static uint32_t pixels[GW * GH];
static int audio_rate = 48000;
static SDL_AudioDeviceID adev = 0;
static char save_path[512];

/* ---------- récord ---------- */
static void load_best(void) {
    FILE *f = fopen(save_path, "r");
    if (!f) return;
    int a = 0, b = 0, c = 0, m = 1;
    if (fscanf(f, "%d %d %d %d", &a, &b, &c, &m) >= 3) { game_set_best(0, a); game_set_best(1, b); game_set_best(2, c); game_set_music(m); }
    fclose(f);
}
static void save_best(void) {
    FILE *f = fopen(save_path, "w");
    if (!f) return;
    fprintf(f, "%d %d %d %d\n", game_get_best(0), game_get_best(1), game_get_best(2), game_music_enabled());
    fclose(f);
}

/* ---------- audio ---------- */
static void audio_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    game_audio((int16_t *)stream, len / 4, audio_rate);
}
static void open_audio(void) {
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 48000; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = audio_cb;
    adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (!adev) { printf("[nube] sin audio: %s\n", SDL_GetError()); return; }
    audio_rate = have.freq;
    SDL_PauseAudioDevice(adev, 0);
}

/* ---------- entrada ---------- */
enum { K_UP = 1, K_DOWN = 2, K_CONFIRM = 4, K_BACK = 8, K_PAUSE = 16, K_MUSIC = 32 };
static int prev_bits = 0;
static float dz(float v) { if (v > -0.25f && v < 0.25f) return 0; return v; }

#ifdef PLATFORM_PS4
static SDL_Joystick *joy = NULL;
static void read_input(GameInput *in) {
    int bits = 0;
    memset(in, 0, sizeof *in);
    if (!joy && SDL_NumJoysticks() > 0) joy = SDL_JoystickOpen(0);
    if (joy) {
        #define B(i) SDL_JoystickGetButton(joy, i)
        float ax = dz(SDL_JoystickGetAxis(joy, 0) / 32767.0f), ay = dz(SDL_JoystickGetAxis(joy, 1) / 32767.0f);
        in->mx = ax + (B(PB_RIGHT) ? 1 : 0) - (B(PB_LEFT) ? 1 : 0);
        in->my = ay + (B(PB_DOWN) ? 1 : 0) - (B(PB_UP) ? 1 : 0);
        int r2axis = SDL_JoystickNumAxes(joy) > 5 && SDL_JoystickGetAxis(joy, 5) > 8000;
        in->fire = B(PB_CROSS) || B(PB_R2) || r2axis;
        in->missile = B(PB_SQUARE) || B(PB_R1) || B(PB_L1);
        if (B(PB_UP) || ay < -0.6f) bits |= K_UP;
        if (B(PB_DOWN) || ay > 0.6f) bits |= K_DOWN;
        if (B(PB_CROSS) || B(PB_OPTIONS)) bits |= K_CONFIRM;
        if (B(PB_CIRCLE)) bits |= K_BACK;
        if (B(PB_OPTIONS)) bits |= K_PAUSE;
        if (B(PB_TRIANGLE)) bits |= K_MUSIC;
        #undef B
    }
    int e = bits & ~prev_bits; prev_bits = bits;
    in->up = !!(e & K_UP); in->down = !!(e & K_DOWN); in->confirm = !!(e & K_CONFIRM);
    in->back = !!(e & K_BACK); in->pause = !!(e & K_PAUSE); in->music = !!(e & K_MUSIC);
}
#else
static SDL_GameController *pad = NULL;
static void read_input(GameInput *in) {
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    int bits = 0;
    memset(in, 0, sizeof *in);
    in->mx = (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) - (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]);
    in->my = (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) - (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]);
    in->fire = k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_J];
    in->missile = k[SDL_SCANCODE_X] || k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_K];
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) bits |= K_UP;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) bits |= K_DOWN;
    if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER] || k[SDL_SCANCODE_SPACE]) bits |= K_CONFIRM;
    if (k[SDL_SCANCODE_ESCAPE]) bits |= K_BACK;
    if (k[SDL_SCANCODE_P]) bits |= K_PAUSE;
    if (k[SDL_SCANCODE_M]) bits |= K_MUSIC;
    if (!pad) for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }
    if (pad) {
        #define GB(b) SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_##b)
        float ax = dz(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX) / 32767.0f);
        float ay = dz(SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY) / 32767.0f);
        in->mx += ax + GB(DPAD_RIGHT) - GB(DPAD_LEFT);
        in->my += ay + GB(DPAD_DOWN) - GB(DPAD_UP);
        in->fire |= GB(A) || SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 8000;
        in->missile |= GB(X) || GB(RIGHTSHOULDER) || GB(LEFTSHOULDER);
        if (GB(DPAD_UP) || ay < -0.6f) bits |= K_UP;
        if (GB(DPAD_DOWN) || ay > 0.6f) bits |= K_DOWN;
        if (GB(A) || GB(START)) bits |= K_CONFIRM;
        if (GB(B)) bits |= K_BACK;
        if (GB(START)) bits |= K_PAUSE;
        if (GB(Y)) bits |= K_MUSIC;
        #undef GB
    }
    int e = bits & ~prev_bits; prev_bits = bits;
    in->up = !!(e & K_UP); in->down = !!(e & K_DOWN); in->confirm = !!(e & K_CONFIRM);
    in->back = !!(e & K_BACK); in->pause = !!(e & K_PAUSE); in->music = !!(e & K_MUSIC);
}
#endif

/* ---------- presentación: escala el juego (480x720) al centro de la pantalla ---------- */
static SDL_Surface *stage = NULL;
static void present(SDL_Window *win) {
    SDL_Surface *ws = SDL_GetWindowSurface(win);
    if (!ws) return;
    if (!stage || stage->format->format != ws->format->format) {
        if (stage) SDL_FreeSurface(stage);
        stage = SDL_CreateRGBSurfaceWithFormat(0, GW, GH, 32, ws->format->format);
        if (!stage) return;
    }
    SDL_LockSurface(stage);
    SDL_ConvertPixels(GW, GH, SDL_PIXELFORMAT_ARGB8888, pixels, GW * 4, stage->format->format, stage->pixels, stage->pitch);
    SDL_UnlockSurface(stage);
    float s = (float)ws->h / GH;
    if (GW * s > ws->w) s = (float)ws->w / GW;
    SDL_Rect dst = { (ws->w - (int)(GW * s)) / 2, (ws->h - (int)(GH * s)) / 2, (int)(GW * s), (int)(GH * s) };
    Uint32 side = SDL_MapRGB(ws->format, 0x1b, 0x28, 0x38);
    SDL_Rect l = {0, 0, dst.x, ws->h}, r = {dst.x + dst.w, 0, ws->w - dst.x - dst.w, ws->h};
    SDL_Rect t = {0, 0, ws->w, dst.y}, b = {0, dst.y + dst.h, ws->w, ws->h - dst.y - dst.h};
    if (l.w > 0) SDL_FillRect(ws, &l, side);
    if (r.w > 0) SDL_FillRect(ws, &r, side);
    if (t.h > 0) SDL_FillRect(ws, &t, side);
    if (b.h > 0) SDL_FillRect(ws, &b, side);
    if (dst.w == GW && dst.h == GH) SDL_BlitSurface(stage, NULL, ws, &dst);
    else SDL_BlitScaled(stage, NULL, ws, &dst);
    SDL_UpdateWindowSurface(win);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO
#ifndef PLATFORM_PS4
                 | SDL_INIT_GAMECONTROLLER
#endif
                 ) != 0) {
        printf("[nube] SDL_Init falló: %s\n", SDL_GetError());
        for (;;) SDL_Delay(1000);
    }
#ifdef PLATFORM_PS4
    snprintf(save_path, sizeof save_path, "/data/dispara_xd.sav");
    SDL_Window *win = SDL_CreateWindow("main", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIN_W, WIN_H, 0);
    game_init((unsigned)SDL_GetTicks() * 2654435761u + 1, PLAT_CONSOLE);
#else
    char *pref = SDL_GetPrefPath("DisparaXD", "DisparaXD");
    snprintf(save_path, sizeof save_path, "%srecord.txt", pref ? pref : "");
    if (pref) SDL_free(pref);
    int ww = WIN_W, wh = WIN_H;
    SDL_Rect ub;
    if (SDL_GetDisplayUsableBounds(0, &ub) == 0 && ub.h - 40 < wh) { wh = (ub.h - 40) / 4 * 4; ww = wh * GW / GH; }
    SDL_Window *win = SDL_CreateWindow("Dispara XD", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, ww, wh, SDL_WINDOW_RESIZABLE);
    game_init((unsigned)SDL_GetPerformanceCounter() | 1u, PLAT_DESKTOP);
#endif
    if (!win) { printf("[nube] no se pudo crear la ventana: %s\n", SDL_GetError()); for (;;) SDL_Delay(1000); }
    load_best();
    open_audio();

    /* límite de cuadros para pruebas automáticas: NUBE_FRAMES=n */
    const char *ef = SDL_getenv("NUBE_FRAMES");
    long max_frames = ef ? atol(ef) : -1, frame = 0;

    Uint64 freq = SDL_GetPerformanceFrequency(), last = SDL_GetPerformanceCounter();
    const double target = 1.0 / 60.0;
    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
#ifndef PLATFORM_PS4
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_F && !ev.key.repeat) {
                Uint32 fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
            if (ev.type == SDL_CONTROLLERDEVICEREMOVED && pad) { SDL_GameControllerClose(pad); pad = NULL; }
#endif
        }
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - last) / freq);
        last = now;

        GameInput in;
        read_input(&in);
        if (adev) SDL_LockAudioDevice(adev);
        game_update(&in, dt);
        if (adev) SDL_UnlockAudioDevice(adev);
        if (game_best_changed()) save_best();
        if (in.music) save_best();

        game_render(pixels);
        present(win);

        double spent = (double)(SDL_GetPerformanceCounter() - now) / freq;
        if (spent < target) SDL_Delay((Uint32)((target - spent) * 1000.0));
        if (max_frames > 0 && ++frame >= max_frames) {
            const char *dump = SDL_getenv("NUBE_DUMP"); /* prueba: guarda lo que se envió a la pantalla */
            if (dump) { SDL_Surface *ws = SDL_GetWindowSurface(win); if (ws) SDL_SaveBMP(ws, dump); }
            running = 0;
        }
    }
    save_best();
    if (adev) SDL_CloseAudioDevice(adev);
    SDL_Quit();
    return 0;
}
