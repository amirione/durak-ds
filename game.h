// game.h - Durak rules for the DS port. Pure C, no DS dependencies.
// Ported from DurakSwiss3Dv15.jsx. Player 0 is the human, 1..3 are bots.
#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

enum { SUIT_SPADES, SUIT_HEARTS, SUIT_DIAMONDS, SUIT_CLUBS };
enum { GM_CLASSIC, GM_PODKIDNOY, GM_PEREVODNOY };
enum { PH_ATTACK, PH_DEFEND };

#define G_MAX_PLAYERS 4
#define G_MAX_HAND    36
#define G_MAX_TABLE   6
#define GO_NONE (-2)
#define GO_DRAW (-1)

typedef struct { uint8_t rank, suit; } DCard;   // rank 0..8 = 6 7 8 9 10 J Q K A
typedef struct { DCard attack, defend; bool hasDefend; uint8_t by; } Pair;

typedef struct {
    DCard deck[36]; int deckN;                  // deck[0] is drawn next; the trump card is the last one
    DCard hands[G_MAX_PLAYERS][G_MAX_HAND]; int handN[G_MAX_PLAYERS];
    Pair table[G_MAX_TABLE]; int tableN;
    int nPlayers, mode; bool cheating;
    int trump; DCard trumpCard;
    int attacker, defender, active, phase;
    int over;                                   // GO_NONE, GO_DRAW, or index of the loser (the durak)
    const char *msg;
    bool autoHuman;                             // testing: let player 0 be played by the bot logic
    uint32_t rng;
} Game;

/* ------------------------------------------------------------- helpers */

static uint32_t g_rand(Game *g) {
    uint32_t x = g->rng ? g->rng : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g->rng = x;
    return x;
}

static bool g_beats(DCard a, DCard d, int trump) {
    if (d.suit == a.suit) return d.rank > a.rank;
    return d.suit == trump && a.suit != trump;
}

static int g_open_mask(const Game *g) {
    int m = 0;
    for (int i = 0; i < g->tableN; i++) {
        m |= 1 << g->table[i].attack.rank;
        if (g->table[i].hasDefend) m |= 1 << g->table[i].defend.rank;
    }
    return m;
}

static int g_undefended(const Game *g) {
    int n = 0;
    for (int i = 0; i < g->tableN; i++) if (!g->table[i].hasDefend) n++;
    return n;
}

static int g_open_pair(const Game *g) {            // index of the first unbeaten pair, or -1
    for (int i = 0; i < g->tableN; i++) if (!g->table[i].hasDefend) return i;
    return -1;
}

static int g_sort_key(const Game *g, DCard c) {
    return (c.suit == g->trump ? 4 : c.suit) * 9 + c.rank;
}

static void g_sort_hand(Game *g, int p) {
    DCard *h = g->hands[p];
    for (int i = 1; i < g->handN[p]; i++) {
        DCard c = h[i]; int j = i - 1;
        while (j >= 0 && g_sort_key(g, h[j]) > g_sort_key(g, c)) { h[j + 1] = h[j]; j--; }
        h[j + 1] = c;
    }
}

static void g_remove_card(Game *g, int p, int idx) {
    for (int k = idx; k < g->handN[p] - 1; k++) g->hands[p][k] = g->hands[p][k + 1];
    g->handN[p]--;
}

static DCard g_draw_one(Game *g) {
    DCard c = g->deck[0];
    for (int i = 0; i < g->deckN - 1; i++) g->deck[i] = g->deck[i + 1];
    g->deckN--;
    return c;
}

// next player strictly after `from` who still has cards (or can still draw)
static int g_next_active(const Game *g, int from) {
    int n = g->nPlayers;
    for (int step = 1; step <= n; step++) {
        int i = (from + step) % n;
        if (g->handN[i] > 0 || g->deckN > 0) return i;
    }
    return from;
}

static int g_first_active(const Game *g, int from) {
    if (g->handN[from] > 0 || g->deckN > 0) return from;
    return g_next_active(g, from);
}

/* ---------------------------------------------------------------- setup */

static void g_init(Game *g, int mode, int bots, bool cheating, uint32_t seed) {
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 1;
    g->mode = mode; g->cheating = cheating;
    g->nPlayers = 1 + bots;
    g->deckN = 36;
    for (int s = 0; s < 4; s++)
        for (int r = 0; r < 9; r++) { g->deck[s * 9 + r].suit = (uint8_t)s; g->deck[s * 9 + r].rank = (uint8_t)r; }
    for (int i = 35; i > 0; i--) {                  // Fisher-Yates
        int j = (int)(g_rand(g) % (uint32_t)(i + 1));
        DCard t = g->deck[i]; g->deck[i] = g->deck[j]; g->deck[j] = t;
    }
    for (int p = 0; p < g->nPlayers; p++)
        for (int k = 0; k < 6; k++) g->hands[p][g->handN[p]++] = g_draw_one(g);
    g->trumpCard = g->deck[g->deckN - 1];
    g->trump = g->trumpCard.suit;
    for (int p = 0; p < g->nPlayers; p++) g_sort_hand(g, p);
    g->attacker = 0; g->defender = 1; g->active = 0; g->phase = PH_ATTACK;
    g->over = GO_NONE;
    g->msg = "Твой ход — выбери карту для атаки";
}

// refill to 6 cards: attacker first, then the others in order, defender last
static void g_refill(Game *g, int att, int def) {
    int order[G_MAX_PLAYERS], n = 0;
    order[n++] = att;
    for (int i = (def + 1) % g->nPlayers; i != att; i = (i + 1) % g->nPlayers) if (i != def) order[n++] = i;
    if (def != att) order[n++] = def;
    for (int k = 0; k < n; k++) {
        int p = order[k];
        while (g->handN[p] < 6 && g->deckN > 0) g->hands[p][g->handN[p]++] = g_draw_one(g);
    }
    for (int p = 0; p < g->nPlayers; p++) g_sort_hand(g, p);
}

// once the deck is empty: nobody left with cards = draw, exactly one left = that player is the durak
static void g_check_over(Game *g) {
    if (g->deckN > 0 || g->tableN > 0) return;
    int with = 0, last = -1;
    for (int p = 0; p < g->nPlayers; p++) if (g->handN[p] > 0) { with++; last = p; }
    if (with == 0) g->over = GO_DRAW;
    else if (with == 1) g->over = last;
}

/* ------------------------------------------------------- legality checks */

static bool g_defender_can_take_more(const Game *g) {   // attacks may not outnumber the defender's cards
    return g->tableN < G_MAX_TABLE && g_undefended(g) < g->handN[g->defender];
}

static bool g_can_attack_card(const Game *g, DCard c) {
    if (!g_defender_can_take_more(g)) return false;
    return g->tableN == 0 || (g_open_mask(g) & (1 << c.rank));
}

static bool g_can_throw_card(const Game *g, int p, DCard c) {
    if (g->mode != GM_PODKIDNOY || g->phase != PH_DEFEND || p == g->defender) return false;
    if (g->tableN == 0 || !g_defender_can_take_more(g)) return false;
    return g->cheating || (g_open_mask(g) & (1 << c.rank));
}

static bool g_has_throwable(const Game *g, int p) {
    for (int i = 0; i < g->handN[p]; i++) if (g_can_throw_card(g, p, g->hands[p][i])) return true;
    return false;
}

static bool g_can_pass(const Game *g, int p) {
    return g->over == GO_NONE && p == g->attacker && g->active == p && g->phase == PH_ATTACK
        && g->tableN > 0 && g_undefended(g) == 0;
}

static bool g_can_take(const Game *g, int p) {
    return g->over == GO_NONE && p == g->defender && g->active == p && g->phase == PH_DEFEND
        && g_open_pair(g) >= 0;
}

static bool g_can_transfer_card(const Game *g, int p, DCard c) {
    if (g->mode != GM_PEREVODNOY || g->over != GO_NONE) return false;
    if (g->phase != PH_DEFEND || p != g->defender || g->active != p) return false;
    int o = g_open_pair(g);
    if (o < 0) return false;
    for (int i = 0; i < g->tableN; i++) if (g->table[i].hasDefend) return false;
    if (c.rank != g->table[o].attack.rank) return false;
    int nd = g_next_active(g, p);
    if (nd == p || nd == g->attacker) return false;
    return g->tableN < G_MAX_TABLE && g->handN[nd] >= g->tableN + 1;
}

/* --------------------------------------------------------------- actions */

static void g_add_attack(Game *g, int p, int ci) {
    DCard c = g->hands[p][ci];
    g_remove_card(g, p, ci);
    Pair *pr = &g->table[g->tableN++];
    pr->attack = c; pr->hasDefend = false; pr->by = (uint8_t)p;
}

static bool g_attack(Game *g, int p, int ci) {
    if (g->over != GO_NONE || g->phase != PH_ATTACK || g->active != p || p != g->attacker) return false;
    if (ci < 0 || ci >= g->handN[p]) return false;
    if (!g_can_attack_card(g, g->hands[p][ci])) {
        g->msg = g->tableN >= G_MAX_TABLE || !g_defender_can_take_more(g) ? "Больше карт подкинуть нельзя"
                                                                           : "Такого ранга нет на столе";
        return false;
    }
    g_add_attack(g, p, ci);
    g->phase = PH_DEFEND; g->active = g->defender;
    g->msg = p == 0 ? "Ждём защиту…" : "Отбивайся!";
    return true;
}

static bool g_throw_in(Game *g, int p, int ci) {
    if (g->over != GO_NONE || ci < 0 || ci >= g->handN[p]) return false;
    if (!g_can_throw_card(g, p, g->hands[p][ci])) { g->msg = "Эту карту подкинуть нельзя"; return false; }
    g_add_attack(g, p, ci);
    g->msg = p == 0 ? "Подкинул. Ждём защиту…" : "Подкинули карту.";
    return true;
}

static bool g_defend(Game *g, int p, int ci) {
    if (g->over != GO_NONE || g->phase != PH_DEFEND || g->active != p || p != g->defender) return false;
    if (ci < 0 || ci >= g->handN[p]) return false;
    int o = g_open_pair(g);
    if (o < 0) return false;
    DCard c = g->hands[p][ci];
    if (!g_beats(g->table[o].attack, c, g->trump)) { g->msg = "Эта карта не бьёт"; return false; }
    g_remove_card(g, p, ci);
    g->table[o].defend = c; g->table[o].hasDefend = true;
    if (g_open_pair(g) >= 0) {
        g->msg = p == 0 ? "Ещё есть незакрытые карты." : "Бот отбивается дальше…";
    } else {
        g->phase = PH_ATTACK; g->active = g->attacker;
        g->msg = p == 0 ? "Отбился! Можно подкинуть ещё." : "Бот отбился.";
    }
    return true;
}

static bool g_transfer(Game *g, int p, int ci) {
    if (ci < 0 || ci >= g->handN[p] || !g_can_transfer_card(g, p, g->hands[p][ci])) return false;
    int nd = g_next_active(g, p);
    g_add_attack(g, p, ci);
    g->defender = nd; g->active = nd;
    g->msg = nd == 0 ? "Перевод тебе — отбивайся!" : (p == 0 ? "Перевёл дальше." : "Бот перевёл.");
    return true;
}

static bool g_take(Game *g, int p) {
    if (!g_can_take(g, p)) return false;
    for (int i = 0; i < g->tableN; i++) {
        g->hands[p][g->handN[p]++] = g->table[i].attack;
        if (g->table[i].hasDefend) g->hands[p][g->handN[p]++] = g->table[i].defend;
    }
    int att = g->attacker, def = g->defender;
    g->tableN = 0;
    g_refill(g, att, def);
    g->phase = PH_ATTACK;
    g->msg = p == 0 ? "Ты забрал карты. Ход переходит дальше." : "Бот забрал карты.";
    g_check_over(g);
    if (g->over == GO_NONE) {
        g->attacker = g_next_active(g, def);
        g->defender = g_next_active(g, g->attacker);
        g->active = g->attacker;
    }
    return true;
}

static bool g_pass(Game *g, int p) {
    if (!g_can_pass(g, p)) return false;
    int att = g->attacker, def = g->defender;
    g->tableN = 0;
    g_refill(g, att, def);
    g->phase = PH_ATTACK;
    g->msg = "Бита.";
    g_check_over(g);
    if (g->over == GO_NONE) {
        g->attacker = g_first_active(g, def);
        g->defender = g_next_active(g, g->attacker);
        g->active = g->attacker;
    }
    return true;
}

/* ------------------------------------------------------------------ bots */

static int g_bot_key(const Game *g, DCard c) { return c.rank + (c.suit == g->trump ? 20 : 0); }

// cheapest card that satisfies `ok` (non-trumps first), or -1
static int g_bot_pick(const Game *g, int p, int mode /*0 attack,1 defend,2 transfer*/, int openIdx) {
    int best = -1, bestKey = 1000;
    for (int i = 0; i < g->handN[p]; i++) {
        DCard c = g->hands[p][i];
        bool ok;
        if (mode == 0) ok = g->tableN == 0 || (g_open_mask(g) & (1 << c.rank));
        else if (mode == 1) ok = g_beats(g->table[openIdx].attack, c, g->trump);
        else ok = c.rank == g->table[openIdx].attack.rank;
        if (!ok) continue;
        int k = g_bot_key(g, c);
        if (k < bestKey) { bestKey = k; best = i; }
    }
    return best;
}

static bool g_bot_throw_in(Game *g) {              // podkidnoy: a bystander bot adds a card before the bout ends
    if (g->mode != GM_PODKIDNOY || !g_defender_can_take_more(g)) return false;
    for (int k = 1; k < g->nPlayers; k++) {
        int p = (g->attacker + k) % g->nPlayers;
        if (p == g->defender || p == g->attacker || g->handN[p] == 0) continue;
        if (p == 0 && !g->autoHuman) continue;
        int ci = g_bot_pick(g, p, 0, -1);
        if (ci < 0) continue;
        if (g->hands[p][ci].suit == g->trump && g->deckN > 0) continue;
        g_add_attack(g, p, ci);
        g->phase = PH_DEFEND; g->active = g->defender;
        g->msg = "Бот подкинул.";
        return true;
    }
    return false;
}

// one bot decision for whoever is `active`
static void g_bot_step(Game *g) {
    if (g->over != GO_NONE) return;
    int p = g->active;
    if (p == 0 && !g->autoHuman) return;

    if (g->phase == PH_DEFEND && p == g->defender) {
        int o = g_open_pair(g);
        if (o < 0) return;
        int beat = g_bot_pick(g, p, 1, o);
        if (g->mode == GM_PEREVODNOY) {
            int tr = g_bot_pick(g, p, 2, o);
            if (tr >= 0 && g_can_transfer_card(g, p, g->hands[p][tr]) && (beat < 0 || (g_rand(g) & 1))) {
                g_transfer(g, p, tr);
                return;
            }
        }
        if (beat >= 0) g_defend(g, p, beat);
        else g_take(g, p);
        return;
    }

    if (g->phase == PH_ATTACK && p == g->attacker) {
        int ci = g_bot_pick(g, p, 0, -1);
        if (ci >= 0 && g->tableN > 0 && g->hands[p][ci].suit == g->trump && g->deckN > 0) ci = -1;
        if (ci >= 0 && !g_defender_can_take_more(g)) ci = -1;
        if (ci >= 0) { g_attack(g, p, ci); return; }
        if (g->tableN > 0) {
            if (g_bot_throw_in(g)) return;
            g_pass(g, p);
            return;
        }
        // attacker without any playable card and an empty table: hand the turn on
        g->attacker = g_next_active(g, g->attacker);
        g->defender = g_next_active(g, g->attacker);
        g->active = g->attacker;
        return;
    }
}

#endif
