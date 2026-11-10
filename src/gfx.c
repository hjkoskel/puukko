/* PUUKKO C64 – screen layer: PETSCII text, block letters, boxes,
 * backing copy of the stage (for restoring cells), $D021 raster splits. */
#include <string.h>
#include "puukko.h"

#define R(n) ((n) * 40)
const u16 rowoff[25] = {
    R(0), R(1), R(2), R(3), R(4), R(5), R(6), R(7), R(8), R(9), R(10), R(11), R(12),
    R(13), R(14), R(15), R(16), R(17), R(18), R(19), R(20), R(21), R(22), R(23), R(24)
};


/* Finnish letters patched into the ROM charset copy:
 * screen code 27 '[' -> Ä, 28 '£' -> Å, 29 ']' -> Ö (C64 font style) */
static const u8 glyph_ae[8] = { 0x66, 0x00, 0x3C, 0x66, 0x7E, 0x66, 0x66, 0x00 };
static const u8 glyph_aa[8] = { 0x18, 0x00, 0x3C, 0x66, 0x7E, 0x66, 0x66, 0x00 };
static const u8 glyph_oe[8] = { 0x66, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00 };

static void patch(u8 code, const u8 *g)
{
    u8 i;
    for (i = 0; i < 8; ++i) {
        CHARSET[code * 8 + i] = g[i];
        CHARSET[(code + 128) * 8 + i] = (u8)~g[i];
    }
}

void gfx_init(void)
{
    /* charset ROM -> RAM (IRQs are off) */
    POKE(1, 0x31);                /* char ROM at $D000, RAM elsewhere */
    memcpy(CHARSET, (void *)0xD000, 2048);
    POKE(1, 0x35);
    patch(27, glyph_ae);
    patch(28, glyph_aa);
    patch(29, glyph_oe);

    /* VIC bank 3 ($C000) */
    POKE(0xDD02, PEEK(0xDD02) | 3);
    POKE(0xDD00, PEEK(0xDD00) & 0xFC);
    POKE(0xD018, 0x02);          /* screen $C000 (0 << 4), charset $C800 (1 << 1) */
    POKE(0xD016, 0x08);
    POKE(VIC_BORDER, C_BLACK);
    POKE(VIC_BG, C_BLACK);
    cls();
}

void screen_off(void) { POKE(0xD011, PEEK(0xD011) & 0x6F); }
void screen_on(void)  { POKE(0xD011, (PEEK(0xD011) & 0x7F) | 0x10); }

void cls(void)
{
    memset(SCREEN, 32, 1000);
    memset(COLRAM, C_WHITE, 1000);
}

void put(u8 x, u8 y, u8 ch, u8 col)
{
    u16 o;
    if (x >= 40 || y >= 25) return;
    o = rowoff[y] + x;
    SCREEN[o] = ch;
    COLRAM[o] = col;
}

void bg_put(u8 x, u8 y, u8 ch, u8 col)
{
    u16 o;
    if (x >= 40 || y >= 25) return;
    o = rowoff[y] + x;
    SCREEN[o] = ch;
    COLRAM[o] = col;
    bg_scr[o] = ch;
    bg_col[o] = col;
}

void fill(u8 x, u8 y, u8 w, u8 h, u8 ch, u8 col)
{
    u8 *s, *c;
    u8 i;
    if (x >= 40) return;
    if (x + w > 40) w = 40 - x;
    while (h--) {
        if (y < 25) {
            s = SCREEN + rowoff[y] + x;
            c = COLRAM + rowoff[y] + x;
            for (i = 0; i < w; ++i) { s[i] = ch; c[i] = col; }
        }
        ++y;
    }
}

u8 scode(char c)
{
    u8 u = (u8)c;
    if (u >= 'a' && u <= 'z') return u - 96;
    if (u >= '@' && u <= '_') return u - 64;   /* '[' Ä, '\\' Å, ']' Ö */
    if (u == '|') return 93;
    return u;
}

void textn(u8 x, u8 y, const char *s, u8 n, u8 col)
{
    u8 *sp, *cp;
    if (y >= 25) return;
    sp = SCREEN + rowoff[y] + x;
    cp = COLRAM + rowoff[y] + x;
    while (*s && n && x < 40) {
        *sp++ = scode(*s++);
        *cp++ = col;
        ++x; --n;
    }
}

void text(u8 x, u8 y, const char *s, u8 col) { textn(x, y, s, 40, col); }

void textrev(u8 x, u8 y, const char *s, u8 col)
{
    u8 *sp, *cp;
    sp = SCREEN + rowoff[y] + x;
    cp = COLRAM + rowoff[y] + x;
    while (*s && x < 40) {
        *sp++ = scode(*s++) | 0x80;
        *cp++ = col;
        ++x;
    }
}

void textcx(u8 cx, u8 y, const char *s, u8 col)
{
    u8 l = (u8)strlen(s);
    s8 x = (s8)cx - (s8)(l >> 1);
    if (x < 0) x = 0;
    text((u8)x, y, s, col);
}

void textc(u8 y, const char *s, u8 col) { textcx(20, y, s, col); }

void textr(u8 x, u8 y, const char *s, u8 col)
{
    u8 l = (u8)strlen(s);
    text(x + 1 - l, y, s, col);
}

void num(u8 x, u8 y, u8 v, u8 col)
{
    char b[4];
    u8 i = 3;
    b[3] = 0;
    do { b[--i] = '0' + v % 10; v /= 10; } while (v && i);
    text(x, y, b + i, col);
}

void hexnum(u8 x, u8 y, u16 v, u8 col)
{
    static const char hx[] = "0123456789ABCDEF";
    put(x, y, scode(hx[v >> 12]), col);
    put(x + 1, y, scode(hx[(v >> 8) & 15]), col);
    put(x + 2, y, scode(hx[(v >> 4) & 15]), col);
    put(x + 3, y, scode(hx[v & 15]), col);
}

/* ---------------------------------------------------------- block letters */
static const char font_keys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.!?-:[]\\";
static const u8 font5[][7] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
  {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
  {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
  {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
  {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
  {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
  {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
  {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
  {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
  {0x00,0x00,0x00,0x00,0x00,0x04,0x04}, {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
  {0x0E,0x11,0x01,0x06,0x04,0x00,0x04}, {0x00,0x00,0x00,0x0E,0x00,0x00,0x00},
  {0x00,0x04,0x04,0x00,0x04,0x04,0x00}, {0x0A,0x00,0x0E,0x11,0x1F,0x11,0x11},
  {0x0A,0x00,0x0E,0x11,0x11,0x11,0x0E}, {0x04,0x0A,0x0E,0x13,0x1F,0x11,0x11}
};

u8 blockwidth(const char *s)
{
    u8 l = (u8)strlen(s);
    return l ? l * 6 - 1 : 0;
}

/* each font pixel = one solid PETSCII cell (screen code 160) */
void blocktext(u8 x, u8 y, const char *s, u8 col)
{
    const char *k;
    const u8 *g;
    u8 r, c, bits;
    for (; *s; ++s, x += 6) {
        if (*s == ' ') continue;
        for (k = font_keys; *k && *k != *s; ++k) ;
        if (!*k) continue;
        g = font5[k - font_keys];
        for (r = 0; r < 7; ++r) {
            bits = g[r];
            for (c = 0; c < 5; ++c)
                if (bits & (0x10 >> c)) put(x + c, y + r, 160, col);
        }
    }
}

/* PETSCII frame: corners + lines, interior cleared to spaces */
void frame_box(u8 x, u8 y, u8 w, u8 h, u8 col)
{
    fill(x, y, w, h, 32, col);
    fill(x + 1, y, w - 2, 1, 64, col);
    fill(x + 1, y + h - 1, w - 2, 1, 64, col);
    fill(x, y + 1, 1, h - 2, 93, col);
    fill(x + w - 1, y + 1, 1, h - 2, 93, col);
    put(x, y, 112, col);
    put(x + w - 1, y, 110, col);
    put(x, y + h - 1, 109, col);
    put(x + w - 1, y + h - 1, 125, col);
}

/* ---------------------------------------------------------- backing copy */
void bg_save(void)
{
    memcpy(bg_scr, SCREEN, 1000);
    memcpy(bg_col, COLRAM, 1000);
}

void bg_restore(u8 x, u8 y, u8 w)
{
    u16 o;
    if (y >= 25 || x >= 40) return;
    if (x + w > 40) w = 40 - x;
    o = rowoff[y] + x;
    memcpy(SCREEN + o, bg_scr + o, w);
    memcpy(COLRAM + o, bg_col + o, w);
}

void bg_restore_rows(u8 y0, u8 y1)
{
    u16 o = rowoff[y0];
    u16 n = rowoff[y1] + 40 - o;
    memcpy(SCREEN + o, bg_scr + o, n);
    memcpy(COLRAM + o, bg_col + o, n);
}

/* ---------------------------------------------------------- raster splits */
void splits_off(u8 bg)
{
    split_n = 0;
    top_bg = bg;
    POKE(VIC_BG, bg);
}

/* background colour changes to `col` from text row `row` down.
 * The write lands on the last pixel line of the row above, so at worst a
 * single line of the upper band takes the new colour early. */
void splits_add(u8 row, u8 col)
{
    u8 n = split_n;
    if (n >= 8) return;
    split_line[n] = 50 + row * 8;
    split_col[n] = col;
    split_n = n + 1;
}

void wait_frame(void)
{
    static u8 last;
    while (frame_cnt == last) ;
    last = frame_cnt;
}

/* rnd() / rndn() live in sys.s (16-bit xorshift, 8x8 multiply) */
