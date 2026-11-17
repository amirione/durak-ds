// DURAK DS - main.c
// Both screens are 16-bit bitmap backgrounds that ui.h draws into.
// Top screen = table, bottom screen = hand + menus (stylus).
// Rules live in game.h, drawing and input handling in ui.h, fonts in font.h.

#include <nds.h>
#include <time.h>
#include "ui.h"

static UI ui;

int main(void) {
    lcdMainOnTop();                         // main engine -> upper screen
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);

    int bgTop = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    int bgBot = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    uint16_t *top = bgGetGfxPtr(bgTop);
    uint16_t *bot = bgGetGfxPtr(bgBot);

    ui_init(&ui);

    touchPosition tp;
    tp.px = 0; tp.py = 0;
    uint32_t frames = 0;
    uint32_t clock = (uint32_t)time(NULL);

    while (pmMainLoop()) {
        swiWaitForVBlank();
        scanKeys();
        frames++;

        int down = keysDown(), held = keysHeld(), up = keysUp();
        int touch = 0, tx = 0, ty = 0;
        if (held & KEY_TOUCH) {
            touchRead(&tp);
            tx = tp.px; ty = tp.py;
            touch = (down & KEY_TOUCH) ? 1 : 2;
        } else if (up & KEY_TOUCH) {
            touch = 3;
        }

        // new games are shuffled from the moment of the tap, so every deal differs
        uint32_t seed = frames * 2654435761u ^ clock * 40503u;
        ui_update(&ui, touch, tx, ty, (down & KEY_START) != 0, seed);
        ui_render(&ui, top, bot);
    }
    return 0;
}
