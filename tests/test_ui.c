// UI tests: menu navigation with real touch coordinates, full games played through the UI (cheating included),
// per-screen redraw rules, text fitting, and a random-stylus fuzz run. No DS toolchain needed.
#include <stdio.h>
#include <stdlib.h>
#include "ui.h"

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t TOP[SCR_W * SCR_H], BOT[SCR_W * SCR_H];
static uint32_t SEED = 1;
static int maxStatusW = 0;
static char widestMsg[96];

static void frame(UI *u, int touch, int x, int y, long *f) {
    ui_update(u, touch, x, y, false, SEED); ui_render(u, TOP, BOT); (*f)++;
    if (u->scr == UI_GAME) {
        char tmp[8]; int w = text_width(&F9, ui_status(u, tmp), 0);
        if (w > maxStatusW) { maxStatusW = w; strncpy(widestMsg, ui_status(u, tmp), 95); widestMsg[95] = 0; }
    }
}
static void tap(UI *u, int x, int y, long *f) { frame(u, 1, x, y, f); frame(u, 3, x, y, f); }
static void tap_card(UI *u, int i, long *f) {
    int x, st; hand_geometry(u->g.handN[0], i, &x, &st);
    int w = (i == u->g.handN[0] - 1) ? HAND_CW : st;
    tap(u, x + w / 2, HAND_Y + 30, f);
}
static void tap_btn(UI *u, int id, long *f) { int x, y, w, h; btn_rect(u, id, &x, &y, &w, &h); tap(u, x + w / 2, y + h / 2, f); }

static int count_color(const uint16_t *fb, int x0, int y0, int w, int h, uint16_t c) {
    int n = 0;
    for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) if (fb[y * SCR_W + x] == c) n++;
    return n;
}

static void t_menus_and_icons(void) {
    printf("menus: navigation, suit icons, cheat toggle, back\n");
    UI u; ui_init(&u); long f = 0;
    ui_render(&u, TOP, BOT);
    // each mode column shows a real suit in its card icon: the diamond (middle) is red, the others black
    int redL = count_color(BOT, 10, 60, 65, 75, C_ACCENT), redM = count_color(BOT, 95, 60, 65, 75, C_ACCENT), redR = count_color(BOT, 180, 60, 65, 75, C_ACCENT);
    int inkL = count_color(BOT, 10, 60, 65, 75, C_INK), inkR = count_color(BOT, 180, 60, 65, 75, C_INK);
    CHECK(redM > 120 && redL == 0 && redR == 0, "red diamond only in the middle column (L %d, M %d, R %d)", redL, redM, redR);
    CHECK(inkL > 300 && inkR > 300, "spade and club drawn in ink (L %d, R %d)", inkL, inkR);

    tap(&u, 40, 100, &f);  CHECK(u.scr == UI_BOTS && u.mode == GM_CLASSIC, "classic selected");
    tap(&u, 128, 175, &f); CHECK(u.scr == UI_MODE, "back to the mode menu");
    tap(&u, 128, 100, &f); CHECK(u.mode == GM_PODKIDNOY, "podkidnoy selected");
    tap(&u, 128, 160, &f); CHECK(u.cheat, "cheating toggled on");
    tap(&u, 128, 160, &f); CHECK(!u.cheat, "cheating toggled off");
    tap(&u, 128, 175, &f); tap(&u, 220, 100, &f); CHECK(u.mode == GM_PEREVODNOY, "perevodnoy selected");
    int mn = ui_min_bots(&u);
    CHECK(mn == 2, "perevodnoy needs at least 2 bots");
    tap(&u, 20, 80, &f);   CHECK(u.scr == UI_GAME && u.bots == 2, "first option is 2 bots (got %d)", u.bots);
    // START returns to the menu
    ui_update(&u, 0, 0, 0, true, SEED); CHECK(u.scr == UI_MODE, "START opens the menu");
    // bot-count icons: spade, club, red diamond
    tap(&u, 128, 100, &f);
    ui_render(&u, TOP, BOT);
    CHECK(count_color(BOT, 170, 40, 80, 80, C_ACCENT) > 120, "third bot option shows the red diamond");
}

static void t_redraw_rules(void) {
    printf("redraw: hover touches only the touch screen\n");
    UI u; ui_init(&u); long f = 0;
    u.mode = GM_CLASSIC; u.bots = 2; ui_start_game(&u, 99);
    ui_render(&u, TOP, BOT);
    CHECK(!u.dirtyTop && !u.dirtyBot, "clean after render");
    ui_update(&u, 1, 60, HAND_Y + 30, false, SEED);
    CHECK(u.dirtyBot && !u.dirtyTop, "pressing a card redraws the bottom screen only (top %d bottom %d)", u.dirtyTop, u.dirtyBot);
    ui_render(&u, TOP, BOT);
    ui_update(&u, 2, 62, HAND_Y + 30, false, SEED);
    CHECK(!u.dirtyTop, "moving along the same card changes nothing on top");
    (void)f;
}

// A sensible player. When cheating is on it also tries illegal extra cards now and then.
static void play_game(int mode, int bots, bool cheat, uint32_t seed, long *frames, int *outcome, int *cheated) {
    UI u; ui_init(&u); long f = 0; SEED = seed; u.cheat = cheat;
    tap(&u, mode * 85 + 40, 100, &f);
    { int mn = ui_min_bots(&u), k = 4 - mn; tap(&u, (bots - mn) * 256 / k + 20, 80, &f); if (cheat) {} }
    // the cheat toggle lives on the bot-count screen; set it before choosing (re-run navigation properly)
    ui_init(&u); u.cheat = false; f = 0;
    tap(&u, mode * 85 + 40, 100, &f);
    if (cheat) tap(&u, 128, 160, &f);
    { int mn = ui_min_bots(&u), k = 4 - mn; tap(&u, (bots - mn) * 256 / k + 20, 80, &f); }
    if (u.scr != UI_GAME || u.bots != bots || u.cheat != cheat) { printf("  navigation failed (scr %d bots %d cheat %d)\n", u.scr, u.bots, u.cheat); *outcome = -9; return; }
    uint32_t r = seed * 2654435761u + 1; int tries = 0;
    while (u.g.over == GO_NONE && f < 400000) {
        Game *g = &u.g; bool acted = false;
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        bool tempt = cheat && (r % 3 == 0);
        if (g->active == 0 && g->attacker == 0 && g->phase == PH_ATTACK) {
            int legal = -1, illegal = -1;
            for (int i = 0; i < g->handN[0]; i++) { if (g_can_attack_card(g, g->hands[0][i])) { if (legal < 0) legal = i; } else if (illegal < 0 && g->tableN > 0) illegal = i; }
            if (tempt && illegal >= 0 && g_defender_can_take_more(g) && !g->cheatBan) { tap_card(&u, illegal, &f); tries++; acted = true; }
            else if (legal >= 0) { tap_card(&u, legal, &f); acted = true; }
            else if (g_can_pass(g, 0)) { tap_btn(&u, ID_PASS, &f); acted = true; }
        } else if (g->active == 0 && g->defender == 0 && g->phase == PH_DEFEND) {
            int o = g_open_pair(g);
            for (int i = 0; i < g->handN[0] && !acted; i++) if (g_can_transfer_card(g, 0, g->hands[0][i])) { tap_card(&u, i, &f); acted = true; }
            for (int i = 0; i < g->handN[0] && !acted; i++) if (o >= 0 && g_beats(g->table[o].attack, g->hands[0][i], g->trump)) { tap_card(&u, i, &f); acted = true; }
            if (!acted && g_can_take(g, 0)) { tap_btn(&u, ID_TAKE, &f); acted = true; }
        } else if (ui_throw_window(&u)) {
            int legal = -1, illegal = -1;
            for (int i = 0; i < g->handN[0]; i++) if (g_can_throw_card(g, 0, g->hands[0][i])) { if (g_rank_ok(g, g->hands[0][i])) { if (legal < 0) legal = i; } else if (illegal < 0) illegal = i; }
            if (tempt && illegal >= 0) { tap_card(&u, illegal, &f); tries++; acted = true; }
            else if (legal >= 0) { tap_card(&u, legal, &f); acted = true; }
            else { tap_btn(&u, ID_SKIP, &f); acted = true; }
        }
        if (!acted) frame(&u, 0, 0, 0, &f);
    }
    *frames = f; *cheated = tries;
    CHECK(u.g.stat_repairs == 0, "safety net ran during a UI game");
    *outcome = u.g.over == GO_NONE ? -1 : (u.g.over == GO_DRAW ? 100 : u.g.over);   // -1 = never finished, 100 = draw
    // game over screen: both buttons present and working
    if (u.g.over != GO_NONE) {
        tap_btn(&u, ID_AGAIN, &f);
        if (!(u.scr == UI_GAME && u.g.over == GO_NONE && u.g.handN[0] == 6)) { printf("  AGAIN did not start a new game\n"); *outcome = -9; }
    }
}

static void t_full_games(void) {
    printf("full games through the UI (touch coordinates), every mode / bot count / cheating on+off\n");
    const char *mn[] = { "classic", "podkidnoy", "perevodnoy" };
    long worst = 0;
    for (int mode = 0; mode < 3; mode++) for (int bots = (mode == 2 ? 2 : 1); bots <= 3; bots++) for (int cheat = 0; cheat < 2; cheat++) {
        int done = 0, stuck = 0, attempts = 0; long maxf = 0;
        for (int g = 0; g < 80; g++) {
            long fr = 0; int out = 0, ch = 0;
            play_game(mode, bots, cheat, (uint32_t)(g + 1) * 2246822519u + mode * 97u + bots * 13u + cheat, &fr, &out, &ch);
            if (out == -9) { CHECK(0, "game setup failed"); continue; }
            if (out == -1) stuck++; else done++;
            attempts += ch; if (fr > maxf) maxf = fr;
        }
        if (maxf > worst) worst = maxf;
        printf("  %-10s bots=%d cheating=%d: finished %2d/80, cheat attempts %4d, longest %ld frames\n", mn[mode], bots, cheat, done, attempts, maxf);
        CHECK(stuck == 0, "%d games did not finish", stuck);
    }
    CHECK(maxStatusW <= 244, "status line must fit the screen: widest %d px", maxStatusW);
    printf("  widest status line: %d px of 244: %s\n", maxStatusW, widestMsg);
}

static void t_fuzz(void) {
    printf("fuzz: 1,500,000 frames of random stylus input\n");
    UI u; ui_init(&u);
    uint32_t rs = 12345; int touching = 0, hold = 0, tx = 0, ty = 0; long games = 0, repairs = 0;
    #define RND() (rs ^= rs << 13, rs ^= rs >> 17, rs ^= rs << 5, rs)
    for (long f = 0; f < 1500000; f++) {
        int touch = 0;
        if (!touching) { if (RND() % 40 == 0) { touching = 1; hold = 1 + RND() % 6; tx = RND() % 256; ty = (RND() % 3) ? 40 + RND() % 150 : RND() % 192; touch = 1; } }
        else if (--hold <= 0) { touching = 0; touch = 3; }
        else { touch = 2; if (RND() % 4 == 0) tx = (tx + (int)(RND() % 9) - 4) & 255; }
        int prev = u.scr;
        ui_update(&u, touch, tx, ty, RND() % 20000 == 0, (uint32_t)f * 2654435761u + 1);
        if (u.scr == UI_GAME && prev != UI_GAME) games++;
        ui_render(&u, TOP, BOT);
        if (u.scr == UI_GAME) {
            Game *g = &u.g; repairs += g->stat_repairs;
            if (g->handN[0] > G_MAX_HAND || g->tableN > G_MAX_TABLE) { CHECK(0, "overflow at frame %ld", f); return; }
        }
    }
    CHECK(repairs == 0, "safety net ran %ld times", repairs);
    printf("  %ld games started\n", games);
}

int main(void) {
    t_menus_and_icons();
    t_redraw_rules();
    t_full_games();
    t_fuzz();
    printf("\n%d checks, %d failed\n%s\n", g_checks, g_fail, g_fail ? "UI TESTS FAILED" : "UI TESTS OK");
    return g_fail != 0;
}
