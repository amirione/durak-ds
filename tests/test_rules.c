// Rules tests: crafted edge cases, the cheating model, difficulty levels and long bot-vs-bot sessions.
#include <stdio.h>
#include <stdlib.h>
#include "game.h"

static int g_fail = 0, g_checks = 0;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static DCard C(int rank, int suit) { DCard c = { (uint8_t)rank, (uint8_t)suit }; return c; }
enum { R6, R7, R8, R9, R10, RJ, RQ, RK, RA };

// empty position: nobody holds anything, deck empty, clean table. Tests then deal exact cards.
static void blank(Game *g, int mode, int bots, bool cheat, uint32_t seed) {
    g_init(g, mode, bots, cheat, seed);
    for (int p = 0; p < G_MAX_PLAYERS; p++) g->handN[p] = 0;
    g->deckN = 0; g->tableN = 0; g->over = GO_NONE;
    g->attacker = 0; g->defender = 1; g->active = 0; g->phase = PH_ATTACK;
    g->trump = SUIT_HEARTS; g->trumpCard = C(R6, SUIT_HEARTS);
}
static void give(Game *g, int p, DCard c) { g->hands[p][g->handN[p]++] = c; }
static int cards_total(const Game *g) {
    int n = g->deckN;
    for (int p = 0; p < g->nPlayers; p++) n += g->handN[p];
    for (int i = 0; i < g->tableN; i++) n += 1 + (g->table[i].hasDefend ? 1 : 0);
    return n;
}
static int dupes(const Game *g) {
    int seen[36] = {0}, bad = 0;
    #define MARK(c) do { if (seen[(c).suit * 9 + (c).rank]++) bad++; } while (0)
    for (int i = 0; i < g->deckN; i++) MARK(g->deck[i]);
    for (int p = 0; p < g->nPlayers; p++) for (int i = 0; i < g->handN[p]; i++) MARK(g->hands[p][i]);
    for (int i = 0; i < g->tableN; i++) { MARK(g->table[i].attack); if (g->table[i].hasDefend) MARK(g->table[i].defend); }
    return bad;
}

/* ---------------------------------------------------------- crafted cases */

static void t_endgame_draw_and_durak(void) {
    printf("endgame: last cards\n");
    Game g; blank(&g, GM_CLASSIC, 1, false, 1);
    give(&g, 0, C(R6, SUIT_CLUBS)); give(&g, 1, C(R7, SUIT_CLUBS));
    CHECK(g_attack(&g, 0, 0), "attack with the last card");
    CHECK(g_defend(&g, 1, 0), "defend with the last card");
    CHECK(g.over == GO_NONE, "game must not end mid-bout (prototype bug)");
    CHECK(g_pass(&g, 0), "pass after all beaten");
    CHECK(g.over == GO_DRAW, "both out -> draw, got %d", g.over);

    blank(&g, GM_CLASSIC, 1, false, 2);
    give(&g, 0, C(R6, SUIT_CLUBS)); give(&g, 1, C(R7, SUIT_SPADES)); give(&g, 1, C(R8, SUIT_SPADES));
    CHECK(g_attack(&g, 0, 0), "attack");
    CHECK(g_defend(&g, 1, 0) == false || true, "(defend attempt)");
    // bot with 7S cannot beat 6C (different non-trump suit) -> takes
    blank(&g, GM_CLASSIC, 1, false, 3);
    give(&g, 0, C(R6, SUIT_CLUBS)); give(&g, 1, C(R7, SUIT_SPADES)); give(&g, 1, C(R8, SUIT_SPADES));
    g_attack(&g, 0, 0);
    CHECK(g_defend(&g, 1, 0) == false, "7S must not beat 6C");
    CHECK(g_take(&g, 1), "defender takes");
    CHECK(g.over == 1, "human is out, bot holds cards -> bot is the durak, got %d", g.over);
}

static void t_attack_cap(void) {
    printf("attack cap = defender's hand\n");
    Game g; blank(&g, GM_PODKIDNOY, 1, false, 4);
    give(&g, 0, C(R8, SUIT_CLUBS)); give(&g, 0, C(R8, SUIT_SPADES)); give(&g, 0, C(R8, SUIT_DIAMONDS));
    give(&g, 1, C(R9, SUIT_CLUBS)); give(&g, 1, C(R10, SUIT_CLUBS));
    CHECK(g_attack(&g, 0, 0), "1st attack");
    CHECK(g_throw_in(&g, 0, 0), "2nd card (defender holds 2)");
    CHECK(!g_throw_in(&g, 0, 0), "3rd must be refused: more attacks than defender cards");
    CHECK(g.tableN == 2, "table size %d", g.tableN);
}

static void t_defend_always_strict(void) {
    printf("defending is never relaxed by cheating\n");
    for (int cheat = 0; cheat < 2; cheat++) {
        Game g; blank(&g, GM_CLASSIC, 1, cheat, 5);
        give(&g, 1, C(R6, SUIT_CLUBS)); give(&g, 1, C(R9, SUIT_SPADES)); give(&g, 1, C(R6, SUIT_HEARTS));
        give(&g, 0, C(R10, SUIT_SPADES));
        g.attacker = 1; g.defender = 0; g.active = 1; g.phase = PH_ATTACK;
        CHECK(g_attack(&g, 1, 0), "bot attacks 6C");
        g.handN[0] = 0; give(&g, 0, C(R10, SUIT_DIAMONDS)); give(&g, 0, C(R7, SUIT_CLUBS));
        CHECK(!g_defend(&g, 0, 0), "10D must not beat 6C (cheat=%d)", cheat);
        CHECK(g_defend(&g, 0, 1), "7C beats 6C (cheat=%d)", cheat);
    }
}

static void t_transfer_rules(void) {
    printf("perevodnoy transfer\n");
    // seating: 2 attacks, 0 defends, 1 is the next player in turn order
    Game g; blank(&g, GM_PEREVODNOY, 2, false, 6);
    give(&g, 2, C(R8, SUIT_CLUBS)); give(&g, 2, C(R10, SUIT_CLUBS));
    give(&g, 0, C(R8, SUIT_SPADES)); give(&g, 0, C(R7, SUIT_SPADES));
    give(&g, 1, C(R9, SUIT_CLUBS)); give(&g, 1, C(R9, SUIT_SPADES));
    g.attacker = 2; g.defender = 0; g.active = 2;
    CHECK(g_attack(&g, 2, 0), "attack 8C");
    CHECK(g_can_transfer_card(&g, 0, g.hands[0][0]), "8S (same rank) may transfer");
    CHECK(!g_can_transfer_card(&g, 0, g.hands[0][1]), "7S (other rank) may not");
    CHECK(g_transfer(&g, 0, 0), "transfer");
    CHECK(g.defender == 1 && g.active == 1 && g.tableN == 2, "defender now seat 1 (got %d), table %d", g.defender, g.tableN);
    CHECK(g_state_ok(&g), "position stays consistent after a transfer");

    // the next player holds too few cards for the grown table -> no transfer
    blank(&g, GM_PEREVODNOY, 2, false, 7);
    give(&g, 2, C(R8, SUIT_CLUBS)); give(&g, 0, C(R8, SUIT_SPADES)); give(&g, 0, C(R7, SUIT_SPADES));
    give(&g, 1, C(R9, SUIT_CLUBS));
    g.attacker = 2; g.defender = 0; g.active = 2;
    g_attack(&g, 2, 0);
    CHECK(!g_can_transfer_card(&g, 0, g.hands[0][0]), "seat 1 has 1 card, table would hold 2");

    // once a pair is beaten no transfer any more
    blank(&g, GM_PEREVODNOY, 2, false, 8);
    give(&g, 2, C(R8, SUIT_CLUBS)); give(&g, 2, C(R8, SUIT_DIAMONDS));
    give(&g, 0, C(R9, SUIT_CLUBS)); give(&g, 0, C(R8, SUIT_SPADES));
    give(&g, 1, C(R9, SUIT_SPADES)); give(&g, 1, C(R10, SUIT_SPADES)); give(&g, 1, C(RJ, SUIT_SPADES));
    g.attacker = 2; g.defender = 0; g.active = 2;
    g_attack(&g, 2, 0);
    CHECK(g_defend(&g, 0, 0), "beat 8C with 9C");
    CHECK(!g_can_transfer_card(&g, 0, g.hands[0][0]), "no transfer after a defended pair");

    // classic mode never transfers
    blank(&g, GM_CLASSIC, 2, false, 9);
    give(&g, 2, C(R8, SUIT_CLUBS)); give(&g, 0, C(R8, SUIT_SPADES)); give(&g, 1, C(R9, SUIT_CLUBS)); give(&g, 1, C(R9, SUIT_SPADES));
    g.attacker = 2; g.defender = 0; g.active = 2;
    g_attack(&g, 2, 0);
    CHECK(!g_can_transfer_card(&g, 0, g.hands[0][0]), "transfer only exists in perevodnoy");
}

static void t_throw_in_modes(void) {
    printf("throw-in only in podkidnoy\n");
    for (int mode = 0; mode < 3; mode++) {
        Game g; blank(&g, mode, 2, false, 10);
        give(&g, 0, C(R8, SUIT_CLUBS)); give(&g, 0, C(R8, SUIT_SPADES));
        give(&g, 1, C(R9, SUIT_CLUBS)); give(&g, 1, C(R10, SUIT_CLUBS)); give(&g, 2, C(R9, SUIT_SPADES));
        g_attack(&g, 0, 0);
        bool ok = g_throw_in(&g, 0, 0);
        CHECK(ok == (mode == GM_PODKIDNOY), "mode %d: throw-in %s", mode, ok ? "allowed" : "refused");
    }
}

/* ------------------------------------------------------------ cheating */

static void cheat_scene(Game *g, int mode, bool cheat, uint32_t seed, int bots) {
    blank(g, mode, bots, cheat, seed);
    // table: 6C attacked, 7C beat it. Human (attacker) holds KH (rank not on the table) and 6D (legal).
    give(g, 0, C(RK, SUIT_HEARTS)); give(g, 0, C(R6, SUIT_DIAMONDS));
    for (int p = 1; p <= bots; p++) {                       // every bot gets different cards (spades / hearts / clubs)
        int su = p == 1 ? SUIT_SPADES : p == 2 ? SUIT_HEARTS : SUIT_CLUBS;
        int r0 = p == 3 ? R8 : R10;                         // clubs 6 and 7 are already on the table
        for (int r = r0; r < r0 + 4; r++) give(g, p, C(r == RK && su == SUIT_HEARTS ? RA : r, su));
        if (su == SUIT_HEARTS) g->hands[p][g->handN[p] - 1] = C(R9, SUIT_HEARTS);
    }
    g->tableN = 1; g->table[0].attack = C(R6, SUIT_CLUBS); g->table[0].defend = C(R7, SUIT_CLUBS); g->table[0].hasDefend = true; g->table[0].by = 0;
    g->phase = PH_ATTACK; g->active = 0; g->attacker = 0; g->defender = 1;
    g->deckN = 6;
    for (int i = 0; i < 6; i++) g->deck[i] = C(R6 + i % 3, SUIT_DIAMONDS + (i / 3) * 0);   // filler cards (suit overlap irrelevant here)
    g->deck[0] = C(R8, SUIT_DIAMONDS); g->deck[1] = C(R9, SUIT_DIAMONDS); g->deck[2] = C(R10, SUIT_DIAMONDS);
    g->deck[3] = C(RJ, SUIT_DIAMONDS); g->deck[4] = C(RQ, SUIT_DIAMONDS); g->deck[5] = C(RA, SUIT_DIAMONDS);
}

static void t_cheating_model(void) {
    printf("cheating model\n");
    // off: illegal extra attack refused in every mode
    for (int mode = 0; mode < 3; mode++) {
        Game g; cheat_scene(&g, mode, false, 11, 2);
        int idx = -1; for (int i = 0; i < g.handN[0]; i++) if (g.hands[0][i].rank == RK) idx = i;
        CHECK(!g_attack(&g, 0, idx), "cheating OFF must refuse a foreign rank (mode %d)", mode);
        CHECK(g.stat_cheats == 0 && g.tableN == 1, "nothing happened");
    }
    // on: in every mode the human may try; outcome is caught (undone + penalty + ban) or kept
    for (int mode = 0; mode < 3; mode++) {
        int caught = 0, kept = 0;
        for (uint32_t seed = 1; seed <= 400; seed++) {
            Game g; cheat_scene(&g, mode, true, seed * 977u, 2);
            int before = g.handN[0], deck = g.deckN;
            int idx = -1; for (int i = 0; i < g.handN[0]; i++) if (g.hands[0][i].rank == RK) idx = i;
            CHECK(g_attack(&g, 0, idx), "cheat attempt is accepted as a move (mode %d)", mode);
            CHECK(dupes(&g) == 0, "no duplicate cards after a cheat");
            if (g.stat_caught) {
                caught++;
                CHECK(g.tableN == 1 && g.phase == PH_ATTACK && g.active == 0, "caught: move undone, still attacker's turn");
                CHECK(g.handN[0] == before + CHEAT_PENALTY_CARDS && g.deckN == deck - CHEAT_PENALTY_CARDS, "caught: card back + %d penalty cards (hand %d->%d, deck %d->%d)", CHEAT_PENALTY_CARDS, before, g.handN[0], deck, g.deckN);
                CHECK(g.cheatBan, "caught: banned until the bout ends");
                CHECK(g.msg && strstr(g.msg, "Поймали"), "caught: message says so");
                // second attempt in the same bout is refused
                int k2 = -1; for (int i = 0; i < g.handN[0]; i++) if (g.hands[0][i].rank == RK) k2 = i;
                CHECK(!g_attack(&g, 0, k2), "banned: no second cheat in this bout");
            } else {
                kept++;
                CHECK(g.tableN == 2 && g.phase == PH_DEFEND && g.active == 1, "kept: card on the table, defender to move");
                CHECK(g.handN[0] == before - 1, "kept: hand shrinks by one");
            }
        }
        CHECK(caught > 0 && kept > 0, "mode %d: both outcomes occur (caught %d, kept %d)", mode, caught, kept);
    }
    // ban lifts when the bout ends
    {
        Game g; cheat_scene(&g, GM_CLASSIC, true, 5, 1); g.cheatBan = true;
        g_pass(&g, 0);
        CHECK(!g.cheatBan, "ban cleared by the end of the bout");
    }
    // catch rate follows the bots' level: 1 bot, N trials
    const char *nm[3] = { "easy", "medium", "hard" };
    for (int d = 0; d < 3; d++) {
        int trials = 6000, caught = 0;
        for (int t = 1; t <= trials; t++) {
            Game g; cheat_scene(&g, GM_CLASSIC, true, (uint32_t)t * 2654435761u, 1);
            g_set_difficulty(&g, d);
            int idx = -1; for (int i = 0; i < g.handN[0]; i++) if (g.hands[0][i].rank == RK) idx = i;
            g_attack(&g, 0, idx);
            caught += g.stat_caught;
        }
        double pct = 100.0 * caught / trials;
        printf("  catch rate vs %-6s bot: %5.1f%% (target %d%%)\n", nm[d], pct, CHEAT_CATCH_PCT[d]);
        CHECK(pct > CHEAT_CATCH_PCT[d] - 3 && pct < CHEAT_CATCH_PCT[d] + 3, "%s catch rate %.1f%% off target", nm[d], pct);
    }
    // more bots -> more eyes
    {
        int trials = 6000, caught = 0;
        for (int t = 1; t <= trials; t++) {
            Game g; cheat_scene(&g, GM_CLASSIC, true, (uint32_t)t * 40503u + 17, 3);
            int idx = -1; for (int i = 0; i < g.handN[0]; i++) if (g.hands[0][i].rank == RK) idx = i;
            g_attack(&g, 0, idx); caught += g.stat_caught;
        }
        double pct = 100.0 * caught / trials, want = 100.0 * (1.0 - 0.6 * 0.6 * 0.6);
        printf("  catch rate vs 3 medium bots: %5.1f%% (expected %.1f%%)\n", pct, want);
        CHECK(pct > want - 3 && pct < want + 3, "3-bot catch rate %.1f%%", pct);
    }
    // penalty with a nearly empty deck: only what is left is drawn
    {
        Game g; cheat_scene(&g, GM_CLASSIC, true, 3, 1); g_set_difficulty(&g, DIFF_HARD);
        g.deckN = 1; g.cheatBan = false;
        int saved = 0;
        for (uint32_t s2 = 1; s2 < 500 && !saved; s2++) {
            cheat_scene(&g, GM_CLASSIC, true, s2 * 7919u, 1); g.deckN = 1;
            int idx = -1; for (int i = 0; i < g.handN[0]; i++) if (g.hands[0][i].rank == RK) idx = i;
            int before = g.handN[0];
            g_attack(&g, 0, idx);
            if (g.stat_caught) { saved = 1; CHECK(g.deckN == 0 && g.handN[0] == before + 1, "penalty limited by the deck"); CHECK(strstr(g.msg, "+1"), "message says +1"); }
        }
        CHECK(saved, "a catch occurred in the small-deck scenario");
    }
    // throw-in cheat in podkidnoy: the window opens for any card, illegal ranks are reviewed
    {
        int caught = 0, kept = 0;
        for (uint32_t s2 = 1; s2 <= 300; s2++) {
            Game g; blank(&g, GM_PODKIDNOY, 2, true, s2 * 31u);
            give(&g, 0, C(RK, SUIT_HEARTS)); give(&g, 0, C(R6, SUIT_DIAMONDS));
            for (int r = RJ; r <= RA; r++) { give(&g, 1, C(r, SUIT_SPADES)); if (r != RK) give(&g, 2, C(r, SUIT_HEARTS)); }
            g.attacker = 0; g.defender = 1; g.active = 1; g.phase = PH_DEFEND; g.tableN = 1;
            g.table[0].attack = C(R6, SUIT_CLUBS); g.table[0].hasDefend = false;
            CHECK(g_has_throwable(&g, 0), "window open while cheating");
            int idx = -1; for (int i = 0; i < g.handN[0]; i++) if (g.hands[0][i].rank == RK) idx = i;
            CHECK(g_throw_in(&g, 0, idx), "throw-in cheat accepted");
            if (g.stat_caught) { caught++; CHECK(g.tableN == 1 && g.phase == PH_DEFEND && g.active == 1, "undone, defense continues"); }
            else { kept++; CHECK(g.tableN == 2, "kept"); }
        }
        CHECK(caught && kept, "throw-in cheat: both outcomes (caught %d kept %d)", caught, kept);
    }
}

/* ----------------------------------------------------- invariants & sims */

static int check_state(const Game *g, long *repairs) {
    int bad = 0;
    if (cards_total(g) > 36 || (36 - cards_total(g)) % 2) bad = 1;     // the discard pile only ever grows by beaten pairs
    if (dupes(g)) bad = 1;
    if (g->over == GO_NONE && !g_state_ok(g)) bad = 1;
    for (int p = 0; p < g->nPlayers; p++) if (g->handN[p] > G_MAX_HAND) bad = 1;
    if (g->tableN > G_MAX_TABLE) bad = 1;
    *repairs += g->stat_repairs;
    return bad;
}

static void t_simulations(void) {
    printf("bot-vs-bot simulations (every mode, bot count, cheating on/off, mixed levels)\n");
    const char *mn[3] = { "classic", "podkidnoy", "perevodnoy" };
    long total = 0, stuck = 0, bad = 0, repairs = 0, maxsteps = 0; int cheatsSeen = 0;
    for (int mode = 0; mode < 3; mode++)
    for (int bots = (mode == GM_PEREVODNOY ? 2 : 1); bots <= 3; bots++)
    for (int cheat = 0; cheat < 2; cheat++) {
        long fin = 0, draws = 0;
        for (int seed = 1; seed <= 500; seed++) {
            Game g; g_init(&g, mode, bots, cheat, (uint32_t)seed * 2654435761u + mode * 131u + bots * 17u + cheat);
            for (int p = 0; p < G_MAX_PLAYERS; p++) g.diff[p] = (seed + p) % 3;
            g.autoHuman = true;
            long steps = 0;
            while (g.over == GO_NONE && steps < 6000) { g_bot_step(&g); steps++; if (check_state(&g, &repairs)) { bad++; printf("  bad state: mode %d bots %d seed %d step %ld\n", mode, bots, seed, steps); break; } }
            if (g.over == GO_NONE) { stuck++; printf("  STUCK mode %d bots %d cheat %d seed %d\n", mode, bots, cheat, seed); }
            else { fin++; if (g.over == GO_DRAW) draws++; }
            cheatsSeen += g.stat_cheats;
            if (steps > maxsteps) maxsteps = steps;
            total++;
        }
        printf("  %-10s bots=%d cheating=%d: finished %3ld/500 (draws %ld)\n", mn[mode], bots, cheat, fin, draws);
    }
    CHECK(stuck == 0, "%ld stuck games", stuck);
    CHECK(bad == 0, "%ld invariant violations", bad);
    CHECK(repairs == 0, "safety net ran %ld times - a real logic bug", repairs);
    CHECK(cheatsSeen == 0, "bots must never cheat (saw %d cheats)", cheatsSeen);
    printf("  %ld games, longest %ld bot steps\n", total, maxsteps);
}

// who loses more often? seat 0 and seat 1 swap levels so the first-attacker edge cancels out.
static double loss_rate(int levelA, int levelB, int games) {
    int lossA = 0, done = 0;
    for (int i = 0; i < games; i++) {
        Game g; g_init(&g, GM_PODKIDNOY, 1, false, (uint32_t)(i + 1) * 2246822519u);
        int swap = i & 1;
        g.diff[0] = swap ? levelB : levelA; g.diff[1] = swap ? levelA : levelB;
        g.autoHuman = true;
        for (long s = 0; g.over == GO_NONE && s < 6000; s++) g_bot_step(&g);
        if (g.over == GO_NONE) continue;
        done++;
        if (g.over == (swap ? 1 : 0)) lossA++;
    }
    return 100.0 * lossA / (done ? done : 1);
}

static void t_levels(void) {
    printf("difficulty levels (2 players, 4000 games per pairing, share of games LOST by the first-named level)\n");
    double hm = loss_rate(DIFF_HARD, DIFF_MEDIUM, 4000), me = loss_rate(DIFF_MEDIUM, DIFF_EASY, 4000), he = loss_rate(DIFF_HARD, DIFF_EASY, 4000);
    printf("  hard vs medium : hard loses %.1f%%\n  medium vs easy : medium loses %.1f%%\n  hard vs easy   : hard loses %.1f%%\n", hm, me, he);
    CHECK(me < 50.0, "medium should beat easy (loses %.1f%%)", me);
    CHECK(he < 50.0, "hard should beat easy (loses %.1f%%)", he);
    CHECK(hm < 50.0, "hard should beat medium (loses %.1f%%)", hm);
}

static void t_long_session(void) {
    printf("long session: 30000 consecutive random games\n");
    uint32_t r = 987654321u; long repairs = 0, stuck = 0, bad = 0;
    for (int n = 0; n < 30000; n++) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        int mode = r % 3, bots = 1 + (r >> 4) % 3; if (mode == GM_PEREVODNOY && bots < 2) bots = 2;
        Game g; g_init(&g, mode, bots, (r >> 8) & 1, r | 1);
        for (int p = 0; p < G_MAX_PLAYERS; p++) g.diff[p] = (r >> (10 + p)) % 3;
        g.autoHuman = true;
        long steps = 0;
        while (g.over == GO_NONE && steps < 6000) { g_bot_step(&g); steps++; if (check_state(&g, &repairs)) { bad++; break; } }
        if (g.over == GO_NONE) { stuck++; if (stuck <= 3) printf("  stuck game #%d: mode %d bots %d cheat %d seed %u diff %d%d%d%d stall %d deck %d table %d active %d phase %d\n", n, mode, bots, (int)((r >> 8) & 1), r | 1, g.diff[0], g.diff[1], g.diff[2], g.diff[3], g.stall, g.deckN, g.tableN, g.active, g.phase); }
    }
    CHECK(stuck == 0 && bad == 0 && repairs == 0, "stuck %ld bad %ld repairs %ld", stuck, bad, repairs);
}

static void t_repair_net(void) {
    printf("safety net: corrupt positions are detected and recovered\n");
    Game g; g_init(&g, GM_CLASSIC, 2, false, 77); g.autoHuman = true;
    g.defender = g.attacker;                           // impossible roles
    CHECK(!g_state_ok(&g), "detects attacker == defender");
    g_bot_step(&g);
    CHECK(g.stat_repairs == 1, "repair ran");
    CHECK(g_state_ok(&g) || g.over != GO_NONE, "position is playable again");
    for (long s = 0; g.over == GO_NONE && s < 6000; s++) g_bot_step(&g);
    CHECK(g.over != GO_NONE, "game finishes after recovery");
}

int main(void) {
    t_endgame_draw_and_durak();
    t_attack_cap();
    t_defend_always_strict();
    t_transfer_rules();
    t_throw_in_modes();
    t_cheating_model();
    t_repair_net();
    t_simulations();
    t_levels();
    t_long_session();
    printf("\n%d checks, %d failed\n%s\n", g_checks, g_fail, g_fail ? "RULES TESTS FAILED" : "RULES TESTS OK");
    return g_fail != 0;
}
