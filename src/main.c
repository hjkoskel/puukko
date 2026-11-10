/* PUUKKO C64 – boot, input, menus and the main loop. */
#include <string.h>
#include "puukko.h"

Sel sel = { 1, { 0, 5 }, 0, 1, 0 };
u8 game_state, prev_state;

/* ---------------------------------------------------------------- input */
u8 in_p[2], in_held[2], in_press[2];
u8 key_confirm, key_back, key_pause, key_o, key_t, key_c, key_any;
extern u8 kb[8], kbp[8], joy1, joy2;     /* sys.s: matrix scan */
u8 kbd_scan(void);
static u8 raw_old[2];
#ifdef PROF
u8 dbg_joy, dbg_key;
#endif

#define K(r, b) (((r) << 3) | (b))
#define KEY_A K(1,2)
#define KEY_D K(2,2)
#define KEY_W K(1,1)
#define KEY_S K(1,5)
#define KEY_Z K(1,4)
#define KEY_X K(2,7)
#define KEY_SPACE K(7,4)
#define KEY_J K(4,2)
#define KEY_L K(5,2)
#define KEY_I K(4,1)
#define KEY_K K(4,5)
#define KEY_N K(4,7)
#define KEY_M K(4,4)
#define KEY_RETURN K(0,1)
#define KEY_P K(5,1)
#define KEY_O K(4,6)
#define KEY_T K(2,6)
#define KEY_C K(2,4)
#define KEY_Q K(7,6)
#define KEY_STOP K(7,7)
#define KEY_LARROW K(7,1)

static const u8 bitv[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
#define kd(k) (kb[(k) >> 3] & bitv[(k) & 7])
#define kp(k) (kbp[(k) >> 3] & bitv[(k) & 7])

void input_read(void)
{
    u8 any, raw, f, i, j1, j2;
    any = kbd_scan();
    j1 = joy1;
    j2 = joy2;
#ifdef PROF
    /* test hook: the monitor pokes dbg_joy (port-2 bits) and dbg_key (K(r,b)+1) */
    j2 |= dbg_joy;
    if (dbg_key) {
        kb[(dbg_key - 1) >> 3] |= bitv[(dbg_key - 1) & 7];
        kbp[(dbg_key - 1) >> 3] |= bitv[(dbg_key - 1) & 7];
        dbg_key = 0;
    }
    any |= j2;
#endif

    for (i = 0; i < 2; ++i) {
        if (i == 0) {
            raw = j2 | (sel.mode == 1 ? j1 : 0);
            if (kd(KEY_W)) raw |= 1;
            if (kd(KEY_S)) raw |= 2;
            if (kd(KEY_A)) raw |= 4;
            if (kd(KEY_D)) raw |= 8;
            if (kd(KEY_Z)) raw |= 16;
        } else {
            raw = sel.mode == 2 ? j1 : 0;
            if (kd(KEY_I)) raw |= 1;
            if (kd(KEY_K)) raw |= 2;
            if (kd(KEY_J)) raw |= 4;
            if (kd(KEY_L)) raw |= 8;
            if (kd(KEY_N)) raw |= 16;
        }
        in_held[i] = raw;
        in_press[i] = raw & ~raw_old[i];
        raw_old[i] = raw;
        f = 0;
        if (raw & 4) f |= I_LEFT;
        if (raw & 8) f |= I_RIGHT;
        if (raw & 2) f |= I_DOWN;
        if (raw & 1) f |= I_BLOCK | I_UP;
        if (in_press[i] & 16) {
            if (raw & 2) f |= I_SLASH;          /* fire + down = bottle */
            else if (raw & 1) f |= I_MEDKIT;    /* fire + up   = rag    */
            else f |= I_STAB;
        }
        if (i == 0) {
            if (kp(KEY_X)) f |= I_SLASH;
            if (kp(KEY_SPACE)) f |= I_MEDKIT;
        } else {
            if (kp(KEY_M)) f |= I_SLASH;
            if (kp(KEY_RETURN)) f |= I_MEDKIT;
        }
        in_p[i] = f;
        any |= raw;
    }
    key_confirm = ((in_press[0] | in_press[1]) & 16) || kp(KEY_SPACE) || kp(KEY_RETURN);
    key_back = kp(KEY_Q) || kp(KEY_LARROW);
    key_pause = kp(KEY_P) || kp(KEY_STOP);
    key_o = kp(KEY_O);
    key_t = kp(KEY_T);
    key_c = kp(KEY_C);
    key_any = any;
}

/* menu helpers: up/down edges from either player */
static u8 m_up(void)   { return (in_press[0] | in_press[1]) & 1; }
static u8 m_down(void) { return (in_press[0] | in_press[1]) & 2; }
static u8 m_left(void) { return (in_press[0] | in_press[1]) & 4; }
static u8 m_right(void){ return (in_press[0] | in_press[1]) & 8; }

/* ---------------------------------------------------------------- title */
#define NDRIPS 8
static u8 dr_x[NDRIPS], dr_y[NDRIPS], dr_len[NDRIPS], dr_on[NDRIPS];
static u8 title_music;
static u8 st_t;
u16 idle_t;                 /* frames without input on the title: attract mode */
#define ATTRACT_FRAMES 1000

static void title_enter(void)
{
    u8 i;
    spr_all_off();
    screen_off();
    stage_draw(0);
    splits_off(C_BLACK);
    splits_add(23, C_DGRAY);
    blocktext(3, 0, "PUUKKO", C_RED);
    frame_box(2, 13, 36, 10, C_WHITE);
    textc(14, "A VERY FINNISH FIGHT", C_GRAY);
    bg_save();
    textr(39, 23, "O = OPTIOT", C_GRAY);
    screen_on();
    for (i = 0; i < NDRIPS; ++i) dr_on[i] = 0;
    st_t = 0;
    idle_t = 0;
    if (sel.mode == 0) sel.mode = 1;
    if (!title_music) { music_play(MUS_TITLE); title_music = 1; }
}

static void title_frame(void)
{
    u8 i, k;
    ++st_t;
    /* blood dripping from the logo */
    if (rnd() < 26) {
        for (i = 0; i < NDRIPS; ++i) if (!dr_on[i]) {
            dr_on[i] = 1; dr_x[i] = 3 + rndn(34); dr_y[i] = 7; dr_len[i] = 1;
            break;
        }
    }
    for (i = 0; i < NDRIPS; ++i) {
        if (!dr_on[i]) continue;
        if (rnd() < 100) {
            bg_restore(dr_x[i], dr_y[i], 1);
            ++dr_y[i];
        }
        if (rnd() < 77 && dr_len[i] < 3) ++dr_len[i];
        if (dr_y[i] + dr_len[i] > 13) {
            for (k = 0; k < dr_len[i]; ++k) bg_restore(dr_x[i], dr_y[i] + k, 1);
            dr_on[i] = 0;
            continue;
        }
        for (k = 0; k < dr_len[i]; ++k)
            put(dr_x[i], dr_y[i] + k, k == dr_len[i] - 1 ? 81 : 93, k == dr_len[i] - 1 ? C_LRED : C_RED);
    }
    if ((st_t & 15) == 0) {
        if (st_t & 16) textc(16, "PAINA NAPPIA TAI SPACE", C_WHITE);
        else bg_restore(3, 16, 34);
    }
    if ((st_t & 63) == 0) {
        if (st_t & 64) {
            textc(18, "VAROITUS: T[M[ PELI SIS[LT[[", C_YELLOW);
            textc(19, "V[KIVALTAA, VIINAA JA SUOMEA", C_YELLOW);
            textc(20, "WARNING: VIOLENCE, ALCOHOL ABUSE", C_YELLOW);
            textc(21, "AND FINNISH LANGUAGE", C_YELLOW);
        } else {
            bg_restore_rows(18, 21);
        }
    }
    if (key_any) idle_t = 0;
    else if (++idle_t >= ATTRACT_FRAMES) {
        /* attract mode: two random drunks, CPU vs CPU */
        sel.mode = 0;
        sel.ch[0] = rndn(NCHARS);
        do sel.ch[1] = rndn(NCHARS); while (sel.ch[1] == sel.ch[0]);
        sel.stage = rndn(NSTAGES);
        goto_state(GS_VS);
        return;
    }
    if (key_confirm) {
        sfx_play(SFX_SELECT);
        digi_play(DIGI_SAATANA);
        goto_state(GS_MODE);
    } else if (key_o) {
        sfx_play(SFX_BLIP);
        goto_state(GS_OPTIONS);
    }
}

/* ---------------------------------------------------------------- simple menus */
static u8 menu_idx;

static void plain_screen(void)
{
    spr_all_off();
    cls();
    splits_off(C_BLACK);
    POKE(VIC_BORDER, C_BLACK);
}

static void draw_choice(u8 row, const char *s, u8 selected)
{
    u8 x = 20 - ((u8)strlen(s) >> 1);
    fill(0, row, 40, 1, 32, C_WHITE);
    if (selected) put(x - 2, row, '>', C_ORANGE);
    text(x, row, s, selected ? C_WHITE : C_GRAY);
}

static void mode_draw(void)
{
    draw_choice(7, "1 PELAAJA", menu_idx == 0);
    draw_choice(9, "2 PELAAJAA", menu_idx == 1);
}

static void mode_enter(void)
{
    plain_screen();
    textc(3, "MONTA PELAAJAA?", C_WHITE);
    textc(12, "JOYSTICK: VAS/OIK = LIIKU", C_GRAY);
    textc(13, "ALAS = KYYKKY   YL]S = TORJU", C_GRAY);
    textc(14, "NAPPI = PUUKKO  NAPPI+ALAS = PULLO", C_GRAY);
    textc(15, "NAPPI+YL]S = ENSIAPURIEPU", C_GRAY);
    textc(17, "P1: PORTTI 2 TAI A D W S Z X SPACE", C_GRAY);
    textc(18, "P2: PORTTI 1 TAI J L I K N M RETURN", C_GRAY);
    textc(19, "P = TAUKO", C_GRAY);
    textc(22, "NAPPI / SPACE = VALITSE", C_ORANGE);
    menu_idx = sel.mode - 1;
    mode_draw();
}

static void mode_frame(void)
{
    if (m_up() && menu_idx) { menu_idx = 0; sfx_play(SFX_BLIP); mode_draw(); }
    if (m_down() && !menu_idx) { menu_idx = 1; sfx_play(SFX_BLIP); mode_draw(); }
    if (key_confirm) {
        sfx_play(SFX_SELECT);
        sel.mode = menu_idx + 1;
        sel.ch[0] = 0;
        sel.ch[1] = NCHARS - 1;
        goto_state(menu_idx == 0 ? GS_DIFF : GS_CHARSEL);
    } else if (key_back) goto_state(GS_TITLE);
}

static const char *const diff_names[3] = { "HELPPO", "NORMAALI", "VITTU" };
static const char *const diff_desc[3] = {
    "HIDAS JUOPPO, MUTTA LY] SILTI", "TASAPUOLINEN TAISTELU", "ARMOTON, NOPEA, EI ANNA ARMOA"
};

static void diff_draw(void)
{
    u8 i;
    for (i = 0; i < 3; ++i) {
        draw_choice(8 + i * 3, diff_names[i], menu_idx == i);
        fill(0, 9 + i * 3, 40, 1, 32, C_WHITE);
        textc(9 + i * 3, diff_desc[i], menu_idx == i ? C_YELLOW : C_DGRAY);
    }
}

static void diff_enter(void)
{
    plain_screen();
    textc(3, "VAIKEUS", C_WHITE);
    textc(22, "NAPPI = VALITSE   Q = TAKAISIN", C_GRAY);
    menu_idx = sel.diff;
    diff_draw();
}

static void diff_frame(void)
{
    if (m_up()) { menu_idx = (menu_idx + 2) % 3; sfx_play(SFX_BLIP); diff_draw(); }
    if (m_down()) { menu_idx = (menu_idx + 1) % 3; sfx_play(SFX_BLIP); diff_draw(); }
    if (key_confirm) { sel.diff = menu_idx; sfx_play(SFX_SELECT); goto_state(GS_CHARSEL); }
    else if (key_back) { sfx_play(SFX_BLIP); goto_state(GS_MODE); }
}

/* ---------------------------------------------------------------- character select */
static u8 cs_done[2], cs_cpuT, cs_nextT, cs_rep[2], cs_shown[2];
static const u8 cs_cx[3] = { 8, 20, 32 };

static void cs_names(void)
{
    u8 i, x, y, col, l;
    for (i = 0; i < NCHARS; ++i) {
        l = (u8)strlen(chars[i].name);
        x = cs_cx[i % 3] - (l >> 1);
        y = 4 + (i / 3) * 3;
        fill(x - 1, y, l + 2, 1, 32, C_WHITE);
        if (sel.ch[0] == i && sel.ch[1] == i) col = C_YELLOW;
        else if (sel.ch[0] == i) col = C_WHITE;
        else if (sel.ch[1] == i) col = C_ORANGE;
        else { text(x, y, chars[i].name, C_DGRAY); continue; }
        put(x - 1, y, 160, col);
        textrev(x, y, chars[i].name, col);
        put(x + l, y, 160, col);
    }
}

static void cs_info(void)
{
    u8 c = cs_done[0] ? sel.ch[1] : sel.ch[0];
    fill(0, 19, 40, 3, 32, C_WHITE);
    textcx(10, 19, chars[sel.ch[0]].name, C_WHITE);
    textcx(30, 19, chars[sel.ch[1]].name, C_ORANGE);
    textc(21, chars[c].bio, C_YELLOW);
}

static void cs_preview(u8 slot)
{
    if (cs_shown[slot] == sel.ch[slot]) return;
    cs_shown[slot] = sel.ch[slot];
    spr_build_one(slot, sel.ch[slot], FR_IDLE0);
    spr_build_one(slot, sel.ch[slot], FR_IDLE1);
    spr_colors(slot, sel.ch[slot]);
}

static void charsel_enter(void)
{
    plain_screen();
    textc(1, "VALITSE HAHMO", C_WHITE);
    textcx(10, 11, "P1", C_WHITE);
    textcx(30, 11, sel.mode == 1 ? "CPU" : "P2", C_ORANGE);
    textc(14, "VS", C_RED);
    if (sel.mode == 1) textc(23, "NAPPI = VALITSE   Q = TAKAISIN", C_GRAY);
    else textc(23, "P1: NAPPI/Z  P2: NAPPI/N  Q: TAKAISIN", C_GRAY);
    cs_done[0] = cs_done[1] = 0;
    cs_cpuT = 0;
    cs_nextT = 0;
    cs_rep[0] = cs_rep[1] = 0;
    cs_shown[0] = cs_shown[1] = 255;
    cs_names();
    cs_info();
    cs_preview(0);
    cs_preview(1);
}

static void cs_move(u8 p)
{
    s8 d = 0;
    u8 h = in_held[p];
    if (cs_rep[p]) { --cs_rep[p]; if (!(in_press[p] & 15)) return; }
    if (h & 4) d = -1;
    else if (h & 8) d = 1;
    else if (h & 1) d = -3;
    else if (h & 2) d = 3;
    if (!d) return;
    sel.ch[p] = (u8)((sel.ch[p] + d + NCHARS) % NCHARS);
    sfx_play(SFX_BLIP);
    cs_rep[p] = (d == 1 || d == -1) ? 9 : 14;
    cs_names();
    cs_info();
}

static void charsel_frame(void)
{
    u8 anim = (frame_cnt >> 5) & 1;
    if (!cs_done[0]) {
        cs_move(0);
        if (in_press[0] & 16) {
            cs_done[0] = 1;
            sfx_play(SFX_SELECT);
            if (sel.mode == 1) cs_cpuT = 40;
            cs_info();
        }
    }
    if (sel.mode == 1 && cs_done[0] && !cs_done[1]) {
        if (--cs_cpuT == 0) {
            do sel.ch[1] = rndn(NCHARS); while (sel.ch[1] == sel.ch[0]);
            cs_done[1] = 1;
            sfx_play(SFX_SELECT);
            cs_names();
            cs_info();
        }
    }
    if (sel.mode == 2 && !cs_done[1]) {
        cs_move(1);
        if (in_press[1] & 16) { cs_done[1] = 1; sfx_play(SFX_SELECT); }
    }
    cs_preview(0);
    cs_preview(1);
    spr_set(0, anim ? FR_IDLE1 : FR_IDLE0, 0);
    spr_set(1, anim ? FR_IDLE1 : FR_IDLE0, 1);
    spr_place(0, 0, 80, 50 + 104, 0);
    spr_place(1, 3, 240, 50 + 104, 0);
    if (cs_done[0] && cs_done[1]) {
        if (++cs_nextT > 30) goto_state(GS_STAGESEL);
    }
    if (key_back) { sfx_play(SFX_BLIP); goto_state(GS_MODE); }
}

/* ---------------------------------------------------------------- stage select */
static u8 ss_rep;

static void ss_draw(void)
{
    u8 i;
    stage_draw(sel.stage);
    frame_box(7, 1, 26, 9, C_WHITE);
    textc(2, "VALITSE PAIKKA", C_WHITE);
    for (i = 0; i < NSTAGES; ++i) {
        if (i == sel.stage) put(10, 3 + i, '>', C_ORANGE);
        text(12, 3 + i, stage_names[i], i == sel.stage ? C_WHITE : C_GRAY);
    }
    textc(8, "NAPPI = OK  Q = TAKAISIN", C_GRAY);
}

static void stagesel_enter(void)
{
    spr_all_off();
    ss_rep = 0;
    ss_draw();
}

static void stagesel_frame(void)
{
    u8 h = in_held[0] | in_held[1];
    if (ss_rep) --ss_rep;
    if (!ss_rep && (h & 15)) {
        sel.stage = (h & 5) ? (sel.stage + NSTAGES - 1) % NSTAGES : (sel.stage + 1) % NSTAGES;
        sfx_play(SFX_BLIP);
        ss_rep = 12;
        ss_draw();
    }
    if (key_confirm) { sfx_play(SFX_SELECT); goto_state(GS_VS); }
    else if (key_back) { sfx_play(SFX_BLIP); goto_state(GS_CHARSEL); }
}

/* ---------------------------------------------------------------- VS */
static u8 vs_start, vs_t;

static void vs_enter(void)
{
    u8 x;
    spr_all_off();
    screen_off();
    stage_draw(sel.stage);
    frame_box(1, 4, 38, 18, C_WHITE);
    x = (40 - blockwidth(chars[sel.ch[0]].name)) >> 1;
    blocktext(x, 5, chars[sel.ch[0]].name, C_WHITE);
    textc(13, "VS", C_ORANGE);
    x = (40 - blockwidth(chars[sel.ch[1]].name)) >> 1;
    blocktext(x, 14, chars[sel.ch[1]].name, C_WHITE);
    textc(23, "GET READY, YOU DRUNKEN BASTARDS", C_GRAY);
    screen_on();
    music_stop();
    title_music = 0;
    vs_start = frame_cnt;
    /* compose both fighters' animation sets (takes a moment) */
    spr_build_all(0, sel.ch[0]);
    spr_build_all(1, sel.ch[1]);
    vs_t = 0;
}

static void vs_frame(void)
{
    u8 t = frame_cnt - vs_start;
    if (t > 200) t = 200;
    if (vs_t < 90 && t >= 90) digi_play(DIGI_PERKELE);
    vs_t = t;
    if (t > 90) {
        if ((t >> 3) & 1) textc(22, "VALMISTAUTUKAA...", C_YELLOW);
        else fill(2, 22, 36, 1, 32, C_WHITE);
    }
    if (t >= 180) {
        fight_start();
        game_state = GS_FIGHT;
    }
}

/* ---------------------------------------------------------------- options */
static void opt_draw(void)
{
    u8 i, row;
    static const char *const lbl[4] = { "MUSIIKKI", "EFEKTIT", "VAIKEUS", "HARD CORE" };
    const char *val;
    for (i = 0; i < 4; ++i) {
        row = 12 + i * 2;
        fill(0, row, 40, 1, 32, C_WHITE);
        if (i == menu_idx) put(4, row, '>', C_ORANGE);
        text(6, row, lbl[i], i == menu_idx ? C_WHITE : C_GRAY);
        switch (i) {
        case 0: val = music_enabled ? "P[[LL[" : "POIS"; break;
        case 1: val = sfx_enabled ? "P[[LL[" : "POIS"; break;
        case 2: val = diff_names[sel.diff]; break;
        default: val = sel.hardcore ? "P[[LL[" : "POIS"; break;
        }
        textr(33, row, val, i == menu_idx ? C_YELLOW : C_GRAY);
    }
}

static void options_enter(void)
{
    plain_screen();
    blocktext(2, 2, "OPTIOT", C_WHITE);
    textc(21, "YL]S/ALAS = VALITSE  VAS/OIK = MUUTA", C_GRAY);
    textc(23, "NAPPI / SPACE = TAKAISIN", C_GRAY);
    menu_idx = 0;
    opt_draw();
}

static void options_frame(void)
{
    u8 ch = 0;
    if (m_up()) { menu_idx = (menu_idx + 3) & 3; ch = 1; }
    if (m_down()) { menu_idx = (menu_idx + 1) & 3; ch = 1; }
    if (m_left() || m_right()) {
        switch (menu_idx) {
        case 0:
            music_enabled ^= 1;
            if (!music_enabled) { music_stop(); title_music = 0; }
            break;
        case 1: sfx_enabled ^= 1; break;
        case 2: sel.diff = m_left() ? (sel.diff + 2) % 3 : (sel.diff + 1) % 3; break;
        case 3: sel.hardcore ^= 1; break;
        }
        ch = 1;
    }
    if (ch) { sfx_play(SFX_BLIP); opt_draw(); }
    if (key_confirm || key_back) { sfx_play(SFX_SELECT); goto_state(GS_TITLE); }
}

/* ---------------------------------------------------------------- flow */
void goto_state(u8 s)
{
    prev_state = game_state;
    game_state = s;
    switch (s) {
    case GS_TITLE:    title_enter(); break;
    case GS_MODE:     mode_enter(); break;
    case GS_DIFF:     diff_enter(); break;
    case GS_CHARSEL:  charsel_enter(); break;
    case GS_STAGESEL: stagesel_enter(); break;
    case GS_VS:       vs_enter(); break;
    case GS_VICTORY:  victory_enter(); break;
    case GS_OPTIONS:  options_enter(); break;
    }
}

u16 loop_cnt;          /* main loop iterations (vs frame_cnt: performance check) */

int main(void)
{
    __asm__("sei");
    POKE(1, 0x35);       /* BASIC + KERNAL out, I/O in */
    relocate_hi();       /* $C000 blob -> under the KERNAL, before the VIC uses $C000 */
    gfx_init();          /* banks KERNAL out ($01 = $35) */
    sys_init();          /* own IRQ/NMI vectors, raster IRQ, SID */
    spr_init();
    goto_state(GS_TITLE);
#ifdef PROF
    prof_init();
#endif
    for (;;) {
        PT(15);                 /* idle: waiting for the frame */
        wait_frame();
        ++loop_cnt;
        PT(14);                 /* (IRQ time lands here) */
        spr_vsync();
        PT(1);
        input_read();
        PT(0);
        switch (game_state) {
        case GS_TITLE:    title_frame(); break;
        case GS_MODE:     mode_frame(); break;
        case GS_DIFF:     diff_frame(); break;
        case GS_CHARSEL:  charsel_frame(); break;
        case GS_STAGESEL: stagesel_frame(); break;
        case GS_VS:       vs_frame(); break;
        case GS_FIGHT:    fight_frame(); break;
        case GS_VICTORY:  victory_frame(); break;
        case GS_OPTIONS:  options_frame(); break;
        }
    }
    return 0;
}
