/* PUUKKO C64 – fighters as VIC-II hardware sprites.
 *
 * The browser version composes 24x21 frames from string-art parts (head,
 * torso, arms, legs, props). We do the same at runtime, then split every
 * frame into 3 stacked, X+Y expanded (48x42) hardware sprites:
 *   layer 0 hi-res  skin   (light red)
 *   layer 1 hi-res  shirt  (per character)
 *   layer 2 multicolour: pants (sprite colour), MC0 grey, MC1 black
 * The hi-res layers sit on top, so the chunky multicolour pixels only show
 * at full width where nothing else covers them: eyes stay 1 pixel wide. Frames are cached facing right and
 * mirrored through lookup tables when copied to the hardware buffers. */
#include <string.h>
#include "puukko.h"

/* role codes in the composition canvas */
#define L_NONE  0
#define L_SKIN  1   /* hi-res layer 0 (light red)      */
#define L_SHIRT 2   /* hi-res layer 1 (per character)  */
#define L_PANTS 3   /* MC %10 sprite colour            */
#define L_GREY  4   /* MC %01 $D025 grey               */
#define L_BLACK 5   /* MC %11 $D026 black              */

/* ---------------------------------------------------------------- characters */
enum { HD_REISKA, HD_JORMA, HD_SAFKA, HD_MASA, HD_KAKE, HD_PENA };
enum { TO_NORMAL, TO_THIN, TO_BIG };

const CharDef chars[6] = {
    { "REISKA", "48 VUOTIAS. PUUKKO MUKANA VUODESTA 1987.",
      100, 218, 130, 100, 218, 256, 1, 2, 1, 0, 0, HD_REISKA, TO_NORMAL, 3, 5,
      C_GREEN, C_BLUE, L_GREY, L_BLACK },
    { "JORMA", "OHUT KUIN KAHVI. PUKAHTAA ENNEN KUIN JUO.",
      90, 282, 90, 90, 346, 282, 1, 2, 1, 0, 0, HD_JORMA, TO_THIN, 3, 5,
      C_YELLOW, C_DGRAY, L_PANTS, L_BLACK },
    { "SAFKA", "KIOSKIN VAKIOASIAKAS. PULLOT TUNTEE H[NET.",
      95, 269, 100, 125, 256, 282, 1, 2, 1, 0, 1, HD_SAFKA, TO_NORMAL, 2, 4,
      C_PURPLE, C_GREEN, L_BLACK, L_BLACK },
    { "MASA", "VATSA ON PANKKI. PANKKI ON HIDAS.",
      120, 179, 110, 110, 205, 205, 1, 2, 1, 0, 0, HD_MASA, TO_BIG, 2, 5,
      C_ORANGE, C_BROWN, L_GREY, L_BLACK },
    { "KAKE", "VETERAANI. SELVISI KOLMESTA SODASTA.",
      100, 243, 100, 100, 256, 256, 1, 2, 2, 0, 0, HD_KAKE, TO_NORMAL, 3, 5,
      C_DGRAY, C_GREEN, L_BLACK, L_BLACK },
    { "PENA", "KUKAAN EI TIED[, MINNE PENA MENEE.",
      95, 256, 100, 100, 256, 256, 2, 5, 1, 8, 0, HD_PENA, TO_NORMAL, 3, 5,
      C_CYAN, C_BROWN, L_PANTS, L_GREY }
};

/* ---------------------------------------------------------------- part art */
/* letters: S skin N nose W teeth E eyes M mouth H hair C hat B hair(safka)
 * G beard 1/2 shirt 3 pants D belt 5 shoes R stain
 * props: k knife handle b blade o bottle l label w box r cross */
static const char head_art[6][97] = {
    ".HHHHHHHH..." "HHHHHHHHHH.." ".HSSSSSSSH.." ".SSESSESSS.."
    ".SSSSSSNSS.." ".SSSMMMSSS.." "..SSSSSSS..." "...SSSS.....",
    "..CCCCCC...." ".CCCCCCCC..." ".CCCCCCCCC.." ".SSESSESSS.."
    ".SSSSSSNSS.." ".SSSMMMSSS.." "..SSSSSS...." "...SSSS.....",
    "..BBBBBBBB.." ".BBBBBBBBBB." ".SSESSESSBB." ".SSSSSSNSSB."
    ".SSWMMMWWSS." ".SSSWWWWSSS." "..SSSSSSS..." "...SSSS.....",
    "..HHHHHHHH.." ".HHHHHHHHHH." ".HSESSESHHH." ".HSSSSSSSHH."
    ".HSSSSSNSSH." ".HSSSMMMSSH." ".HHSSSSSSHH." "..HSSSSSHH..",
    ".CCCCCCCC..." "CCCCCCCCCCC." "CCCCCCCCCCCC" ".SSESSESSS.."
    ".SSSSSSNSS.." ".GGGMMMGGG.." ".GGSSSSSGG.." "..GGSSSGG...",
    ".H.HHHH.HH.." ".HHHHHHHHH.." ".HSSSSSSSH.." ".SSESSESSS.."
    ".SSSSSSNSS.." ".SSSMMMSSS.." "..SSSSSSS..." "...SSSS....."
};

static const char torso_art[3][85] = {
    ".1111111111." "111111111111" "111111111122" "111111111122"
    "11R111111222" ".DDDDDDDDDD." "..33333333..",
    "..11111111.." "..11111111.." "..11111112.." "..11111112.."
    "..1R111112.." "..DDDDDDDD.." "...3333333..",
    "111111111111" "111111111122" "111111111122" "111111111122"
    "11R111111222" "DDDDDDDDDDDD" "..33333333.."
};

/* overlays (12x1): grit eyes, pain eyes, grit mouth, open mouth, grin */
static const char ov_eyegrit[] = "...E...E....";
static const char ov_eyepain[] = "...EE.EE....";
static const char ov_mgrit[]   = "...WWWW.....";
static const char ov_mopen[]   = "...MMMM.....";
static const char ov_mgrin[]   = "..WWWWWW....";

enum { BA_DOWN, BA_PAIN, BA_WIN };
static const char arm_back[3][65] = {
    "....1111" "....1111" "....111." "....SSS." "....SSS." "....SSS." "....SSS." "....SSS.",
    "........" "....SSS." "....SSS." "....111." "....1111" "........" "........" "........",
    "....SSS." "....SSS." "....SSS." "....111." "....111." "....1111" "........" "........"
};

enum { FA_DOWN, FA_STABBACK, FA_STABEXT, FA_BOTTLEUP, FA_BOTTLESWING, FA_BLOCK, FA_MEDKIT, FA_PAIN, FA_WIN };
static const char arm_front[9][65] = {
    "1111...." "1111...." "111....." "SSS....." "SSS....." "SSS....." "SSS....." "SSS.....",
    "........" "........" "........" "111....." "111....." "SSS....." ".SSS...." ".SSS....",
    "........" "........" "........" "........" "1111111." "SSSSSSSS" "........" "........",
    "SSS....." "SSS....." "111....." "111....." "111....." "........" "........" "........",
    "........" "........" "........" "111....." "1111...." "SSS....." "SSS....." ".SSS....",
    "SSS....." "SSS....." "SSS....." "SSS....." "SSS....." "111....." "111....." "111.....",
    "........" "1111...." "111....." "SSS....." "SSS....." ".SS....." "........" "........",
    "........" "SSS....." "SSS....." "111....." "1111...." "........" "........" "........",
    "SSS....." "SSS....." "SSS....." "111....." "111....." "111....." "........" "........"
};

enum { LG_IDLE, LG_WALKA, LG_WALKB, LG_LUNGE, LG_CROUCH, LG_KNEEL };
static const char legs_art[6][73] = {
    ".333....333." ".333....333." ".333....333." ".333....333." ".555....555." ".555....555.",
    ".333....333." ".333....333." "333......333" "333......333" "555......555" "555......555",
    "..333..333.." "..333..333.." "..333..333.." "...33.33...." "...55.55...." "...55.55....",
    ".333....333." ".333....333." "333......333" "333......333" "5555.....555" "5555.....555",
    "............" "............" "............" "..333..333.." ".3333..3333." ".5555..5555.",
    "............" "............" "..333..333.." "..333..333.." "..33333333.." "..55555555.."
};

enum { PR_NONE, PR_KNIFE, PR_BOTTLE, PR_BROKEN, PR_MEDKIT };
static const char prop_knife[]  = "kkkbbbbb" "kkkbbbbb";                       /* 8x2 */
static const char prop_bottle[] = ".ooo.." ".ooo.." ".ooo.." ".oooo." "oooooo"
                                  "oollo." "oollo." "oollo.";                  /* 6x8 */
static const char prop_broken[] = "o.oooo" "ooo.oo" "oooooo" "oollo." "oollo." "oollo."; /* 6x6 */
static const char prop_medkit[] = "wwwwww" "wrrrrw" "wrrwww" "wrrwww" "wwwwww";    /* 6x5 */

static const char dead_art[] =                                                  /* 24x6 */
    "........................" ".HHHH..................." ".SSSS111111111111111111."
    ".SSSS111111111111111111." ".3333333333333333333355." "...555555555555555555...";

/* frame definitions (anchors: legs 6,15 torso 6,8 head 6,0 back arm 2,8 front arm 14,8) */
enum { EM_NONE, EM_GRIT, EM_PAIN, EM_GRIN };
typedef struct {
    u8 legs, barm, farm, emo;
    s8 tx, ty, hx, hy, fax, fay, bax, bay;
    u8 prop; s8 px, py; u8 pfront;
} FDef;

static const FDef fdefs[NFRAMES] = {
    /* legs       ba       fa              emo      tx ty hx hy fax fay bax bay  prop       px py front */
    { LG_IDLE,   BA_DOWN, FA_DOWN,        EM_NONE,  0, 0, 0, 0,  0,  0,  0,  0, PR_NONE,    0, 0, 0 }, /* idle0 */
    { LG_IDLE,   BA_DOWN, FA_DOWN,        EM_NONE,  0, 1, 0, 1,  0,  1,  0,  1, PR_NONE,    0, 0, 0 }, /* idle1 */
    { LG_WALKA,  BA_DOWN, FA_DOWN,        EM_NONE,  0, 0, 0, 0,  0,  0,  0,  0, PR_NONE,    0, 0, 0 }, /* walk0 */
    { LG_WALKB,  BA_DOWN, FA_DOWN,        EM_NONE,  0, 1, 0, 1,  0,  1,  0,  1, PR_NONE,    0, 0, 0 }, /* walk1 */
    { LG_CROUCH, BA_DOWN, FA_DOWN,        EM_GRIT,  0, 4, 0, 4,  0,  4,  0,  4, PR_NONE,    0, 0, 0 }, /* crouch */
    { LG_IDLE,   BA_DOWN, FA_STABBACK,    EM_GRIT,  0, 0, 0, 0,  0,  0,  0,  0, PR_KNIFE,  11, 12, 1 }, /* stab0 */
    { LG_LUNGE,  BA_DOWN, FA_STABEXT,     EM_GRIT,  0, 0, 0, 0,  0,  0,  0,  0, PR_KNIFE,  16, 12, 1 }, /* stab1 */
    { LG_LUNGE,  BA_DOWN, FA_STABEXT,     EM_GRIT,  0, 0, 0, 0,  1,  0,  0,  0, PR_KNIFE,  17, 12, 1 }, /* stab2 */
    { LG_IDLE,   BA_DOWN, FA_BOTTLEUP,    EM_GRIT,  0, 0, 0, 0,  0, -7,  0,  0, PR_BOTTLE, 14, 0, 0 },  /* slash0 */
    { LG_LUNGE,  BA_PAIN, FA_BOTTLESWING, EM_GRIT,  0, 0, 0, 0,  0, -4,  0,  0, PR_BOTTLE, 16, 12, 1 }, /* slash1 */
    { LG_LUNGE,  BA_PAIN, FA_BOTTLESWING, EM_GRIT,  0, 0, 0, 0,  1, -3,  0,  0, PR_BROKEN, 17, 14, 1 }, /* slash2 */
    { LG_IDLE,   BA_DOWN, FA_BLOCK,       EM_NONE,  0, 0, 0, 0, -1, -7,  0,  0, PR_NONE,    0, 0, 0 }, /* block */
    { LG_IDLE,   BA_PAIN, FA_PAIN,        EM_PAIN, -1, 0,-1,-1,  0, -4,  0, -4, PR_NONE,    0, 0, 0 }, /* hit0 */
    { LG_IDLE,   BA_PAIN, FA_PAIN,        EM_PAIN, -1,-1,-1,-2,  0, -5,  0, -5, PR_NONE,    0, 0, 0 }, /* hit1 */
    { LG_IDLE,   BA_DOWN, FA_DOWN,        EM_PAIN,  0, 0, 0, 0,  0,  0,  0,  0, PR_NONE,    0, 0, 0 }, /* med0 */
    { LG_KNEEL,  BA_DOWN, FA_MEDKIT,      EM_PAIN,  0, 4, 0, 4,  0,  4,  0,  4, PR_MEDKIT, 14, 15, 1 }, /* med1 */
    { LG_CROUCH, BA_PAIN, FA_PAIN,        EM_PAIN,  0, 4, 0, 4,  0,  8,  0,  8, PR_NONE,    0, 0, 0 }, /* dead0 */
    { 255,       0,       0,              0,        0, 0, 0, 0,  0,  0,  0,  0, PR_NONE,    0, 0, 0 }, /* dead1 */
    { LG_IDLE,   BA_WIN,  FA_WIN,         EM_GRIN,  0, 0, 0, 0,  0, -8,  0, -8, PR_NONE,    0, 0, 0 }, /* win0 */
    { LG_WALKA,  BA_WIN,  FA_WIN,         EM_GRIN,  0, 1, 0, 1,  0, -8,  0, -8, PR_NONE,    0, 0, 0 }, /* win1 */
    { LG_IDLE,   BA_WIN,  FA_WIN,         EM_GRIN,  1, 0, 1, 0,  0, -8,  0, -8, PR_NONE,    0, 0, 0 }  /* win2 */
};

/* ---------------------------------------------------------------- composition */
static u8 cv[24 * 21];
static u8 rmap[128];
#define FRSZ 192          /* 3 layers, 64-byte aligned: the VIC shows them in place */
u8 spr_cache_char[2] = { 255, 255 };
static u8 *const cache_base[2] = { CACHE0, CACHE1 };   /* CACHE1 lives under the I/O area */
static const u16 cvrow16[21] = { 0, 24, 48, 72, 96, 120, 144, 168, 192, 216, 240, 264, 288, 312, 336, 360, 384, 408, 432, 456, 480 };

static void build_rmap(u8 c)
{
    const CharDef *d = &chars[c];
    memset(rmap, 0, sizeof(rmap));
    rmap['S'] = rmap['s'] = rmap['N'] = rmap['R'] = rmap['w'] = L_SKIN;
    rmap['E'] = rmap['D'] = rmap['M'] = rmap['W'] = rmap['k'] = rmap['r'] = rmap['l'] = L_BLACK;
    rmap['1'] = rmap['2'] = L_SHIRT;
    rmap['3'] = L_PANTS;
    rmap['b'] = rmap['o'] = rmap['G'] = L_GREY;
    rmap['H'] = rmap['C'] = rmap['B'] = d->hair;
    rmap['5'] = d->shoes;
}

static u8 blit_mirror;   /* 1 = draw the part mirrored horizontally */

static void blit(const char *p, u8 w, u8 h, s8 x, s8 y)
{
    u8 r, c, ch, v;
    s8 yy;
    u8 xx;
    u8 *d;
    for (r = 0; r < h; ++r, p += w) {
        yy = y + (s8)r;
        if (yy < 0 || yy >= 21) continue;
        d = cv + cvrow16[(u8)yy];
        for (c = 0; c < w; ++c) {
            ch = (u8)p[blit_mirror ? w - 1 - c : c];
            if (ch == '.') continue;
            xx = (u8)(x + (s8)c);
            if (xx >= 24) continue;
            v = rmap[ch & 127];
            if (v) d[xx] = v;
        }
    }
}

static const u8 mcbits[8] = { 0, 0, 0, 2, 1, 3, 0, 0 };

/* canvas -> 3 sprite layers (63 bytes each) */
static void pack(u8 *out)
{
    const u8 *s = cv;
    u8 i, b, a0, a1, a2, v, n, m;
    for (i = 0; i < 63; ++i) {
        a0 = a1 = a2 = 0;
        m = 0x80;
        for (b = 0; b < 4; ++b) {
            v = s[0];
            if (v == L_SKIN) a0 |= m; else if (v == L_SHIRT) a1 |= m;
            n = mcbits[v];
            m >>= 1;
            v = s[1];
            if (v == L_SKIN) a0 |= m; else if (v == L_SHIRT) a1 |= m;
            if (!n) n = mcbits[v];
            m >>= 1;
            a2 = (a2 << 2) | n;
            s += 2;
        }
        out[i] = a0;
        out[64 + i] = a1;
        out[128 + i] = a2;
    }
}

static void prop_blit(u8 prop, s8 x, s8 y)
{
    switch (prop) {
    case PR_KNIFE:  blit(prop_knife, 8, 2, x, y); break;
    case PR_BOTTLE: blit(prop_bottle, 6, 8, x, y); break;
    case PR_BROKEN: blit(prop_broken, 6, 6, x, y); break;
    case PR_MEDKIT: blit(prop_medkit, 6, 5, x, y); break;
    }
}

static void compose(u8 c, u8 f)
{
    const FDef *d = &fdefs[f];
    const CharDef *cd = &chars[c];
    s8 hx, hy;
    memset(cv, 0, sizeof(cv));
    if (d->legs == 255) {                 /* lying dead body */
        blit(dead_art, 24, 6, 0, 15);
        return;
    }
    /* victory poses: arms swapped left <-> right (each side gets the other
     * arm's art mirrored) and raised beside the head, not over the face:
     * hands at columns 3-5 and 18-20, the face sits on 7-16 */
    if (d->barm == BA_WIN) {
        blit_mirror = 1;
        blit(arm_front[d->farm], 8, 8, -2 + d->bax, 8 + d->bay);
        blit_mirror = 0;
    } else
        blit(arm_back[d->barm], 8, 8, 2 + d->bax, 8 + d->bay);
    blit(legs_art[d->legs], 12, 6, 6, 15);
    blit(torso_art[cd->torso], 12, 7, 6 + d->tx, 8 + d->ty);
    if (d->prop && !d->pfront) prop_blit(d->prop, d->px, d->py);
    hx = 6 + d->hx;
    hy = d->hy;
    /* the head art looks the wrong way for a right-facing body: mirror it
     * (and the eye/mouth overlays with it) so the face turns to the foe */
    blit_mirror = 1;
    blit(head_art[cd->head], 12, 8, hx, hy);
    switch (d->emo) {
    case EM_GRIT:
        blit(ov_eyegrit, 12, 1, hx, hy + (s8)cd->eyeRow);
        blit(ov_mgrit, 12, 1, hx, hy + (s8)cd->mouthRow);
        break;
    case EM_PAIN:
        blit(ov_eyepain, 12, 1, hx, hy + (s8)cd->eyeRow);
        blit(ov_mopen, 12, 1, hx, hy + (s8)cd->mouthRow);
        break;
    case EM_GRIN:
        blit(ov_mgrin, 12, 1, hx, hy + (s8)cd->mouthRow);
        break;
    }
    blit_mirror = 0;
    if (d->barm == BA_WIN) {
        blit_mirror = 1;
        blit(arm_back[d->barm], 8, 8, 17 + d->fax, 8 + d->fay);
        blit_mirror = 0;
    } else
        blit(arm_front[d->farm], 8, 8, 14 + d->fax, 8 + d->fay);
    if (d->prop && d->pfront) prop_blit(d->prop, d->px, d->py);
}

static u8 shown_fr[2] = { 255, 255 };

#define IO_OFF() POKE(1, 0x34)
#define IO_ON()  POKE(1, 0x35)

void spr_build_one(u8 slot, u8 c, u8 f)
{
    spr_cache_char[slot] = 255;
    shown_fr[slot] = 255;
    build_rmap(c);
    compose(c, f);
    IO_OFF();
    pack(cache_base[slot] + (u16)f * FRSZ);
    IO_ON();
}

void spr_build_all(u8 slot, u8 c)
{
    u8 f;
    u8 *p = cache_base[slot];
    if (spr_cache_char[slot] == c) return;
    shown_fr[slot] = 255;
    build_rmap(c);
    for (f = 0; f < NFRAMES; ++f, p += FRSZ) {
        compose(c, f);
        IO_OFF();
        pack(p);
        IO_ON();
    }
    spr_cache_char[slot] = c;
}

/* ---------------------------------------------------------------- hardware */
/* Right-facing frames are shown straight from the cache (both caches are
 * inside VIC bank 3); left-facing ones are mirrored into a double-buffered
 * pair of blocks per fighter. spr_vsync (sys.s) commits pointers/positions. */
u8 spr_vis[2], spr_next[2], spr_nbuf[2], spr_dbuf[2] = { 255, 255 };
u8 spr_hw[2], spr_y[2], spr_flash[2], spr_col1[2], spr_col2[2];
s16 spr_x[2];
u8 *spr_src, *spr_dst;
void spr_flipcopy(void);
static u8 shown_flip[2];
static const u8 cache_ptr[2] = { (0xE000 - 0xC000) / 64, (0xD000 - 0xC000) / 64 };

void spr_init(void)
{
    u16 i;
    u8 v, r, j;
    for (i = 0; i < 256; ++i) {
        v = (u8)i;
        r = 0;
        for (j = 0; j < 8; ++j) { r = (r << 1) | (v & 1); v >>= 1; }
        REVTAB[i] = r;
        v = (u8)i;
        REVMC[i] = (v >> 6) | ((v >> 2) & 0x0C) | ((v << 2) & 0x30) | (v << 6);
    }
    memset(SPRBUF, 0, 12 * 64);
    POKE(VIC_SPR_EXPX, 0x3F);
    POKE(VIC_SPR_EXPY, 0x3F);
    POKE(VIC_SPR_MC, 0x24);          /* sprites 2 and 5 = multicolour layers */
    POKE(VIC_SPR_MC0, C_GRAY);
    POKE(VIC_SPR_MC1, C_BLACK);
    POKE(0xD01B, 0);                  /* sprites in front of the PETSCII */
    POKE(VIC_SPR_EN, 0);
}

void spr_set(u8 slot, u8 fr, u8 flip)
{
    u8 b;
    if (shown_fr[slot] == fr && shown_flip[slot] == flip) return;
    shown_fr[slot] = fr;
    shown_flip[slot] = flip;
    if (!flip) {
        spr_next[slot] = cache_ptr[slot] + fr * 3;
        spr_nbuf[slot] = 255;
        return;
    }
    b = spr_dbuf[slot] == 0 ? 1 : 0;           /* the buffer not on screen */
    spr_src = cache_base[slot] + (u16)fr * FRSZ;
    spr_dst = SPRBUF + (u16)(slot * 6 + b * 3) * 64;
    IO_OFF();
    spr_flipcopy();
    IO_ON();
    spr_next[slot] = SPRPTR0 + slot * 6 + b * 3;
    spr_nbuf[slot] = b;
}

void spr_place(u8 slot, u8 hw, s16 x, u8 y, u8 flash)
{
    spr_hw[slot] = hw;
    spr_x[slot] = x;
    spr_y[slot] = y;
    spr_flash[slot] = flash;
    spr_vis[slot] = 1;
}

void spr_hide(u8 slot) { spr_vis[slot] = 0; }

void spr_all_off(void)
{
    spr_vis[0] = spr_vis[1] = 0;
    POKE(VIC_SPR_EN, 0);
}

void spr_colors(u8 slot, u8 c)
{
    spr_col1[slot] = chars[c].shirt;
    spr_col2[slot] = chars[c].pants;
}
