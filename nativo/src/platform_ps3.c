/* Capa de plataforma PS3 (PSL1GHT): video RSX por software, audio y control. */
#include <ppu-lv2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <sysutil/video.h>
#include <sysutil/sysutil.h>
#include <rsx/gcm_sys.h>
#include <rsx/rsx.h>
#include <io/pad.h>
#include <audio/audio.h>
#include <lv2/systime.h>
#include "game.h"

#define CB_SIZE   0x100000
#define HOST_SIZE (32 * 1024 * 1024)
#define SAVE_PATH "/dev_hdd0/game/DSXD00001/USRDIR/record.txt"

typedef struct { int width, height, id; uint32_t *ptr; uint32_t offset; } Buffer;

static uint32_t pixels[GW * GH];
static gcmContextData *context;
static Buffer buffers[2];
static int running = 1;

/* ---------- video (basado en los ejemplos de PSL1GHT) ---------- */
#define GCM_LABEL_INDEX 255
static void wait_rsx_idle(void) {
    u32 label = 1;
    rsxSetWriteBackendLabel(context, GCM_LABEL_INDEX, label);
    rsxSetWaitLabel(context, GCM_LABEL_INDEX, label);
    label++;
    rsxSetWriteBackendLabel(context, GCM_LABEL_INDEX, label);
    rsxFlushBuffer(context);
    while (*(vu32 *)gcmGetLabelAddress(GCM_LABEL_INDEX) != label) usleep(30);
}
static int init_screen(u16 *w, u16 *h) {
    void *host = memalign(1024 * 1024, HOST_SIZE);
    if (!host) return 0;
    rsxInit(&context, CB_SIZE, HOST_SIZE, host);
    if (!context) return 0;
    videoState state; videoResolution res; videoConfiguration vc;
    if (videoGetState(0, 0, &state) != 0 || state.state != 0) return 0;
    if (videoGetResolution(state.displayMode.resolution, &res) != 0) return 0;
    memset(&vc, 0, sizeof vc);
    vc.resolution = state.displayMode.resolution;
    vc.format = VIDEO_BUFFER_FORMAT_XRGB;
    vc.pitch = res.width * sizeof(u32);
    vc.aspect = state.displayMode.aspect;
    wait_rsx_idle();
    if (videoConfigure(0, &vc, NULL, 0) != 0) return 0;
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    gcmResetFlipStatus();
    *w = res.width; *h = res.height;
    for (int i = 0; i < 2; i++) {
        Buffer *b = &buffers[i];
        b->ptr = (uint32_t *)rsxMemalign(64, res.width * res.height * 4);
        if (!b->ptr || rsxAddressToOffset(b->ptr, &b->offset) != 0) return 0;
        if (gcmSetDisplayBuffer(i, b->offset, res.width * 4, res.width, res.height) != 0) return 0;
        b->width = res.width; b->height = res.height; b->id = i;
    }
    return 1;
}
static void wait_flip(void) { while (gcmGetFlipStatus() != 0) usleep(200); gcmResetFlipStatus(); }
static void do_flip(int id) { if (gcmSetFlip(context, id) == 0) { rsxFlushBuffer(context); gcmSetWaitFlip(context); } }

/* escala 480x720 al centro de la pantalla (vecino más cercano) */
static int dst_x, dst_y, dst_w, dst_h;
static int *xmap;
static void setup_scaler(int sw, int sh) {
    float s = (float)sh / GH;
    if (GW * s > sw) s = (float)sw / GW;
    dst_w = (int)(GW * s); dst_h = (int)(GH * s);
    dst_x = (sw - dst_w) / 2; dst_y = (sh - dst_h) / 2;
    xmap = (int *)malloc(sizeof(int) * dst_w);
    for (int x = 0; x < dst_w; x++) { int v = (int)(x / s); xmap[x] = v < GW ? v : GW - 1; }
}
static void clear_buffer(Buffer *b) {
    const uint32_t side = 0x1b2838;
    for (int i = 0; i < b->width * b->height; i++) b->ptr[i] = side;
}
static void blit(Buffer *b) {
    float inv = (float)GH / dst_h;
    for (int y = 0; y < dst_h; y++) {
        int sy = (int)(y * inv); if (sy >= GH) sy = GH - 1;
        const uint32_t *src = pixels + sy * GW;
        uint32_t *dst = b->ptr + (dst_y + y) * b->width + dst_x;
        for (int x = 0; x < dst_w; x++) dst[x] = src[xmap[x]] & 0x00FFFFFF;
    }
}

/* ---------- audio ---------- */
static u32 audio_port;
static audioPortConfig acfg;
static int audio_ok = 0;
static u32 audio_block = 1;
static int16_t abuf[AUDIO_BLOCK_SAMPLES * 2];
static void init_audio(void) {
    audioPortParam p;
    if (audioInit() != 0) return;
    p.numChannels = AUDIO_PORT_2CH; p.numBlocks = AUDIO_BLOCK_16; p.attrib = 0; p.level = 1;
    if (audioPortOpen(&p, &audio_port) != 0) return;
    if (audioGetPortConfig(audio_port, &acfg) != 0) return;
    memset((void *)(u64)acfg.audioDataStart, 0, acfg.portSize);
    if (audioPortStart(audio_port) != 0) return;
    audio_ok = 1;
}
static void pump_audio(void) {
    if (!audio_ok) return;
    u64 hw = *(volatile u64 *)(u64)acfg.readIndex;
    int guard = 16;
    while (audio_block != hw && guard--) {
        float *dst = (float *)(u64)acfg.audioDataStart + 2 * AUDIO_BLOCK_SAMPLES * audio_block;
        game_audio(abuf, AUDIO_BLOCK_SAMPLES, 48000);
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES * 2; i++) dst[i] = abuf[i] * (1.0f / 32768.0f);
        audio_block = (audio_block + 1) % AUDIO_BLOCK_16;
    }
}

/* ---------- récord ---------- */
static void load_best(void) {
    FILE *f = fopen(SAVE_PATH, "r");
    if (!f) return;
    int a = 0, b = 0, c = 0, m = 1;
    if (fscanf(f, "%d %d %d %d", &a, &b, &c, &m) >= 3) { game_set_best(0, a); game_set_best(1, b); game_set_best(2, c); game_set_music(m); }
    fclose(f);
}
static void save_best(void) {
    FILE *f = fopen(SAVE_PATH, "w");
    if (!f) return;
    fprintf(f, "%d %d %d %d\n", game_get_best(0), game_get_best(1), game_get_best(2), game_music_enabled());
    fclose(f);
}

/* ---------- control ---------- */
enum { K_UP = 1, K_DOWN = 2, K_CONFIRM = 4, K_BACK = 8, K_PAUSE = 16, K_MUSIC = 32 };
static int prev_bits = 0;
static padData last;
static float axis(unsigned v) { float f = ((int)v - 128) / 127.0f; if (f > -0.25f && f < 0.25f) return 0; return f < -1 ? -1 : f > 1 ? 1 : f; }
static void read_input(GameInput *in) {
    padInfo info; padData pd;
    memset(in, 0, sizeof *in);
    int bits = 0;
    if (ioPadGetInfo(&info) == 0) {
        for (int i = 0; i < MAX_PADS; i++) {
            if (!info.status[i]) continue;
            if (ioPadGetData(i, &pd) == 0 && pd.len > 0) last = pd;   /* len 0 = sin cambios */
            break;
        }
    }
    if (last.len > 0) {
        float ax = axis(last.ANA_L_H), ay = axis(last.ANA_L_V);
        in->mx = ax + (last.BTN_RIGHT ? 1 : 0) - (last.BTN_LEFT ? 1 : 0);
        in->my = ay + (last.BTN_DOWN ? 1 : 0) - (last.BTN_UP ? 1 : 0);
        in->fire = last.BTN_CROSS || last.BTN_R2;
        in->missile = last.BTN_SQUARE || last.BTN_R1 || last.BTN_L1;
        if (last.BTN_UP || ay < -0.6f) bits |= K_UP;
        if (last.BTN_DOWN || ay > 0.6f) bits |= K_DOWN;
        if (last.BTN_CROSS || last.BTN_START) bits |= K_CONFIRM;
        if (last.BTN_CIRCLE) bits |= K_BACK;
        if (last.BTN_START) bits |= K_PAUSE;
        if (last.BTN_TRIANGLE) bits |= K_MUSIC;
    }
    int e = bits & ~prev_bits; prev_bits = bits;
    in->up = !!(e & K_UP); in->down = !!(e & K_DOWN); in->confirm = !!(e & K_CONFIRM);
    in->back = !!(e & K_BACK); in->pause = !!(e & K_PAUSE); in->music = !!(e & K_MUSIC);
}

static void sysutil_cb(u64 status, u64 param, void *ud) {
    (void)param; (void)ud;
    if (status == SYSUTIL_EXIT_GAME) running = 0;
}

int main(s32 argc, const char *argv[]) {
    (void)argc; (void)argv;
    u16 w, h;
    sysUtilRegisterCallback(0, sysutil_cb, NULL);
    if (!init_screen(&w, &h)) return 1;
    setup_scaler(w, h);
    clear_buffer(&buffers[0]); clear_buffer(&buffers[1]);
    ioPadInit(7);
    memset(&last, 0, sizeof last);
    game_init((unsigned)sysGetSystemTime() | 1u, PLAT_PS3);
    load_best();
    init_audio();

    do_flip(1);
    int cur = 0;
    s64 prev = sysGetSystemTime();
    while (running) {
        sysUtilCheckCallback();
        s64 now = sysGetSystemTime();
        float dt = (now - prev) / 1000000.0f;
        prev = now;

        GameInput in;
        read_input(&in);
        game_update(&in, dt);
        if (game_best_changed() || in.music) save_best();
        pump_audio();

        game_render(pixels);
        wait_flip();
        blit(&buffers[cur]);
        do_flip(buffers[cur].id);
        cur ^= 1;
        pump_audio();
    }
    save_best();
    gcmSetWaitFlip(context);
    rsxFinish(context, 1);
    if (audio_ok) { audioPortStop(audio_port); audioPortClose(audio_port); audioQuit(); }
    ioPadEnd();
    return 0;
}
