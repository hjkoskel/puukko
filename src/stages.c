/* PUUKKO C64 – the four filthy locations as PETSCII screens.
 * Ground line = top of text row 20 (fighters' feet). Each stage sets
 * $D021 raster bands (sky / ground) like the browser version's rasters. */
#include "puukko.h"

const char *const stage_names[NSTAGES] = {
    "KIOSKIN TAKAPIHA", "MUTAINEN PIHAKUJA", "PYS[K]INTIALUE", "SULJETTU ALKO"
};

/* PETSCII screen codes used for the art */
#define SOLID   160
#define BALL    81
#define RING    87
#define HLINE   64
#define VLINE   93
#define HALF_L  97     /* left half block */
#define BAR_LO  82     /* low horizontal bar */
#define CHECK   102
#define DOT     46
#define COMMA   44
#define PI      94
#define DIAG_D  77     /* \ */
#define DIAG_U  78     /* / */
#define TOPLINE_REV 227   /* reversed top 1/8 line: plank */
#define MIDBAR_REV  195   /* reversed thick middle bar: shutter slat */
#define HALF_UP_REV 226   /* reversed lower half = upper half filled */

static u16 lcg;
static u8 lrnd(u8 n)
{
    lcg = lcg * 109 + 89;
    return (u8)(((lcg >> 8) * n) >> 8);
}

static void stars(u16 seed, u8 n, u8 maxRow)
{
    u8 x, y;
    lcg = seed;
    while (n--) {
        x = lrnd(40);
        y = lrnd(maxRow);
        put(x, y, lrnd(4) ? DOT : '*', lrnd(3) ? C_WHITE : C_GRAY);
    }
}

static void speckle(u16 seed, u8 col)
{
    u8 i;
    lcg = seed;
    for (i = 0; i < 45; ++i)
        put(lrnd(40), 20 + lrnd(5), lrnd(2) ? DOT : COMMA, col);
}

/* bottle standing on the ground: neck + body */
static void bottle(u8 x, u8 col)
{
    put(x, 21, VLINE, col);
    put(x, 22, 0x62, col);   /* lower half block */
}

static void fence(u8 x0, u8 x1, u8 y0, u8 y1, u8 r1, u8 r2, u8 col)
{
    u8 x;
    for (x = x0; x < x1; x += 2) fill(x, y0, 1, y1 - y0 + 1, HALF_L, col);
    for (x = x0 + 1; x < x1; x += 2) {
        put(x, r1, HLINE, col);
        put(x, r2, HLINE, col);
    }
    for (x = x0; x < x1; x += 2) put(x, y0, 0x69, col);   /* pointed tops */
}

/* ---------------------------------------------------------------- 1. kiosk */
static void kiosk(void)
{
    splits_off(C_BLACK);
    splits_add(20, C_DGRAY);
    POKE(VIC_BORDER, C_BLUE);
    stars(7, 40, 11);
    put(32, 2, BALL, C_WHITE);
    put(33, 2, 0x7C, C_WHITE);
    /* kiosk building */
    fill(4, 7, 15, 1, SOLID, C_DGRAY);
    fill(4, 8, 15, 12, TOPLINE_REV, C_ORANGE);
    fill(5, 8, 13, 1, SOLID, C_RED);
    textrev(8, 8, "KAUPPA", C_RED);
    /* lit window + drunk silhouette */
    fill(6, 9, 4, 1, SOLID, C_DGRAY);
    fill(6, 10, 4, 3, SOLID, C_YELLOW);
    put(8, 11, BALL | 0x80, C_YELLOW);
    put(8, 12, HALF_UP_REV, C_YELLOW);
    /* door */
    fill(14, 13, 3, 7, 32, C_BLACK);
    put(16, 16, DOT, C_YELLOW);
    /* fence */
    fence(20, 40, 15, 19, 16, 19, C_ORANGE);
    /* concrete: puddle + bottles */
    fill(23, 22, 4, 1, BAR_LO, C_BLUE);
    speckle(11, C_BLACK);
    bottle(28, C_LGREEN);
    bottle(31, C_ORANGE);
    bottle(15, C_LGREEN);
}

/* ---------------------------------------------------------------- 2. yard */
static void yard(void)
{
    splits_off(C_ORANGE);
    splits_add(9, C_RED);
    splits_add(14, C_DGRAY);
    splits_add(20, C_BROWN);
    POKE(VIC_BORDER, C_RED);
    /* low sun */
    fill(30, 9, 2, 1, SOLID, C_YELLOW);
    put(31, 10, 0x62, C_ORANGE);
    /* dead tree */
    fill(34, 5, 1, 7, VLINE, C_BLACK);
    put(33, 6, DIAG_D, C_BLACK);
    put(32, 5, DIAG_D, C_BLACK);
    put(35, 8, DIAG_U, C_BLACK);
    put(36, 7, DIAG_U, C_BLACK);
    /* fence */
    fence(0, 40, 12, 19, 13, 18, C_BROWN);
    /* dumpster */
    fill(4, 16, 7, 4, SOLID, C_GREEN);
    fill(4, 16, 7, 1, SOLID, C_GRAY);
    fill(5, 17, 2, 1, SOLID, C_BLACK);
    textrev(6, 18, "J[TE", C_GREEN);
    /* mud */
    fill(7, 22, 5, 1, BAR_LO, C_BLUE);
    fill(18, 22, 6, 1, BAR_LO, C_BLUE);
    speckle(23, C_RED);
    bottle(13, C_LGREEN);
    bottle(27, C_ORANGE);
    bottle(30, C_LGREEN);
    bottle(11, C_ORANGE);
    /* a crow on the fence */
    put(25, 11, PI, C_BLACK);
}

/* ---------------------------------------------------------------- 3. parking */
static void parking(void)
{
    u8 x;
    splits_off(C_BLACK);
    splits_add(20, C_DGRAY);
    POKE(VIC_BORDER, C_GRAY);
    stars(31, 30, 8);
    /* far wall with dark windows */
    fill(0, 10, 40, 2, SOLID, C_DGRAY);
    for (x = 1; x < 40; x += 8) fill(x, 10, 2, 2, 32, C_BLACK);
    /* street lamp */
    fill(36, 8, 1, 12, VLINE, C_GRAY);
    fill(33, 8, 4, 1, HLINE, C_GRAY);
    put(33, 9, HALF_UP_REV, C_GRAY);
    put(33, 10, BALL, C_YELLOW);
    put(32, 11, DIAG_U, C_YELLOW);
    put(34, 11, DIAG_D, C_YELLOW);
    /* light pool on the concrete */
    fill(31, 22, 4, 1, CHECK, C_GRAY);
    fill(32, 23, 2, 1, CHECK, C_GRAY);
    /* parking lines */
    for (x = 2; x < 40; x += 7) fill(x, 22, 1, 2, VLINE, C_WHITE);
    /* graffiti */
    text(4, 18, "PUUKKO 4EVER", C_RED);
    text(14, 19, "JORMA RAEEKAI", C_CYAN);
    text(25, 18, "SAFKA <3", C_PURPLE);
    speckle(41, C_GRAY);
    put(29, 21, RING, C_BLACK);
    bottle(12, C_LGREEN);
    bottle(16, C_ORANGE);
}

/* ---------------------------------------------------------------- 4. alko */
static void alko(void)
{
    splits_off(C_BLACK);
    splits_add(20, C_GRAY);
    POKE(VIC_BORDER, C_RED);
    stars(55, 24, 5);
    fill(2, 6, 36, 1, SOLID, C_GRAY);
    fill(2, 7, 36, 13, SOLID, C_DGRAY);
    /* rolling shutter */
    fill(7, 11, 25, 9, MIDBAR_REV, C_GRAY);
    fill(18, 16, 2, 1, 32, C_BLACK);
    /* ALKO sign */
    fill(12, 7, 16, 3, SOLID, C_RED);
    textrev(18, 8, "ALKO", C_RED);
    /* SULJETTU */
    fill(15, 12, 8, 2, 32, C_BLACK);
    text(15, 12, "SULJETTU", C_WHITE);
    /* side window */
    fill(3, 12, 3, 3, SOLID, C_BLUE);
    speckle(61, C_DGRAY);
    bottle(10, C_LGREEN);
    bottle(17, C_LGREEN);
    bottle(26, C_ORANGE);
    bottle(30, C_LGREEN);
    /* stray cat */
    put(34, 21, BALL, C_BLACK);
    put(33, 21, 0x69, C_BLACK);
    put(35, 21, DIAG_D, C_BLACK);
}

void stage_draw(u8 id)
{
    cls();
    switch (id) {
    case 0: kiosk(); break;
    case 1: yard(); break;
    case 2: parking(); break;
    default: alko(); break;
    }
}
