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
enum { DIFF_EASY, DIFF_MEDIUM, DIFF_HARD };

// Cheating model (only the human cheats; bots never do):
//  - a cheat is adding a card whose rank is not on the table (extra attack, or a throw-in in podkidnoy);
//  - every other player gets one look at it; each catches it with the chance below, set by that player's level;
//  - caught: the move is undone, the card returns to the hand, the cheater draws penalty cards from the deck
//    and may not cheat again until the bout ends. Not caught: the card stays and counts as a normal one.
#define CHEAT_PENALTY_CARDS 2

// Stalemate rule: if this many bouts in a row end with the defender taking while nothing is drawn from the deck
// (everybody already holds 6+ cards, or the deck is empty) and nothing is beaten, nobody can make progress -> draw.
// Only 'take' loops can be endless: every beaten pair removes cards from play for good.
#define G_STALL_LIMIT 12
static const int CHEAT_CATCH_PCT[3] = { 20, 40, 65 };

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
    int diff[G_MAX_PLAYERS];                    // bot level per seat (DIFF_*)
    bool cheatBan;                              // the human was caught: no cheating until the bout ends
    uint8_t discarded[36];                      // public knowledge: cards that left the game (beaten pairs)
    int stall;                                  // consecutive bouts that ended with a take and no progress
    int stat_cheats, stat_caught, stat_repairs; // counters for tests and diagnostics
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
    for (int p = 0; p < G_MAX_PLAYERS; p++) g->diff[p] = DIFF_MEDIUM;
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

static void g_set_difficulty(Game *g, int d) {            // every bot seat; seat 0 only matters when autoHuman is on
    for (int p = 0; p < G_MAX_PLAYERS; p++) g->diff[p] = d;
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

static bool g_rank_ok(const Game *g, DCard c) {          // the card matches a rank already on the table
    return g->tableN == 0 || (g_open_mask(g) & (1 << c.rank));
}

static bool g_cheat_allowed(const Game *g, int p) {      // only the human may cheat, once per bout after a catch
    return g->cheating && p == 0 && !g->cheatBan;
}

static bool g_can_attack_card(const Game *g, DCard c) { // strictly legal
    return g_defender_can_take_more(g) && g_rank_ok(g, c);
}

// may player p add card c while somebody else is defending (podkidnoy)? legal, or a cheat the human may try
static bool g_can_throw_card(const Game *g, int p, DCard c) {
    if (g->mode != GM_PODKIDNOY || g->phase != PH_DEFEND || p == g->defender) return false;
    if (g->tableN == 0 || !g_defender_can_take_more(g)) return false;
    return g_rank_ok(g, c) || g_cheat_allowed(g, p);
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

// Called right after card c was put on the table by a cheat. Returns true if somebody caught it (move undone).
static bool g_cheat_review(Game *g, int p, DCard c, int prevPhase, int prevActive) {
    g->stat_cheats++;
    bool caught = false;
    for (int q = 0; q < g->nPlayers && !caught; q++) {
        if (q == p) continue;
        if ((int)(g_rand(g) % 100u) < CHEAT_CATCH_PCT[g->diff[q]]) caught = true;
    }
    if (!caught) return false;
    g->tableN--;                                           // take the card off the table again
    g->hands[p][g->handN[p]++] = c;
    g->phase = prevPhase; g->active = prevActive;
    int pen = 0;
    while (pen < CHEAT_PENALTY_CARDS && g->deckN > 0 && g->handN[p] < G_MAX_HAND) { g->hands[p][g->handN[p]++] = g_draw_one(g); pen++; }
    g_sort_hand(g, p);
    g->cheatBan = true;
    g->stat_caught++;
    g->msg = pen >= 2 ? "Поймали! Штраф: +2 карты"
           : pen == 1 ? "Поймали! Штраф: +1 карта"
                      : "Поймали! Карта вернулась";
    return true;
}

static bool g_attack(Game *g, int p, int ci) {
    if (g->over != GO_NONE || g->phase != PH_ATTACK || g->active != p || p != g->attacker) return false;
    if (ci < 0 || ci >= g->handN[p]) return false;
    DCard c = g->hands[p][ci];
    bool cheat = false;
    if (!g_can_attack_card(g, c)) {
        if (g_cheat_allowed(g, p) && g->tableN > 0 && g_defender_can_take_more(g)) cheat = true;
        else {
            g->msg = g->tableN >= G_MAX_TABLE || !g_defender_can_take_more(g) ? "Больше карт подкинуть нельзя"
                                                                               : "Такого ранга нет на столе";
            return false;
        }
    }
    int prevPhase = g->phase, prevActive = g->active;
    g_add_attack(g, p, ci);
    g->phase = PH_DEFEND; g->active = g->defender;
    g->msg = p == 0 ? "Ждём защиту…" : "Отбивайся!";
    if (cheat) g_cheat_review(g, p, c, prevPhase, prevActive);
    return true;
}

static bool g_throw_in(Game *g, int p, int ci) {
    if (g->over != GO_NONE || ci < 0 || ci >= g->handN[p]) return false;
    DCard c = g->hands[p][ci];
    if (!g_can_throw_card(g, p, c)) { g->msg = "Эту карту подкинуть нельзя"; return false; }
    bool cheat = !g_rank_ok(g, c);
    int prevPhase = g->phase, prevActive = g->active;
    g_add_attack(g, p, ci);
    g->msg = p == 0 ? "Подкинул. Ждём защиту…" : "Подкинули карту.";
    if (cheat) g_cheat_review(g, p, c, prevPhase, prevActive);
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
    int att = g->attacker, def = g->defender, deckBefore = g->deckN;
    g->tableN = 0; g->cheatBan = false;
    g_refill(g, att, def);
    g->phase = PH_ATTACK;
    g->msg = p == 0 ? "Ты забрал карты. Ход переходит дальше." : "Бот забрал карты.";
    g_check_over(g);
    g->stall = (g->deckN == deckBefore) ? g->stall + 1 : 0;        // no card was drawn: no progress
    if (g->over == GO_NONE && g->stall >= G_STALL_LIMIT) { g->over = GO_DRAW; g->msg = "Ничья: никто не может отбиться"; }
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
    for (int i = 0; i < g->tableN; i++) {                  // beaten cards leave the game; everybody can see them
        g->discarded[g->table[i].attack.suit * 9 + g->table[i].attack.rank] = 1;
        if (g->table[i].hasDefend) g->discarded[g->table[i].defend.suit * 9 + g->table[i].defend.rank] = 1;
    }
    g->tableN = 0; g->cheatBan = false; g->stall = 0;
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

/* ------------------------------------------------- consistency / recovery */

// The roles and the table must always describe a position the game can continue from.
static bool g_state_ok(const Game *g) {
    if (g->over != GO_NONE) return true;
    int n = g->nPlayers;
    if (g->attacker < 0 || g->attacker >= n || g->defender < 0 || g->defender >= n || g->attacker == g->defender) return false;
    if (g->active != (g->phase == PH_DEFEND ? g->defender : g->attacker)) return false;
    if (g->tableN == 0) {
        if (g->phase != PH_ATTACK) return false;
        if (g->deckN == 0 && (g->handN[g->attacker] == 0 || g->handN[g->defender] == 0)) return false;
    } else if (g->phase == PH_DEFEND && g_open_pair(g) < 0) return false;
    return true;
}

// Last-resort recovery, never expected to run (tests assert stat_repairs == 0): the defender takes the table.
static void g_repair(Game *g) {
    g->stat_repairs++;
    if (g->over != GO_NONE) return;
    int def = (g->defender >= 0 && g->defender < g->nPlayers) ? g->defender : 0;
    for (int i = 0; i < g->tableN; i++) {
        if (g->handN[def] < G_MAX_HAND) g->hands[def][g->handN[def]++] = g->table[i].attack;
        if (g->table[i].hasDefend && g->handN[def] < G_MAX_HAND) g->hands[def][g->handN[def]++] = g->table[i].defend;
    }
    g->tableN = 0; g->cheatBan = false; g->stall = 0; g->phase = PH_ATTACK;
    g_refill(g, 0, def == 0 ? 1 % g->nPlayers : def);
    g_check_over(g);
    if (g->over == GO_NONE) {
        g->attacker = g_next_active(g, def);
        g->defender = g_next_active(g, g->attacker);
        g->active = g->attacker;
    }
}

/* ------------------------------------------------------------------ bots */

// lower key = cheaper card to give up. Easy bots ignore trumps; the others keep them for later.
static int g_bot_key(const Game *g, int p, DCard c) {
    if (g->diff[p] == DIFF_EASY) return c.rank;
    return c.rank + (c.suit == g->trump ? 20 : 0);
}

static int g_copies_of_rank(const Game *g, int p, int rank) {
    int n = 0;
    for (int i = 0; i < g->handN[p]; i++) if (g->hands[p][i].rank == rank) n++;
    return n;
}

// cheapest card that satisfies `mode` (0 attack/throw-in, 1 defend, 2 transfer), or -1
static int g_bot_pick(const Game *g, int p, int mode, int openIdx) {
    int best = -1, bestKey = 1000;
    for (int i = 0; i < g->handN[p]; i++) {
        DCard c = g->hands[p][i];
        bool ok;
        if (mode == 0) ok = g_rank_ok(g, c);
        else if (mode == 1) ok = g_beats(g->table[openIdx].attack, c, g->trump);
        else ok = c.rank == g->table[openIdx].attack.rank;
        if (!ok) continue;
        int k = g_bot_key(g, p, c);
        if (mode == 0 && g->diff[p] == DIFF_HARD) k -= 4 * (g_copies_of_rank(g, p, c.rank) - 1);  // pairs keep the attack going
        if (k < bestKey) { bestKey = k; best = i; }
    }
    return best;
}

// Hard bots only: do not spend a trump on a cheap lone attack while the deck is still deep (public information only)
static bool g_hard_prefers_take(const Game *g, int p, int o, int beat) {
    DCard atk = g->table[o].attack, c = g->hands[p][beat];
    if (c.suit != g->trump || atk.suit == g->trump) return false;
    return atk.rank <= 1 && g->deckN >= 8 && g->tableN == 1 && g->handN[p] <= 8;
}

static bool g_bot_throw_in(Game *g) {              // podkidnoy: a bystander bot adds a card before the bout ends
    if (g->mode != GM_PODKIDNOY || !g_defender_can_take_more(g)) return false;
    for (int k = 1; k < g->nPlayers; k++) {
        int p = (g->attacker + k) % g->nPlayers;
        if (p == g->defender || p == g->attacker || g->handN[p] == 0) continue;
        if (p == 0 && !g->autoHuman) continue;
        int ci = g_bot_pick(g, p, 0, -1);
        if (ci < 0) continue;
        if (g->diff[p] != DIFF_EASY && g->hands[p][ci].suit == g->trump && g->deckN > 0) continue;
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
    if (!g_state_ok(g)) { g_repair(g); return; }
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
        if (beat >= 0 && g->diff[p] == DIFF_HARD && g_hard_prefers_take(g, p, o, beat)) beat = -1;
        if (beat >= 0) g_defend(g, p, beat);
        else g_take(g, p);
        return;
    }

    if (g->phase == PH_ATTACK && p == g->attacker) {
        int ci = g_bot_pick(g, p, 0, -1);
        if (ci >= 0 && g->tableN > 0 && g->diff[p] != DIFF_EASY && g->hands[p][ci].suit == g->trump && g->deckN > 0) ci = -1;
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
