/* Dispara XD — núcleo portátil (sin dependencias).
 * La plataforma (SDL2 en Mac/PS4, PSL1GHT en PS3) solo debe:
 *   - llamar game_update() con la entrada y el tiempo transcurrido,
 *   - llamar game_render() con un buffer GW x GH de píxeles 0xAARRGGBB,
 *   - llamar game_audio() desde su hilo de audio (proteger con un lock).
 */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

#define GW 480
#define GH 720

typedef struct {
    float mx, my;        /* movimiento -1..1 (stick o teclas) */
    int fire, missile;   /* mantenidos */
    int up, down;        /* pulsados este frame (menú) */
    int confirm;         /* pulsado este frame: empezar / aceptar */
    int back;            /* pulsado este frame: volver al menú (en pausa) */
    int pause;           /* pulsado este frame */
    int music;           /* pulsado este frame: activar/desactivar música */
} GameInput;

enum { PLAT_DESKTOP = 0, PLAT_CONSOLE = 1, PLAT_PS3 = 2 };

void game_init(unsigned seed, int platform);
void game_update(const GameInput *in, float dt);
void game_render(uint32_t *pixels);               /* GW*GH, 0xAARRGGBB */
void game_audio(int16_t *out, int frames, int rate); /* estéreo intercalado */

int  game_get_best(int diff);
void game_set_best(int diff, int value);
int  game_best_changed(void);                     /* 1 si hay que guardar (se reinicia) */
int  game_music_enabled(void);
void game_set_music(int on);

/* Utilidades para pruebas */
void game_debug_god(int on);
void game_debug_goto_level(int level);
int  game_debug_state(void);  /* 0 menú, 1 jugando, 2 derribado, 3 victoria */
int  game_debug_level(void);
int  game_debug_score(void);
void game_render_icon(uint32_t *pixels);          /* dibuja un ícono en GW x GW */
#endif
