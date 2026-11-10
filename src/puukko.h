/* PUUKKO – A Very Finnish Fight, Commodore 64 edition (cc65). */
#ifndef PUUKKO_H
#define PUUKKO_H

#include "charmap.h"
#include "gen_data.h"

typedef unsigned char u8;
typedef signed char s8;
typedef unsigned int u16;
typedef signed int s16;

/* ---------------------------------------------------------------- hardware */
#define POKE(a, v) (*(volatile u8 *)(a) = (v))
#define PEEK(a)    (*(volatile u8 *)(a))

/* memory map (see puukko.cfg) */
#define BG_COL    ((u8 *)0x0400)   /* backing copy of colour RAM          */
#define SCREEN    ((u8 *)0xC000)
#define SPRBUF    ((u8 *)0xC400)   /* 12 sprite blocks: 2 fighters x 3 x 2 */
#define SPRPTR0   16               /* ($C400 - $C000) / 64 */
#define CHARSET   ((u8 *)0xC800)
#define SPRPTR    ((u8 *)0xC3F8)
#define COLRAM    ((u8 *)0xD800)
#define CACHE1    ((u8 *)0xD000)   /* RAM under I/O: access with $01 = $34 */
#define CACHE0    ((u8 *)0xE000)   /* RAM under the (banked out) KERNAL    */
#define REVTAB    ((u8 *)0xF000)
#define REVMC     ((u8 *)0xF100)
#define BG_SCR    ((u8 *)0xF200)   /* backing copy of the screen          */

#define VIC_SPR_X(n)  (0xD000 + (n) * 2)
#define VIC_SPR_Y(n)  (0xD001 + (n) * 2)
#define VIC_SPR_XMSB  0xD010
#define VIC_SPR_EN    0xD015
#define VIC_SPR_EXPY  0xD017
#define VIC_SPR_MC    0xD01C
#define VIC_SPR_EXPX  0xD01D
#define VIC_SPR_MC0   0xD025
#define VIC_SPR_MC1   0xD026
#define VIC_SPR_COL(n) (0xD027 + (n))
#define VIC_BORDER    0xD020
#define VIC_BG        0xD021

/* C64 colours */
enum { C_BLACK, C_WHITE, C_RED, C_CYAN, C_PURPLE, C_GREEN, C_BLUE, C_YELLOW,
       C_ORANGE, C_BROWN, C_LRED, C_DGRAY, C_GRAY, C_LGREEN, C_LBLUE, C_LGRAY };

/* ---------------------------------------------------------------- sys.s */
extern volatile u8 frame_cnt;
extern u8 is_pal;
extern u8 split_n;
extern u8 split_line[8];
extern u8 split_col[8];
extern u8 top_bg;
extern volatile u8 digi_on;
extern u8 snd_vol;
extern u8 music_enabled, sfx_enabled;
void sys_init(void);
void relocate_hi(void);
void __fastcall__ music_play(u8 track);
void music_stop(void);
void __fastcall__ sfx_play(u8 id);
void __fastcall__ digi_play(u8 id);

#ifdef PROF
void __fastcall__ prof_mark(u8 slot);
void prof_init(void);
#define PT(n) prof_mark(n)
#else
#define PT(n)
#endif

/* ---------------------------------------------------------------- gfx.c */
extern const u16 rowoff[25];
#define bg_scr BG_SCR
#define bg_col BG_COL
void gfx_init(void);
void cls(void);
void put(u8 x, u8 y, u8 ch, u8 col);
void fill(u8 x, u8 y, u8 w, u8 h, u8 ch, u8 col);
u8 scode(char c);
void text(u8 x, u8 y, const char *s, u8 col);
void textc(u8 y, const char *s, u8 col);           /* centred on column 20 */
void textcx(u8 cx, u8 y, const char *s, u8 col);   /* centred on column cx */
void textr(u8 x, u8 y, const char *s, u8 col);     /* right aligned, ends at x */
void textrev(u8 x, u8 y, const char *s, u8 col);   /* reversed (filled) text */
void textn(u8 x, u8 y, const char *s, u8 n, u8 col);
void blocktext(u8 x, u8 y, const char *s, u8 col);
u8 blockwidth(const char *s);
void frame_box(u8 x, u8 y, u8 w, u8 h, u8 col);
void bg_save(void);
void bg_restore(u8 x, u8 y, u8 w);
void bg_restore_rows(u8 y0, u8 y1);
void bg_put(u8 x, u8 y, u8 ch, u8 col);            /* both screen and backing copy */
void splits_off(u8 bg);
void splits_add(u8 row, u8 col);
void wait_frame(void);
void screen_off(void);
void screen_on(void);
u8 rnd(void);
u8 __fastcall__ rndn(u8 n);
void hexnum(u8 x, u8 y, u16 v, u8 col);
void num(u8 x, u8 y, u8 v, u8 col);

/* ---------------------------------------------------------------- stages.c */
#define NSTAGES 4
extern const char *const stage_names[NSTAGES];
void stage_draw(u8 id);

/* ---------------------------------------------------------------- spr.c */
enum { FR_IDLE0, FR_IDLE1, FR_WALK0, FR_WALK1, FR_CROUCH, FR_STAB0, FR_STAB1, FR_STAB2,
       FR_SLASH0, FR_SLASH1, FR_SLASH2, FR_BLOCK, FR_HIT0, FR_HIT1, FR_MED0, FR_MED1,
       FR_DEAD0, FR_DEAD1, FR_WIN0, FR_WIN1, FR_WIN2, NFRAMES };
void spr_init(void);
void spr_build_all(u8 slot, u8 ch);                 /* compose all frames into cache */
void spr_build_one(u8 slot, u8 ch, u8 fr);          /* compose one frame into cache[fr] */
void spr_set(u8 slot, u8 fr, u8 flip);               /* request frame on HW buffers */
void spr_place(u8 slot, u8 hw, s16 x, u8 y, u8 flash); /* x = centre px, y = top px */
void spr_hide(u8 slot);
void spr_all_off(void);
void spr_vsync(void);
void spr_colors(u8 slot, u8 ch);                                /* commit pending buffers */
extern u8 spr_cache_char[2];

/* ---------------------------------------------------------------- chars */
typedef struct {
    const char *name;
    const char *bio;
    u8 hp;
    u16 spd256, stabDmg100, slashDmg100, stabSpd256, slashSpd256;
    u8 sway, stumble1000, medkits, miss100, slashBleed;
    u8 head, torso, eyeRow, mouthRow;
    u8 shirt, pants;
    u8 hair, shoes;            /* sprite role codes */
} CharDef;
extern const CharDef chars[6];
#define NCHARS 6

/* ---------------------------------------------------------------- input */
#define I_LEFT   0x01
#define I_RIGHT  0x02
#define I_DOWN   0x04
#define I_BLOCK  0x08
#define I_STAB   0x10
#define I_SLASH  0x20
#define I_MEDKIT 0x40
#define I_UP     0x80          /* raw up (menus) */
extern u8 in_p[2];             /* fighter input per player */
extern u8 in_held[2], in_press[2];  /* raw joystick-like bits: 1 up 2 down 4 left 8 right 16 fire */
extern u8 key_confirm, key_back, key_pause, key_o, key_t, key_c, key_any;
void input_read(void);

/* ---------------------------------------------------------------- game */
typedef struct {
    u8 mode;          /* 1 = vs CPU, 2 = two players */
    u8 ch[2];
    u8 stage;
    u8 diff;          /* 0..2 */
    u8 hardcore;
} Sel;
extern Sel sel;
extern u8 winner, loser;      /* fighter index, 255 = none */
extern u8 game_state;
enum { GS_TITLE, GS_MODE, GS_DIFF, GS_CHARSEL, GS_STAGESEL, GS_VS, GS_FIGHT, GS_VICTORY, GS_OPTIONS };
extern u8 prev_state;
void goto_state(u8 s);

/* fight.c */
void fight_start(void);
u8 fight_frame(void);           /* returns 1 when the fight is over (victory/draw handled) */
void victory_enter(void);
void victory_frame(void);
#define FP 5                      /* fighter x fixed point: 1/32 px */

/* curses */
extern const char *const curse_txt[14];
extern const u8 curse_digi[14];

#endif
