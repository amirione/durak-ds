// DURAK DS - main.c
// Both screens are 16-bit bitmap backgrounds that ui.h draws into.
// Top screen = table, bottom screen = hand + menus (stylus).
// Rules live in game.h, drawing and input handling in ui.h, fonts in font.h.
//
// Profiler: press SELECT to show timings on the touch screen (off by default, nothing is drawn unless asked).
//   UPD = game logic + input, REN = redraw, FRAME = everything in one loop pass, MAX = worst frame so far,
//   LATE = frames that took longer than one video frame (16.7 ms) and so missed a VBlank.

#include <nds.h>
#include <stdio.h>
#include <time.h>
#include "ui.h"

static UI ui;

#define FRAME_TICKS 560190u            /* bus ticks in one video frame: 33513982 Hz / 59.83 Hz */

static void show_stats(uint16_t *bot, uint32_t upd, uint32_t ren, uint32_t frame, uint32_t worst, unsigned late) {
    char line[96];
    unsigned long u = timerTicks2usec(upd), r = timerTicks2usec(ren), f = timerTicks2usec(frame), w = timerTicks2usec(worst);
    sniprintf(line, sizeof line, "UPD %lu.%02lu REN %lu.%02lu FRAME %lu.%02lu MAX %lu.%02lu MS LATE %u",
              u / 1000, (u % 1000) / 10, r / 1000, (r % 1000) / 10, f / 1000, (f % 1000) / 10, w / 1000, (w % 1000) / 10, late);
    fill_rect(bot, 2, 19, SCR_W - 4, 13, C_PAPER);
    text_left(bot, &F9, 4, 22, line, late ? C_ACCENT : C_GREY, 0);
}

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

    bool stats = false;
    uint32_t tUpd = 0, tRen = 0, tFrame = 0, tWorst = 0;
    unsigned late = 0;

    while (pmMainLoop()) {
        swiWaitForVBlank();
        cpuStartTiming(0);
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

        if (down & KEY_SELECT) {
            stats = !stats;
            tWorst = 0; late = 0;
            ui.dirtyBot = true;             // restores the area the profiler line covered
        }

        // new games are shuffled from the moment of the tap, so every deal differs
        uint32_t seed = frames * 2654435761u ^ clock * 40503u;
        ui_update(&ui, touch, tx, ty, (down & KEY_START) != 0, seed);
        uint32_t t1 = cpuGetTiming();
        ui_render(&ui, top, bot);
        uint32_t t2 = cpuGetTiming();

        if (stats) {
            tUpd = t1; tRen = t2 - t1; tFrame = t2;
            if (tFrame > tWorst) tWorst = tFrame;
            if (tFrame > FRAME_TICKS) late++;
            show_stats(bot, tUpd, tRen, tFrame, tWorst, late);
        }
    }
    return 0;
}
