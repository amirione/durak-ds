// DURAK DS - skeleton port (C / libnds)
// Bottom screen: hardware 3D hand of cards, picked with the stylus.
// Top screen: plain text console (status).
//
// Coordinates: 1 pixel on the touch screen = 1/32 unit in 3D space,
// projection is glOrtho(-4, 4, -3, 3, -1, 1), so 256x192 px maps 1:1.

#include <nds.h>
#include <stdio.h>

#define HAND_MAX   6
#define CARD_W     50
#define CARD_H     70
#define CARD_STEP  36
#define HAND_CY    140
#define LIFT_PX    14

typedef struct {
    u8 rank; // 0..8  -> 6 7 8 9 10 J Q K A
    u8 suit; // 0..3  -> spades hearts diamonds clubs
} Card;

static const char *RANK_NAME[9] = { "6", "7", "8", "9", "10", "J", "Q", "K", "A" };
static const char *SUIT_NAME[4] = { "spades", "hearts", "diamonds", "clubs" };

static Card hand[HAND_MAX] = {
    { 0, 1 }, { 3, 0 }, { 4, 2 }, { 5, 3 }, { 7, 1 }, { 8, 0 }
};
static int handCount = HAND_MAX;

// pixel -> v16 (4.12 fixed). 1 px = 128/4096 = 1/32 unit.
static inline v16 px(int x) { return (v16)((x - 128) * 128); }
static inline v16 py(int y) { return (v16)((96 - y) * 128); }

static inline int handX(int i) {
    return 128 + (2 * i - (handCount - 1)) * (CARD_STEP / 2);
}

static void rect(int x0, int y0, int x1, int y1, int z) {
    glVertex3v16(px(x0), py(y0), (v16)z);
    glVertex3v16(px(x1), py(y0), (v16)z);
    glVertex3v16(px(x1), py(y1), (v16)z);
    glVertex3v16(px(x0), py(y1), (v16)z);
}

static void diamond(int cx, int cy, int r, int z) {
    glVertex3v16(px(cx),     py(cy - r), (v16)z);
    glVertex3v16(px(cx + r), py(cy),     (v16)z);
    glVertex3v16(px(cx),     py(cy + r), (v16)z);
    glVertex3v16(px(cx - r), py(cy),     (v16)z);
}

static void drawCard(const Card *c, int cx, int cy, int layer, bool selected) {
    int hw = CARD_W / 2, hh = CARD_H / 2;
    int z = layer * 300; // each card on its own depth layer (later = closer)
    bool red = (c->suit == 1 || c->suit == 2);

    glBegin(GL_QUADS);

    // outline (red when selected)
    if (selected) glColor3b(193, 18, 31); else glColor3b(10, 10, 10);
    rect(cx - hw, cy - hh, cx + hw, cy + hh, z);

    // paper
    glColor3b(255, 255, 255);
    rect(cx - hw + 2, cy - hh + 2, cx + hw - 2, cy + hh - 2, z + 60);

    // suit marker (placeholder: diamond; real glyphs + rank font come next)
    if (red) glColor3b(193, 18, 31); else glColor3b(10, 10, 10);
    diamond(cx, cy, 10, z + 120);

    // rank marker top-left (placeholder: small square, size grows with rank)
    rect(cx - hw + 5, cy - hh + 5, cx - hw + 7 + c->rank, cy - hh + 9, z + 120);

    glEnd();
}

static int pickCard(int tx, int ty) {
    for (int i = handCount - 1; i >= 0; i--) { // topmost card first
        int cx = handX(i);
        if (tx >= cx - CARD_W / 2 && tx <= cx + CARD_W / 2 &&
            ty >= HAND_CY - CARD_H / 2 - LIFT_PX && ty <= HAND_CY + CARD_H / 2)
            return i;
    }
    return -1;
}

static void playCard(int i) {
    iprintf("Played: %s of %s\n", RANK_NAME[hand[i].rank], SUIT_NAME[hand[i].suit]);
    for (int k = i; k < handCount - 1; k++) hand[k] = hand[k + 1];
    handCount--;
}

int main(void) {
    lcdMainOnBottom();            // 3D engine -> bottom (touch) screen
    videoSetMode(MODE_0_3D);
    consoleDemoInit();            // sub engine text console -> top screen

    glInit();
    glEnable(GL_ANTIALIAS);
    glClearColor(31, 31, 31, 31); // white
    glClearPolyID(63);
    glClearDepth(0x7FFF);
    glViewport(0, 0, 255, 191);

    iprintf("DURAK DS\n");
    iprintf("Tap a card, lift stylus to play it.\n\n");

    touchPosition touch;
    int selected = -1;

    while (1) {
        scanKeys();
        int held = keysHeld();
        int up = keysUp();

        if (held & KEY_TOUCH) {
            touchRead(&touch);
            selected = pickCard(touch.px, touch.py);
        }
        if (up & KEY_TOUCH) {
            if (selected >= 0 && selected < handCount) playCard(selected);
            selected = -1;
        }

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-4, 4, -3, 3, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE);

        for (int i = 0; i < handCount; i++) {
            int cy = HAND_CY - (i == selected ? LIFT_PX : 0);
            drawCard(&hand[i], handX(i), cy, i, i == selected);
        }

        glFlush(0);
        swiWaitForVBlank();
    }
    return 0;
}
