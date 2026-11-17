// ui.h - screens, drawing and touch handling for Durak DS.
// Draws into two 256x192 RGB555 framebuffers (stride 256). No DS API calls here.
#ifndef UI_H
#define UI_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "font.h"
#include "game.h"

#define SCR_W 256
#define SCR_H 192

#define C555(r, g, b) ((uint16_t)((((r) >> 3) & 31) | ((((g) >> 3) & 31) << 5) | ((((b) >> 3) & 31) << 10) | 0x8000))
#define C_PAPER  C555(255, 255, 255)
#define C_INK    C555(10, 10, 10)
#define C_ACCENT C555(193, 18, 31)
#define C_TABLE  C555(245, 244, 240)
#define C_GREY   C555(112, 112, 112)
#define C_LINE   C555(214, 214, 214)
#define C_OFF    C555(185, 182, 174)

#define BOT_DELAY_FRAMES 84   /* ~1.4 s, same pace as the prototype */

/* ------------------------------------------------------------ primitives */

static inline void px_put(uint16_t *fb, int x, int y, uint16_t c) {
    if ((unsigned)x < SCR_W && (unsigned)y < SCR_H) fb[y * SCR_W + x] = c;
}

static void fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t c) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > SCR_W) x1 = SCR_W;
    if (y1 > SCR_H) y1 = SCR_H;
    for (int j = y; j < y1; j++) {
        uint16_t *row = fb + j * SCR_W;
        for (int i = x; i < x1; i++) row[i] = c;
    }
}

static void frame_rect(uint16_t *fb, int x, int y, int w, int h, int t, uint16_t c) {
    fill_rect(fb, x, y, w, t, c);
    fill_rect(fb, x, y + h - t, w, t, c);
    fill_rect(fb, x, y, t, h, c);
    fill_rect(fb, x + w - t, y, t, h, c);
}

/* ------------------------------------------------------------------ text */

static uint32_t utf8_next(const char **p) {
    const unsigned char *s = (const unsigned char *)*p;
    uint32_t c = s[0];
    if (c < 0x80) { *p += 1; return c; }
    if ((c & 0xE0) == 0xC0) { *p += 2; return ((c & 0x1F) << 6) | (s[1] & 0x3F); }
    if ((c & 0xF0) == 0xE0) { *p += 3; return ((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); }
    *p += 1; return '?';
}

static uint32_t cp_upper(uint32_t c) {                  // all UI text is set in capitals
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 0x20;
    if (c == 0x451) return 0x401;
    return c;
}

static int glyph_idx(const Font *f, uint32_t cp) {
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2; uint32_t v = f->cp[m];
        if (v == cp) return m;
        if (v < cp) lo = m + 1; else hi = m - 1;
    }
    return -1;
}

static int text_width(const Font *f, const char *s, int track) {
    int w = 0, n = 0;
    while (*s) {
        int gi = glyph_idx(f, cp_upper(utf8_next(&s)));
        if (gi < 0) gi = 0;
        w += f->adv[gi] + track; n++;
    }
    return n ? w - track : 0;
}

static int cap_height(const Font *f) { return (f->base * 73 + 50) / 100; }

// (x,y) = top-left of the whole cell box; rot=1 turns the text by 180 degrees
static void draw_text_ex(uint16_t *fb, const Font *f, int x, int y, const char *s, uint16_t col, int track, int rot) {
    int W = text_width(f, s, track), H = f->h, pen = 0;
    while (*s) {
        int gi = glyph_idx(f, cp_upper(utf8_next(&s)));
        if (gi < 0) gi = 0;
        int adv = f->adv[gi];
        const uint32_t *rows = f->bits + gi * H;
        for (int ry = 0; ry < H; ry++) {
            uint32_t row = rows[ry];
            if (!row) continue;
            for (int cx = 0; cx < adv; cx++) if ((row >> cx) & 1) {
                int px = pen + cx, py = ry;
                if (rot) { px = W - 1 - px; py = H - 1 - py; }
                px_put(fb, x + px, y + py, col);
            }
        }
        pen += adv + track;
    }
}

// (x,y) = corner of the capital-letter box: top-left, or bottom-right when rot=1
static void draw_text_cap(uint16_t *fb, const Font *f, int x, int y, const char *s, uint16_t col, int track, int rot) {
    int W = text_width(f, s, track), cap = cap_height(f);
    if (!rot) draw_text_ex(fb, f, x, y - (f->base - cap), s, col, track, 0);
    else      draw_text_ex(fb, f, x - W + 1, y - (f->h - 1 - (f->base - cap)), s, col, track, 1);
}

static void text_left(uint16_t *fb, const Font *f, int x, int capTop, const char *s, uint16_t col, int track) {
    draw_text_cap(fb, f, x, capTop, s, col, track, 0);
}
static void text_right(uint16_t *fb, const Font *f, int xr, int capTop, const char *s, uint16_t col, int track) {
    draw_text_cap(fb, f, xr - text_width(f, s, track) + 1, capTop, s, col, track, 0);
}
static void text_center(uint16_t *fb, const Font *f, int cx, int capMidY, const char *s, uint16_t col, int track) {
    draw_text_cap(fb, f, cx - text_width(f, s, track) / 2, capMidY - cap_height(f) / 2, s, col, track, 0);
}

// word-wrapped, centred; returns the number of lines drawn
static int text_wrap_center(uint16_t *fb, const Font *f, int cx, int capMidY, int maxw, const char *s, uint16_t col, int lineGap) {
    char line[96]; int ll = 0, lines = 0;
    char word[48];
    const char *p = s;
    line[0] = 0;
    while (1) {
        int wl = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && wl < 47) word[wl++] = *p++;
        word[wl] = 0;
        if (wl == 0) break;
        char trial[96];
        int tl = 0;
        if (ll) { memcpy(trial, line, (size_t)ll); trial[ll] = ' '; tl = ll + 1; }
        memcpy(trial + tl, word, (size_t)wl + 1);
        if (ll && text_width(f, trial, 0) > maxw) {
            text_center(fb, f, cx, capMidY + lines * lineGap, line, col, 0);
            lines++;
            memcpy(line, word, (size_t)wl + 1); ll = wl;
        } else {
            memcpy(line, trial, (size_t)(tl + wl) + 1); ll = tl + wl;
        }
    }
    if (ll) { text_center(fb, f, cx, capMidY + lines * lineGap, line, col, 0); lines++; }
    return lines;
}

/* ----------------------------------------------------------------- cards */

static const char *RANK_STR[9] = { "6", "7", "8", "9", "10", "J", "Q", "K", "A" };
static const char *SUIT_WORD[4] = { "пики", "черви", "бубны", "трефы" };
static const int SUIT_PX[4] = { 8, 11, 14, 18 };

static void draw_suit(uint16_t *fb, int sz, int suit, int cx, int cy, uint16_t col) {
    const uint32_t *rows;
    switch (sz) {
        case 0: rows = SUIT_8[suit]; break;
        case 1: rows = SUIT_11[suit]; break;
        case 2: rows = SUIT_14[suit]; break;
        default: rows = SUIT_18[suit]; break;
    }
    int n = SUIT_PX[sz], x0 = cx - n / 2, y0 = cy - n / 2;
    for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) if ((rows[y] >> x) & 1) px_put(fb, x0 + x, y0 + y, col);
}

static uint16_t suit_color(int suit) { return (suit == SUIT_HEARTS || suit == SUIT_DIAMONDS) ? C_ACCENT : C_INK; }

#define CF_BOTTOM 1   /* mirrored rank in the bottom-right corner */
#define CF_INDEX  2   /* small suit under the top-left rank (so a half-covered card still reads) */

static void draw_card(uint16_t *fb, int x, int y, int w, int h, DCard c, bool accentEdge, int flags) {
    int t = accentEdge ? 2 : 1;
    fill_rect(fb, x, y, w, h, accentEdge ? C_ACCENT : C_INK);
    fill_rect(fb, x + t, y + t, w - 2 * t, h - 2 * t, C_PAPER);
    uint16_t col = suit_color(c.suit);
    const Font *f = (w >= 50) ? &F12 : &F9;
    draw_text_cap(fb, f, x + 4, y + 4, RANK_STR[c.rank], col, 0, 0);
    if (flags & CF_INDEX) draw_suit(fb, 0, c.suit, x + 7, y + 4 + cap_height(f) + 8, col);
    if (flags & CF_BOTTOM) draw_text_cap(fb, f, x + w - 5, y + h - 5, RANK_STR[c.rank], col, 0, 1);
    draw_suit(fb, (h >= 76) ? 3 : (h >= 66) ? 2 : 1, c.suit, x + w / 2, y + h / 2, col);
}

/* -------------------------------------------------------------- UI state */

enum { UI_MODE, UI_BOTS, UI_GAME };

enum {
    ID_NONE = -1,
    ID_PASS = 100, ID_TAKE = 101, ID_SKIP = 102, ID_AGAIN = 103, ID_MENU = 104,
    ID_MODE0 = 200,                 // 200..202
    ID_BOTS1 = 211,                 // 211..213
    ID_CHEAT = 220, ID_BACK = 221
};

typedef struct {
    int scr, mode, bots; bool cheat;
    Game g;
    int hover, botTimer, skipKey;
    int hoverLift;                 // animated lift of the currently selected card, 0..HAND_LIFT
    bool dirty;
    int lastX, lastY;
} UI;

#define HAND_CW 50
#define HAND_CH 70
#define HAND_Y  52
#define HAND_LIFT 12

static void ui_init(UI *u) {
    memset(u, 0, sizeof *u);
    u->scr = UI_MODE; u->hover = ID_NONE; u->skipKey = -1; u->dirty = true;
}

static void ui_start_game(UI *u, uint32_t seed) {
    g_init(&u->g, u->mode, u->bots, u->cheat, seed);
    u->scr = UI_GAME; u->hover = ID_NONE; u->botTimer = 0; u->skipKey = -1; u->dirty = true;
}

static int ui_min_bots(const UI *u) { return u->mode == GM_PEREVODNOY ? 2 : 1; }

static int ui_window_key(const UI *u) {
    const Game *g = &u->g;
    return g->tableN * 100 + g->defender * 10 + g->handN[0];
}

// the human may add a card while somebody else is defending (podkidnoy)
static bool ui_throw_window(const UI *u) {
    const Game *g = &u->g;
    return u->scr == UI_GAME && g->over == GO_NONE && g->mode == GM_PODKIDNOY && g->phase == PH_DEFEND
        && g->defender != 0 && g_has_throwable(g, 0) && u->skipKey != ui_window_key(u);
}

static bool ui_can_transfer_any(const UI *u) {
    const Game *g = &u->g;
    for (int i = 0; i < g->handN[0]; i++) if (g_can_transfer_card(g, 0, g->hands[0][i])) return true;
    return false;
}

/* --------------------------------------------------------------- layout */

static void hand_geometry(int n, int i, int *x, int *step) {
    int st = 38;
    if (n > 1) { int fit = (SCR_W - 12 - HAND_CW) / (n - 1); if (fit < st) st = fit; }
    int total = HAND_CW + st * (n > 0 ? n - 1 : 0);
    *step = st;
    *x = (SCR_W - total) / 2 + st * i;
}

static void btn_rect(const UI *u, int id, int *x, int *y, int *w, int *h) {
    *y = 156; *h = 28;
    switch (id) {
        case ID_PASS:  *x = 8;   *w = 60;  break;
        case ID_TAKE:  *x = 74;  *w = 60;  break;
        case ID_SKIP:  *x = 140; *w = 108; break;
        case ID_AGAIN: *x = 8;   *w = 100; break;
        default:       *x = 114; *w = 134; break;   // ID_MENU
    }
    (void)u;
}

static bool in_rect(int px, int py, int x, int y, int w, int h) { return px >= x && px < x + w && py >= y && py < y + h; }

static int ui_hit(const UI *u, int px, int py) {
    if (u->scr == UI_MODE) {
        for (int i = 0; i < 3; i++) {
            int x0 = i * 85, w = (i == 1) ? 86 : 85;
            if (in_rect(px, py, x0, 18, w, 170)) return ID_MODE0 + i;
        }
    } else if (u->scr == UI_BOTS) {
        int mn = ui_min_bots(u), k = 4 - mn;
        for (int i = 0; i < k; i++) if (in_rect(px, py, i * SCR_W / k, 18, SCR_W / k, 128)) return ID_BOTS1 + (mn + i - 1);
        if (in_rect(px, py, 8, 148, 240, 20)) return ID_CHEAT;
        if (in_rect(px, py, 96, 172, 64, 16)) return ID_BACK;
    } else {
        const Game *g = &u->g;
        int n = g->handN[0];
        if (u->hover >= 0 && u->hover < n) {
            int hx, hst; hand_geometry(n, u->hover, &hx, &hst);
            if (in_rect(px, py, hx, HAND_Y - HAND_LIFT, HAND_CW, HAND_CH + HAND_LIFT)) return u->hover;
        }
        for (int i = n - 1; i >= 0; i--) {
            int x, st; hand_geometry(n, i, &x, &st);
            if (in_rect(px, py, x, HAND_Y, (i == n - 1) ? HAND_CW : st, HAND_CH)) return i;
        }
        int bx, by, bw, bh;
        if (g->over != GO_NONE) {
            btn_rect(u, ID_AGAIN, &bx, &by, &bw, &bh); if (in_rect(px, py, bx, by, bw, bh)) return ID_AGAIN;
            btn_rect(u, ID_MENU, &bx, &by, &bw, &bh);  if (in_rect(px, py, bx, by, bw, bh)) return ID_MENU;
        } else {
            btn_rect(u, ID_PASS, &bx, &by, &bw, &bh); if (in_rect(px, py, bx, by, bw, bh)) return ID_PASS;
            btn_rect(u, ID_TAKE, &bx, &by, &bw, &bh); if (in_rect(px, py, bx, by, bw, bh)) return ID_TAKE;
            if (ui_throw_window(u)) {
                btn_rect(u, ID_SKIP, &bx, &by, &bw, &bh); if (in_rect(px, py, bx, by, bw, bh)) return ID_SKIP;
            }
        }
    }
    return ID_NONE;
}

/* --------------------------------------------------------------- actions */

static void ui_play_card(UI *u, int idx) {
    Game *g = &u->g;
    if (g->over != GO_NONE || idx < 0 || idx >= g->handN[0]) return;
    DCard c = g->hands[0][idx];
    if (g->active == 0 && g->attacker == 0 && g->phase == PH_ATTACK) g_attack(g, 0, idx);
    else if (g->active == 0 && g->defender == 0 && g->phase == PH_DEFEND) {
        if (g_can_transfer_card(g, 0, c)) g_transfer(g, 0, idx);
        else g_defend(g, 0, idx);
    }
    else if (ui_throw_window(u)) g_throw_in(g, 0, idx);
    else g->msg = "Сейчас не твой ход";
}

static void ui_activate(UI *u, int id, uint32_t seed) {
    u->dirty = true;
    if (id >= 0 && id < 100) { ui_play_card(u, id); u->botTimer = 0; return; }
    switch (id) {
        case ID_PASS: g_pass(&u->g, 0); u->botTimer = 0; break;
        case ID_TAKE: g_take(&u->g, 0); u->botTimer = 0; break;
        case ID_SKIP: u->skipKey = ui_window_key(u); break;
        case ID_AGAIN: ui_start_game(u, seed); break;
        case ID_MENU: u->scr = UI_MODE; break;
        case ID_MODE0: case ID_MODE0 + 1: case ID_MODE0 + 2:
            u->mode = id - ID_MODE0;
            u->scr = UI_BOTS; u->bots = 0; break;
        case ID_BOTS1: case ID_BOTS1 + 1: case ID_BOTS1 + 2:
            u->bots = id - ID_BOTS1 + 1; ui_start_game(u, seed); break;
        case ID_CHEAT: u->cheat = !u->cheat; break;
        case ID_BACK: u->scr = UI_MODE; break;
        default: break;
    }
}

// touch: 0 = none, 1 = pressed, 2 = held, 3 = released. Call once per frame.
static void ui_update(UI *u, int touch, int x, int y, bool startPressed, uint32_t seed) {
    if (touch == 1 || touch == 2) {
        u->lastX = x; u->lastY = y;
        int h = ui_hit(u, x, y);
        if (h != u->hover) { u->hover = h; u->dirty = true; }
    } else if (touch == 3) {
        int h = ui_hit(u, u->lastX, u->lastY);
        int was = u->hover;
        u->hover = ID_NONE; u->dirty = true;
        if (h != ID_NONE && h == was) ui_activate(u, h, seed);
    }
    if (startPressed && u->scr == UI_GAME) { u->scr = UI_MODE; u->hover = ID_NONE; u->dirty = true; }

    // Animate the selected card independently of game logic. Two pixels per
    // frame gives a short, smooth ~100 ms lift/drop at 60 Hz.
    {
        int targetLift = (u->scr == UI_GAME && u->hover >= 0 && u->hover < u->g.handN[0]) ? HAND_LIFT : 0;
        if (u->hoverLift < targetLift) {
            u->hoverLift += 2;
            if (u->hoverLift > targetLift) u->hoverLift = targetLift;
            u->dirty = true;
        } else if (u->hoverLift > targetLift) {
            u->hoverLift -= 2;
            if (u->hoverLift < targetLift) u->hoverLift = targetLift;
            u->dirty = true;
        }
    }

    if (u->scr == UI_GAME) {
        Game *g = &u->g;
        bool botActs = g->over == GO_NONE && (g->active != 0 || g->autoHuman) && !ui_throw_window(u);
        if (botActs) {
            if (++u->botTimer >= BOT_DELAY_FRAMES) { g_bot_step(g); u->botTimer = 0; u->dirty = true; }
        } else u->botTimer = 0;
    }
}

/* ------------------------------------------------------------- rendering */

static const char *mode_name(int m) { return m == GM_PODKIDNOY ? "подкидной" : m == GM_PEREVODNOY ? "переводной" : "обычный"; }

static void draw_choice(uint16_t *fb, int x, int y, int w, int h, bool hov, bool accent, int icon,
                        const char *label, const Font *lf, const char *desc) {
    uint16_t bg = hov ? (accent ? C_ACCENT : C_INK) : C_PAPER;
    uint16_t fg = hov ? C_PAPER : C_INK;
    fill_rect(fb, x, y, w, h, bg);
    int cx = x + w / 2, cy = y + h / 2 - 8 - (hov ? 4 : 0);
    frame_rect(fb, cx - 19, cy - 26, 38, 52, 2, fg);
    if (icon == 0) {
        for (int j = -6; j <= 6; j++) for (int i = -6; i <= 6; i++) if (i * i + j * j <= 36) px_put(fb, cx + i, cy + j, fg);
    } else {
        for (int j = -7; j <= 7; j++) for (int i = -7; i <= 7; i++) if (i < 0 ? -i + (j < 0 ? -j : j) <= 7 : i + (j < 0 ? -j : j) <= 7) px_put(fb, cx + i, cy + j, fg);
    }
    text_center(fb, lf, cx, y + h / 2 + 36, label, fg, 0);
    if (desc) text_wrap_center(fb, &F9, cx, y + h / 2 + 52, w - 10, desc, hov ? C_PAPER : C_GREY, 12);
}

static void render_mode_top(uint16_t *fb) {
    fill_rect(fb, 0, 0, SCR_W, SCR_H, C_PAPER);
    text_left(fb, &F9, 6, 6, "ДУРАК DS", C_INK, 1);
    text_center(fb, &F28, 128, 78, "ДУРАК", C_INK, 0);
    fill_rect(fb, 104, 104, 48, 3, C_ACCENT);
    text_center(fb, &F9, 128, 126, "ВЫБЕРИ РЕЖИМ НА НИЖНЕМ ЭКРАНЕ", C_GREY, 1);
    frame_rect(fb, 0, 0, SCR_W, SCR_H, 1, C_INK);
}

static void render_mode_bottom(const UI *u, uint16_t *fb) {
    fill_rect(fb, 0, 0, SCR_W, SCR_H, C_PAPER);
    text_left(fb, &F9, 6, 6, "РЕЖИМ ИГРЫ", C_INK, 1);
    draw_choice(fb, 0,   18, 85, 170, u->hover == ID_MODE0,     false, 0, "ОБЫЧНЫЙ",    &F9, "Без подкидки");
    draw_choice(fb, 85,  18, 86, 170, u->hover == ID_MODE0 + 1, true,  1, "ПОДКИДНОЙ",  &F9, "Можно подкидывать карты");
    draw_choice(fb, 171, 18, 85, 170, u->hover == ID_MODE0 + 2, false, 0, "ПЕРЕВОДНОЙ", &F9, "Перевод хода (нужно ≥2 бота)");
    fill_rect(fb, 85, 18, 1, 170, C_LINE);
    fill_rect(fb, 171, 18, 1, 170, C_LINE);
    frame_rect(fb, 0, 0, SCR_W, SCR_H, 1, C_INK);
}

static void render_bots_top(const UI *u, uint16_t *fb) {
    fill_rect(fb, 0, 0, SCR_W, SCR_H, C_PAPER);
    text_left(fb, &F9, 6, 6, mode_name(u->mode), C_INK, 1);
    text_center(fb, &F20, 128, 84, "СКОЛЬКО БОТОВ?", C_INK, 0);
    fill_rect(fb, 104, 108, 48, 3, C_ACCENT);
    if (u->mode == GM_PEREVODNOY)
        text_wrap_center(fb, &F9, 128, 128, 200, "ПЕРЕВОДУ НУЖНО МИНИМУМ 2 ПРОТИВНИКА", C_GREY, 12);
    frame_rect(fb, 0, 0, SCR_W, SCR_H, 1, C_INK);
}

static void render_bots_bottom(const UI *u, uint16_t *fb) {
    fill_rect(fb, 0, 0, SCR_W, SCR_H, C_PAPER);
    text_left(fb, &F9, 6, 6, "КОЛИЧЕСТВО БОТОВ", C_INK, 1);
    int mn = ui_min_bots(u), k = 4 - mn;
    for (int i = 0; i < k; i++) {
        int n = mn + i, x0 = i * SCR_W / k, w = SCR_W / k;
        char lab[4], desc[24];
        lab[0] = (char)('0' + n); lab[1] = 0;
        strcpy(desc, n == 1 ? "1 противник" : "2 противника");
        if (n == 3) strcpy(desc, "3 противника");
        draw_choice(fb, x0, 18, w, 128, u->hover == ID_BOTS1 + n - 1, n == 3, n == 3 ? 1 : 0, lab, &F12, desc);
        if (i > 0) fill_rect(fb, x0, 18, 1, 128, C_LINE);
    }
    bool hov = u->hover == ID_CHEAT;
    uint16_t bg = u->cheat ? C_ACCENT : C_PAPER, fg = u->cheat ? C_PAPER : C_INK;
    if (hov) { bg = u->cheat ? C_INK : C_ACCENT; fg = C_PAPER; }
    fill_rect(fb, 8, 148, 240, 20, bg);
    frame_rect(fb, 8, 148, 240, 20, 1, C_INK);
    text_left(fb, &F9, 14, 154, "ЖУЛЬНИЧЕСТВО", fg, 1);
    text_right(fb, &F9, 242, 154, u->cheat ? "ВКЛ · ЛЮБАЯ КАРТА" : "ВЫКЛ", fg, 0);
    bool bh = u->hover == ID_BACK;
    fill_rect(fb, 96, 172, 64, 16, bh ? C_ACCENT : C_INK);
    text_center(fb, &F9, 128, 180, "НАЗАД", C_PAPER, 1);
    frame_rect(fb, 0, 0, SCR_W, SCR_H, 1, C_INK);
}

static void table_slot(int n, int i, int *x, int *y, int *w, int *h, int *dx, int *dy) {
    int big = n <= 2;
    *w = big ? 56 : 42; *h = big ? 78 : 60; *dx = big ? 20 : 16; *dy = big ? 8 : 7;
    int cols = n <= 2 ? 2 : 3;
    int rows = (n + cols - 1) / cols;
    int area_x = 6, area_w = 208, area_y = 30, area_h = 138;
    int col = i % cols, row = i / cols;
    int inRow = (n - row * cols) < cols ? (n - row * cols) : cols;
    int cell_w = area_w / cols;
    int pair_w = *w + *dx, pair_h = *h + *dy;
    int rowpitch = rows > 1 ? pair_h + 4 : 0;
    int total_h = rows > 1 ? rowpitch * (rows - 1) + pair_h : pair_h;
    *x = area_x + (area_w - inRow * cell_w) / 2 + col * cell_w + (cell_w - pair_w) / 2;
    *y = area_y + (area_h - total_h) / 2 + row * rowpitch;
}

static const char *ui_status(const UI *u, char *buf) {
    const Game *g = &u->g;
    (void)buf;
    if (g->over == GO_DRAW) return "НИЧЬЯ";
    if (g->over == 0) return "ТЫ ПРОИГРАЛ";
    if (g->over > 0) return "ТЫ ВЫИГРАЛ";
    if (g->active == 0 && g->defender == 0 && g->phase == PH_DEFEND && g->mode == GM_PEREVODNOY && ui_can_transfer_any(u))
        return "Бей или переведи карту того же ранга";
    if (ui_throw_window(u)) return "Можешь подкинуть карту";
    return g->msg;
}

static void render_game_top(const UI *u, uint16_t *fb) {
    const Game *g = &u->g;
    fill_rect(fb, 0, 0, SCR_W, SCR_H, C_TABLE);
    char tmp[64];

    strcpy(tmp, "КОЗЫРЬ — "); strcat(tmp, SUIT_WORD[g->trump]);
    text_left(fb, &F9, 6, 6, tmp, C_INK, 1);
    strcpy(tmp, "КОЛОДА: ");
    { int d = g->deckN; char num[4]; int l = 0; if (d >= 10) num[l++] = (char)('0' + d / 10); num[l++] = (char)('0' + d % 10); num[l] = 0; strcat(tmp, num); }
    text_right(fb, &F9, 250, 6, tmp, C_INK, 1);

    strcpy(tmp, mode_name(g->mode));
    strcat(tmp, " · ");
    { char nb[2] = { (char)('0' + (g->nPlayers - 1)), 0 }; strcat(tmp, nb); strcat(tmp, g->nPlayers - 1 == 1 ? " бот" : " бота"); }
    if (g->cheating) strcat(tmp, " · жульничество");
    text_left(fb, &F9, 6, 19, tmp, C_GREY, 0);
    { char bb[48]; bb[0] = 0;
      for (int p = 1; p < g->nPlayers; p++) {
          char one[16]; int l = 0;
          one[l++] = (char)0xD0; one[l++] = (char)0x91;            // UTF-8 for Б
          one[l++] = (char)('0' + p); one[l++] = ':';
          if (g->handN[p] >= 10) one[l++] = (char)('0' + g->handN[p] / 10);
          one[l++] = (char)('0' + g->handN[p] % 10); one[l] = 0;
          if (p > 1) strcat(bb, "  ");
          strcat(bb, one);
      }
      text_right(fb, &F9, 250, 19, bb, C_INK, 0); }

    for (int i = 0; i < g->tableN; i++) {
        int x, y, w, h, dx, dy;
        table_slot(g->tableN, i, &x, &y, &w, &h, &dx, &dy);
        draw_card(fb, x, y, w, h, g->table[i].attack, false, CF_BOTTOM);
        if (g->table[i].hasDefend) draw_card(fb, x + dx, y + dy, w, h, g->table[i].defend, false, CF_BOTTOM);
    }

    // trump badge in the corner (rank hidden once the deck is gone)
    { int bx = 222, by = 126, bw = 26, bh = 34;
      fill_rect(fb, bx, by, bw, bh, C_INK);
      fill_rect(fb, bx + 1, by + 1, bw - 2, bh - 2, C_PAPER);
      uint16_t col = suit_color(g->trump);
      if (g->deckN > 0) text_left(fb, &F9, bx + 4, by + 4, RANK_STR[g->trumpCard.rank], col, 0);
      draw_suit(fb, 0, g->trump, bx + bw / 2, by + bh / 2 + 3, col); }

    fill_rect(fb, 0, 170, SCR_W, 1, C_LINE);
    text_center(fb, &F9, 128, 181, ui_status(u, tmp), g->over != GO_NONE ? C_ACCENT : C_INK, 0);
    frame_rect(fb, 0, 0, SCR_W, SCR_H, 1, C_INK);
}

static void draw_button(uint16_t *fb, int x, int y, int w, int h, const char *label, bool enabled, bool accent, bool hov) {
    uint16_t bg = enabled ? (accent ? C_ACCENT : C_INK) : C_OFF;
    if (enabled && hov) bg = accent ? C_INK : C_ACCENT;
    fill_rect(fb, x, y, w, h, bg);
    text_center(fb, &F9, x + w / 2, y + h / 2, label, C_PAPER, 1);
}

static void render_game_bottom(const UI *u, uint16_t *fb) {
    const Game *g = &u->g;
    fill_rect(fb, 0, 0, SCR_W, SCR_H, C_PAPER);
    text_left(fb, &F9, 6, 6, "ТВОЯ РУКА", C_INK, 1);
    text_right(fb, &F9, 250, 6, "START — МЕНЮ", C_GREY, 0);

    int n = g->handN[0];
    for (int i = 0; i < n; i++) {                       // fan, left to right; the touched card is drawn last
        if (i == u->hover) continue;
        int x, st; hand_geometry(n, i, &x, &st);
        draw_card(fb, x, HAND_Y, HAND_CW, HAND_CH, g->hands[0][i], false, CF_INDEX | (i == n - 1 ? CF_BOTTOM : 0));
    }
    if (u->hover >= 0 && u->hover < n) {
        int x, st; hand_geometry(n, u->hover, &x, &st);
        draw_card(fb, x, HAND_Y - u->hoverLift, HAND_CW, HAND_CH, g->hands[0][u->hover], true, CF_INDEX | CF_BOTTOM);
    }

    if (ui_throw_window(u)) {
        frame_rect(fb, 8, 132, 240, 16, 1, C_ACCENT);
        text_center(fb, &F9, 128, 140, g->cheating ? "ЖУЛЬНИЧЕСТВО: ПОДКИНЬ ЛЮБУЮ КАРТУ" : "МОЖНО ПОДКИНУТЬ КАРТУ ТОГО ЖЕ РАНГА", C_ACCENT, 0);
    }

    int bx, by, bw, bh;
    if (g->over != GO_NONE) {
        btn_rect(u, ID_AGAIN, &bx, &by, &bw, &bh); draw_button(fb, bx, by, bw, bh, "ЗАНОВО", true, false, u->hover == ID_AGAIN);
        btn_rect(u, ID_MENU, &bx, &by, &bw, &bh);  draw_button(fb, bx, by, bw, bh, "СМЕНИТЬ РЕЖИМ", true, false, u->hover == ID_MENU);
    } else {
        btn_rect(u, ID_PASS, &bx, &by, &bw, &bh); draw_button(fb, bx, by, bw, bh, "БИТА", g_can_pass(g, 0), false, u->hover == ID_PASS);
        btn_rect(u, ID_TAKE, &bx, &by, &bw, &bh); draw_button(fb, bx, by, bw, bh, "ВЗЯТЬ", g_can_take(g, 0), true, u->hover == ID_TAKE);
        if (ui_throw_window(u)) {
            btn_rect(u, ID_SKIP, &bx, &by, &bw, &bh); draw_button(fb, bx, by, bw, bh, "НЕ ПОДКИДЫВАЮ", true, false, u->hover == ID_SKIP);
        }
    }
    frame_rect(fb, 0, 0, SCR_W, SCR_H, 1, C_INK);
}

// top = upper screen, bot = lower (touch) screen
static void ui_render(UI *u, uint16_t *top, uint16_t *bot) {
    if (!u->dirty) return;
    if (u->scr == UI_MODE)      { render_mode_top(top);      render_mode_bottom(u, bot); }
    else if (u->scr == UI_BOTS) { render_bots_top(u, top);   render_bots_bottom(u, bot); }
    else                        { render_game_top(u, top);   render_game_bottom(u, bot); }
    u->dirty = false;
}

#endif
