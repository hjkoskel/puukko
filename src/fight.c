/* PUUKKO C64 – the fight: fighter state machines, drunk AI, bleeding,
 * blood drops & pools (PETSCII cells), HUD, rag pickups, KO and victory.
 * Ported from fighter.js / ai.js / game.js / particles.js / ui.js. */
#include <string.h>
#include "puukko.h"

#define GY        160
#define GROUND_ROW 20
#define WALL_L    (10 << FP)
#define WALL_R    (310 << FP)
#define FPS       50

enum { ST_IDLE, ST_WALK, ST_CROUCH, ST_STAB, ST_SLASH, ST_BLOCK, ST_HITSTUN, ST_MEDKIT, ST_DEAD, ST_WIN };

/* attack tuning (frames, browser values) */
#define STAB_STARTUP 8
#define STAB_ACTIVE 6
#define STAB_RECOVER 13
#define SLASH_STARTUP 12
#define SLASH_ACTIVE 8
#define SLASH_RECOVER 16
#define HITSTUN 22
#define KNOCKBACK 11
#define INPUT_LAG 3
#define MEDKIT_TIME 100
#define MEDKIT_HEAL 12
#define MEDKIT_MAX 3
#define PICKUP_LIFE 700

const char *const curse_txt[14] = {
    "VITTU!", "PERKELE!", "AARGH!", "SAATANA!", "HOMO!", "AARGH!", "EIP[S!",
    "PERSE!", "AARGH!", "PASKA!", "VITTU!", "PILLU!", "VOI P[IV[!", "KYRP[!"
};
const u8 curse_digi[14] = {
    DIGI_VITTU, DIGI_PERKELE, DIGI_AARGH, DIGI_SAATANA, DIGI_HOMO, DIGI_AARGH, DIGI_EIPAS,
    DIGI_PERSE, DIGI_AARGH, DIGI_PASKA, DIGI_VITTU, DIGI_PILLU, DIGI_VOI_PAIVA, DIGI_KYRPA
};

static const char *const taunts[] = {
    "HAISTA PASKA, VELIPOIKA!",
    "KOE ITSESI ALKOHOLISSA. EI SIELL[K[[N AUTA.",
    "SOSIAALITY]NTEKIJ[T VOISIVAT OTTAA SINUT ESIMERKIKSI.",
    "PUUKKO EI VALITSE. SE VALITSI JUURI SINUT.",
    "L[[KE SINULLE: VETT[ JA ARKEA.",
    "KYLL[ SIN[KIN VIEL[ J[[T POHJALLE.",
    "T[ST[ SAAT LASKUN KOTIIN.",
    "H[PE[, H[PE[, H[PE[!",
    "SUIHKUUN, NUKKUMAAN JA AAMULLA T]IHIN.",
    "JUOT KUIN OLISI HUOMISTA. EI OLE.",
    "SINUN VERESI ON JO 40 PROSENTTISTA.",
    "VIINA OTTI SINUT, EI TOISINP[IN.",
    "SEURAAVA KIERROS ON SAIRAALASSA.",
    "MAKSASI ON JO J[TT[NYT IRTISANOMISEN.",
    "HUMPPAA SIIN[, KUN JALAT VIEL[ KANTAVAT.",
    "T[M[ EI OLE KUNNIAOTTELU. T[M[ ON SELVIYTYMISOTTELU.",
    "SINUN ROHKEUTESI L]YTYY PULLOSTA.",
    "HUOMENNA ET MUISTA T[T[. MIN[ MUISTAN.",
    "KAADUIT JO ENNEN LY]NTI[.",
    "SINUN PUOLUSTUKSESI ON KESKIOLUTTA.",
    "T[SS[ EI VOITETA. T[SS[ VAIN H[VIT[[N."
};
#define NTAUNTS (sizeof(taunts) / sizeof(taunts[0]))

/* ---------------------------------------------------------------- fighters */
/* Fighter data lives in fs[2]. The code works on two global structs:
 * C = the fighter being processed, O = the other one (cc65 compiles
 * fixed-address struct fields far tighter than indexed arrays). */
typedef struct {
    s16 xf, vx, hp, px;         /* px = xf >> FP, kept in sync */
    u8 state, t, animT, phase, flash;
    u8 bleedT, bleedInt, bleedTick, stumble;
    u8 pend, pendLeft, hasHit, missed, dead, facing;
    u8 maxhp, medkits, stepT, isCpu, spd, bubT, ch;
    u8 sway, stumble1000, miss100, slashBleed, dmgStab, dmgSlash;
    u8 aStart[2], aAct[2], aRec[2];
    const char *bub;
    u8 ai_state, ai_timer, ai_hold, ai_drink, ai_intent, ai_intentT, ai_lostRight;
} Fighter;

Fighter fc, fo;                 /* exported for the swap routine in sys.s */
#define C fc
#define O fo
static u8 ci;                   /* which fighter (0/1) is in C */
void __fastcall__ fswap(u8 size);

static void sel_f(u8 i)
{
    if (ci != i) { fswap(sizeof(Fighter)); ci = i; }
}

#define CX C.px
#define OX O.px

u8 winner = 255, loser = 255;
static u8 fightT, koT, timeUp, in_ko, paused;
static u8 tsec, tsub;
static u8 bleed_tick_len, bleed_dur;
static u8 digi_q = 255, digi_qT;
static u8 in_cur;            /* input of the fighter being updated */
static u8 last_fc;

static void say(const char *s, u8 t) { C.bub = s; C.bubT = t; }

static void queue_digi(u8 id, u8 delay)
{
    digi_q = id;
    digi_qT = delay;
}

static void set_state(u8 s)
{
    C.state = s;
    C.t = 0;
    if (s == ST_STAB || s == ST_SLASH) C.hasHit = 0;
}

static u8 calc_frames(u8 base, u16 spd, u8 min)
{
    u16 v = (((u16)base << 8) + (spd >> 1)) / spd;
    return v < min ? min : (u8)v;
}

static void fighter_init(u8 i, s16 x, u8 facing, u8 cpu)
{
    const CharDef *d = &chars[sel.ch[i]];
    sel_f(i);
    memset(&C, 0, sizeof(C));
    C.ch = sel.ch[i];
    C.xf = x << FP;
    C.px = x;
    C.facing = facing;
    C.isCpu = cpu;
    C.hp = C.maxhp = d->hp;
    C.medkits = sel.hardcore ? 1 : d->medkits;
    C.phase = rnd() & 15;
    C.stepT = 12;
    C.ai_timer = 20;
    C.sway = d->sway;
    C.stumble1000 = d->stumble1000;
    C.miss100 = d->miss100;
    C.slashBleed = d->slashBleed;
    C.dmgStab = (u8)d->stabDmg100;
    C.dmgSlash = (u8)d->slashDmg100;
    C.aStart[0] = calc_frames(STAB_STARTUP, d->stabSpd256, 3);
    C.aAct[0] = STAB_ACTIVE;
    C.aRec[0] = calc_frames(STAB_RECOVER, d->stabSpd256, 4);
    C.aStart[1] = calc_frames(SLASH_STARTUP, d->slashSpd256, 3);
    C.aAct[1] = SLASH_ACTIVE;
    C.aRec[1] = calc_frames(SLASH_RECOVER, d->slashSpd256, 4);
    /* walk speed: 145/256 px/frame * spd256/256, in 1/32 px */
    C.spd = (u8)((145u * d->spd256) >> 11);
}

/* ---------------------------------------------------------------- blood */
#define MAXDROPS 24
#define MAXPOOLS 16
static u8 d_x[MAXDROPS], d_y[MAXDROPS], d_t[MAXDROPS], d_c[MAXDROPS];
static s8 d_vx[MAXDROPS];
static u8 d_on[MAXDROPS];
static u8 p_x[MAXPOOLS], p_w[MAXPOOLS], p_h[MAXPOOLS], npools;
static const u8 splat_col[4] = { C_RED, C_LRED, C_RED, C_ORANGE };

static void blood_reset(void)
{
    memset(d_on, 0, sizeof(d_on));
    npools = 0;
}

static void drop_add(u8 x, u8 y, s8 vx, u8 t, u8 c)
{
    u8 i;
    if (x >= 40 || y >= GROUND_ROW) return;
    for (i = 0; i < MAXDROPS; ++i) {
        if (!d_on[i]) {
            d_on[i] = 1;
            d_x[i] = x; d_y[i] = y; d_vx[i] = vx; d_t[i] = t; d_c[i] = c;
            put(x, y, 81, c);
            return;
        }
    }
}

static void spray(s16 px, s16 py, u8 n, s8 dir)
{
    u8 cx, cy;
    s8 vx;
    if (px < 0) px = 0;
    cx = (u8)((px + 4) >> 3);
    cy = (u8)((py + 4) >> 3);
    while (n--) {
        vx = (rnd() < 192) ? dir : -dir;
        drop_add(cx + (rnd() % 3) - 1, cy - (rnd() & 1), vx, 0, splat_col[rnd() & 3]);
    }
}

static void drip(s16 px, s16 py)
{
    if (px < 0) px = 0;
    drop_add((u8)((px + 4 + (rnd() & 1)) >> 3), (u8)((py + 4) >> 3), 0, rnd() % 3,
             (rnd() & 1) ? C_RED : C_LRED);
}

static void pool_draw(u8 i)
{
    u8 k, x;
    for (k = 0; k < p_w[i]; ++k) {
        x = p_x[i] + k;
        if (x >= 40) continue;
        bg_put(x, GROUND_ROW, 0x62, C_RED);
        if (p_h[i] >= 2) bg_put(x, GROUND_ROW + 1, 160, C_RED);
        if (p_h[i] >= 3) bg_put(x, GROUND_ROW + 2, 0x62 | 0x80, C_RED);
    }
}

static void pool_add(u8 x)
{
    u8 i;
    if (x >= 40) return;
    for (i = 0; i < npools; ++i) {
        if (x + 1 >= p_x[i] && x <= p_x[i] + p_w[i]) {
            if (x < p_x[i]) { --p_x[i]; ++p_w[i]; }
            else if (x > p_x[i] + p_w[i] - 1) ++p_w[i];
            if (rnd() < 51 && p_h[i] < 3) ++p_h[i];
            pool_draw(i);
            return;
        }
    }
    if (npools < MAXPOOLS) {
        p_x[npools] = x;
        p_w[npools] = 1 + (rnd() & 1);
        p_h[npools] = 1;
        pool_draw(npools);
        ++npools;
    }
}

static void cell_restore(u8 x, u8 y)
{
    u16 o = rowoff[y] + x;
    SCREEN[o] = bg_scr[o];
    COLRAM[o] = bg_col[o];
}

static void blood_update(void)
{
    u8 i, x, y;
    u16 o;
    for (i = 0; i < MAXDROPS; ++i) {
        if (!d_on[i]) continue;
        if (++d_t[i] < 3) continue;
        d_t[i] = 0;
        x = d_x[i];
        y = d_y[i];
        o = rowoff[y] + x;
        SCREEN[o] = bg_scr[o];
        COLRAM[o] = bg_col[o];
        ++y;
        x += d_vx[i];
        if (x >= 40) { d_on[i] = 0; continue; }
        if (y >= GROUND_ROW) {
            d_on[i] = 0;
            pool_add(x);
            continue;
        }
        d_x[i] = x;
        d_y[i] = y;
        o += 40 + d_vx[i];
        SCREEN[o] = 81;
        COLRAM[o] = d_c[i];
    }
}

/* ---------------------------------------------------------------- combat */
static u8 damage(u8 amount, u8 fromBleed)
{
    if (C.dead) return 0;
    C.hp -= amount;
    if (!fromBleed) C.flash = 4;
    if (C.hp <= 0) {
        C.hp = 0;
        C.dead = 1;
        set_state(ST_DEAD);
        C.bleedT = 0;
        return 1;
    }
    return 0;
}

static void add_bleed(u8 intensity)
{
    C.bleedT = bleed_dur;
    if (intensity > 3) intensity = 3;
    if (intensity > C.bleedInt) C.bleedInt = intensity;
}

/* C = defender, O = attacker */
static void on_hit(u8 slash, u8 trade)
{
    s8 dir = CX >= OX ? 1 : -1;
    u8 dmg, bleed, k, facingAtk;
    s16 cx = CX - dir * 6;

    if (!slash) { dmg = 6 + rnd() % 5; bleed = 2 + (rnd() & 1); }
    else        { dmg = 4 + rnd() % 5; bleed = 1 + (rnd() & 1); }
    dmg = (u8)(((u16)dmg * (slash ? O.dmgSlash : O.dmgStab) + 50) / 100);

    facingAtk = (OX >= CX) == (C.facing == 0);
    if (C.state == ST_BLOCK && facingAtk) {
        dmg = (u8)(((u16)dmg * 25 + 50) >> 7);
        bleed = (bleed + 1) >> 1;
        sfx_play(SFX_BLOCK);
        spray(cx, GY - 30, 3, -dir);
        C.vx += dir * 2;
        if (C.t < 10) C.t = 10;
    } else {
        if (C.state == ST_CROUCH || C.state == ST_MEDKIT) dmg = (dmg * 7) >> 3;
        sfx_play(SFX_HIT);
        spray(cx, GY - 30, 6 + (rnd() & 7), -dir);
        if (trade) {
            C.vx += dir * 2;
        } else {
            set_state(ST_HITSTUN);
            C.vx = dir * (KNOCKBACK << FP);
        }
        k = rnd() % 14;
        say(curse_txt[k], 70);
        queue_digi(curse_digi[k], 5);   /* let the thud land, then the voice */
        if (slash) bleed += O.slashBleed;
    }
    add_bleed(bleed);
    damage(dmg ? dmg : 1, 0);
}

static void update_bleed(void)
{
    if (!C.bleedT) return;
    --C.bleedT;
    if (++C.bleedTick >= bleed_tick_len) {
        C.bleedTick = 0;
        damage(C.bleedInt, 1);
        drip(CX - (C.facing ? -4 : 4), GY - 34 + (rnd() % 10));
        if (rnd() < 90) sfx_play(SFX_DRIP);
    } else if (rnd() < 15) {
        drip(CX + (rnd() & 7) - 4, GY - 30);
    }
    if (!C.bleedT) C.bleedInt = 0;
}

static s16 clampx(s16 x)
{
    if (x < WALL_L) return WALL_L;
    if (x > WALL_R) return WALL_R;
    return x;
}

static void physics(void)
{
    s16 dx, adx, push;
    if (C.vx) {
        C.xf += C.vx;
        if (C.xf < WALL_L || C.xf > WALL_R) { C.xf = clampx(C.xf); C.vx = 0; }
    }
    if (!O.dead && !C.dead) {
        dx = O.xf - C.xf;
        adx = dx < 0 ? -dx : dx;
        if (adx < (20 << FP)) {
            push = ((20 << FP) - adx) >> 2;
            if (dx < 0) push = -push;
            C.xf = clampx(C.xf - push);
            O.xf = clampx(O.xf + push);
            O.px = O.xf >> FP;
        }
    }
    C.px = C.xf >> FP;
}

static void start_medkit(void)
{
    --C.medkits;
    sfx_play(SFX_BLIP);
    set_state(ST_MEDKIT);
}

static void update_move(void)
{
    u8 in = in_cur;
    s16 target = 0, dv, av;

    if (!O.dead) C.facing = O.xf >= C.xf ? 0 : 1;

    if ((in & I_MEDKIT) && C.medkits) { start_medkit(); return; }
    if ((in & I_STAB) && !C.pend) { C.pend = ST_STAB; C.pendLeft = INPUT_LAG; return; }
    if ((in & I_SLASH) && !C.pend) { C.pend = ST_SLASH; C.pendLeft = INPUT_LAG; return; }
    if (in & I_BLOCK) { set_state(ST_BLOCK); return; }
    if (in & I_DOWN) { set_state(ST_CROUCH); return; }

    if (in & I_LEFT) target = -(s16)C.spd;
    if (in & I_RIGHT) target = C.spd;
    dv = ((target - C.vx) * 15) >> 8;
    if (!dv && target != C.vx) dv = target > C.vx ? 1 : -1;
    C.vx += dv;

    if (C.stumble) {
        if (--C.stumble == 0) C.vx += ((s16)rnd() - 128) >> 3;
    } else if (target && rndn(250) < C.stumble1000) {
        C.stumble = 7 + rnd() % 5;
    }

    av = C.vx < 0 ? -C.vx : C.vx;
    if (av > 4) { if (C.state != ST_WALK) set_state(ST_WALK); }
    else if (C.state == ST_WALK) set_state(ST_IDLE);
}

static void fighter_update(u8 i, u8 in)
{
    u8 k, type;
    s16 av;

    sel_f(i);
    in_cur = in;
    ++C.animT;
    if (C.flash) --C.flash;
    if (C.bubT && --C.bubT == 0) C.bub = 0;
    if (C.t != 255) ++C.t;

    if (C.dead) {
        if (C.t == 28) sfx_play(SFX_KO);
        goto out;
    }
    if (C.state == ST_WIN) goto out;

    if (C.pend && --C.pendLeft == 0) {
        type = C.pend;
        C.pend = 0;
        if (C.state == ST_IDLE || C.state == ST_WALK || C.state == ST_CROUCH) {
            set_state(type);
            sfx_play(type == ST_STAB ? SFX_STAB : SFX_SLASH);
            C.missed = C.miss100 && rndn(100) < C.miss100;
        }
    }

    switch (C.state) {
    case ST_IDLE:
    case ST_WALK:
        update_move();
        break;
    case ST_CROUCH:
        if ((in & (I_STAB | I_SLASH)) && !C.pend) {   /* fire while crouching = bottle */
            C.pend = ST_SLASH;
            C.pendLeft = INPUT_LAG;
        }
        if (!(in & I_DOWN)) set_state(ST_IDLE);
        C.vx = (C.vx * 13) >> 4;
        break;
    case ST_BLOCK:
        if ((in & I_MEDKIT) && C.medkits) { start_medkit(); break; }
        if (!(in & I_BLOCK)) set_state(ST_IDLE);
        C.vx = (C.vx * 13) >> 4;
        break;
    case ST_STAB:
    case ST_SLASH:
        k = C.state == ST_SLASH;
        if (k && C.t == C.aStart[1]) sfx_play(SFX_SMASH);
        if (C.t >= C.aStart[k] + C.aAct[k] + C.aRec[k]) set_state(ST_IDLE);
        break;
    case ST_HITSTUN:
        C.vx = (C.vx * 7) >> 3;
        if (C.t >= HITSTUN) set_state(ST_IDLE);
        break;
    case ST_MEDKIT:
        if (C.t >= MEDKIT_TIME) {
            C.hp += MEDKIT_HEAL;
            if (C.hp > C.maxhp) C.hp = C.maxhp;
            C.bleedT = 0;
            C.bleedInt = 0;
            sfx_play(SFX_MEDKIT);
            say("PARANNUS!", 80);
            set_state(ST_IDLE);
        }
        break;
    }

    physics();
    update_bleed();

    av = C.vx < 0 ? -C.vx : C.vx;
    if (C.state == ST_WALK && av > 3) {
        if (++C.stepT >= 24) { C.stepT = 0; sfx_play(SFX_STEP); }
    } else C.stepT = 12;
out:
    ;
}

/* attack hitbox vs body (vertical ranges always overlap in this game).
 * Tests C->O and O->C on the structs directly; swaps only on a hit. */
static u8 hit_k, hit_trade;

#define HIT_TEST(A, B, R)                                                    \
    R = 0;                                                                   \
    if (!A.dead && !A.hasHit && !A.missed && !B.dead &&                      \
        (A.state == ST_STAB || A.state == ST_SLASH)) {                       \
        k = A.state == ST_SLASH;                                             \
        if (A.t >= A.aStart[k] && A.t < A.aStart[k] + A.aAct[k]) {           \
            if (!k) { ax0 = 8; ax1 = 26; } else { ax0 = 4; ax1 = 28; }       \
            if (A.facing == 0) { ax0 = A.px + ax0; ax1 = A.px + ax1; }       \
            else { t = A.px - ax1; ax1 = A.px - ax0; ax0 = t; }              \
            if (ax0 < B.px + 9 && ax1 > B.px - 9) {                          \
                A.hasHit = 1;                                                \
                hit_k = k;                                                   \
                hit_trade = B.state == ST_STAB || B.state == ST_SLASH;       \
                R = 1;                                                       \
            }                                                                \
        }                                                                    \
    }

static void check_hits(void)
{
    u8 k, hc, ho, kc, tc;
    s16 ax0, ax1, t;
    HIT_TEST(C, O, hc);       /* C hits O */
    kc = hit_k; tc = hit_trade;
    HIT_TEST(O, C, ho);       /* O hits C */
    if (ho) on_hit(hit_k, hit_trade);          /* C is the defender */
    if (hc) {
        sel_f(ci ^ 1);                          /* now C = old O = defender */
        on_hit(kc, tc);
    }
}

/* ---------------------------------------------------------------- drunk AI */
/* runs with C = the CPU fighter, O = its victim */
typedef struct { u8 r0, r1, atk, slashFar, block, drink, lost, retreat; } AIDiff;
/* react (min, max frames), then % chances: close attack, mid-range bottle,
 * block, drink, wander off, retreat. Tuned well above the browser version:
 * even HELPPO keeps swinging. */
static const AIDiff aidif[3] = {
    { 16, 28, 48, 30, 22, 8, 6, 16 },   /* HELPPO   */
    { 10, 18, 66, 45, 40, 4, 3,  8 },   /* NORMAALI */
    {  4, 10, 82, 60, 60, 1, 1,  3 }    /* VITTU    */
};
enum { AI_IDLE, AI_APPROACH, AI_RETREAT, AI_LOST, AI_CROUCH };
#define ai_state C.ai_state
#define ai_timer C.ai_timer
#define ai_hold C.ai_hold
#define ai_drink C.ai_drink
#define ai_intent C.ai_intent
#define ai_intentT C.ai_intentT
#define ai_lostRight C.ai_lostRight
static AIDiff D;

static void ai_reset(void)
{
    memcpy(&D, &aidif[sel.mode ? sel.diff : 1], sizeof(D));
}

static u8 roll(void) { return rndn(100); }

static void ai_set(u8 bit)
{
    ai_state = AI_IDLE;
    ai_intent = bit;
    ai_intentT = 30;
}

static u8 ai_dist(void)
{
    s16 d = OX - CX;
    if (d < 0) d = -d;
    return d > 255 ? 255 : (u8)d;
}

static void ai_decide(void)
{
    u8 dist = ai_dist();
    u8 r = roll();
    ai_timer = D.r0 + rndn(D.r1 - D.r0 + 1);

    if (C.bleedT && C.medkits && C.hp * 2 < C.maxhp && roll() < 30) {
        ai_set(I_MEDKIT);
        return;
    }
    if ((O.state == ST_STAB || O.state == ST_SLASH) && dist < 46 && roll() < D.block) {
        ai_hold = 15 + rndn(25);
        ai_state = AI_IDLE;
        return;
    }
    if (r < D.drink) {
        ai_drink = 50 + rndn(60);
        sfx_play(SFX_DRINK);
        say("GLUCK...", 50);
        return;
    }
    if (dist > 44) {
        if (roll() < D.lost) {
            ai_state = AI_LOST;
            ai_timer = 25 + rndn(25);
            ai_lostRight = OX < CX;     /* walk away from the foe */
        } else ai_state = AI_APPROACH;
    } else if (dist > 30) {
        if (r < D.slashFar) ai_set(I_SLASH);
        else if (r < D.slashFar + 30) ai_state = AI_APPROACH;
        else if (r < D.slashFar + 30 + D.retreat) ai_state = AI_RETREAT;
        else ai_state = AI_CROUCH;
    } else {
        if (r < D.atk) ai_set(I_STAB);
        else if (r < D.atk + 18) ai_set(I_SLASH);
        else if (r < D.atk + 30) { ai_hold = 20; ai_state = AI_IDLE; }
        else if (r < D.atk + 30 + D.retreat) ai_state = AI_RETREAT;
        else ai_state = AI_CROUCH;
    }
}

static u8 ai_input(void)
{
    u8 o = 0, dist, right;
    if (C.dead || O.dead) { ai_intent = 0; goto out; }
    if (ai_intent) {
        --ai_intentT;
        o |= ai_intent;
        if (C.state == ST_STAB || C.state == ST_SLASH || C.state == ST_MEDKIT || !ai_intentT)
            ai_intent = 0;
    }
    if (ai_drink) {
        if (--ai_drink == 0) say("JUHUUU...", 70);
        goto out;
    }
    if (ai_hold) {
        --ai_hold;
        o |= I_BLOCK;
        if (!ai_hold) ai_decide();
        goto out;
    }
    if (--ai_timer == 0) ai_decide();

    dist = ai_dist();
    right = OX >= CX;
    switch (ai_state) {
    case AI_APPROACH:
        if (dist > 26) o |= right ? I_RIGHT : I_LEFT;
        else if (ai_timer > 2) ai_timer = 2;    /* in range: strike right away */
        break;
    case AI_RETREAT:  if (dist < 60) o |= right ? I_LEFT : I_RIGHT; break;
    case AI_LOST:     o |= ai_lostRight ? I_RIGHT : I_LEFT; break;
    case AI_CROUCH:   o |= I_DOWN; break;
    }
out:
    return o;
}

/* ---------------------------------------------------------------- HUD */
static u8 hud_bar[2], hud_bleed[2], hud_med[2], hud_sec, hud_nl[2];
static s16 hud_hp[2];
static const u8 digits35[10][5] = {   /* 3x5 digits, bits 2..0 */
    {7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,3,1,7}, {5,5,7,1,1},
    {7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,2,2}, {7,5,7,5,7}, {7,5,7,1,7}
};

static void draw_timer(u8 v)
{
    u8 d[2], n, k, r, c, x0, col;
    bg_restore_rows(0, 4);
    col = v <= 10 ? C_RED : C_WHITE;
    if (v >= 10) { d[0] = v / 10; d[1] = v % 10; n = 2; }
    else { d[0] = v; n = 1; }
    x0 = (40 - (n * 4 - 1)) >> 1;
    for (k = 0; k < n; ++k)
        for (r = 0; r < 5; ++r)
            for (c = 0; c < 3; ++c)
                if (digits35[d[k]][r] & (4 >> c)) put(x0 + k * 4 + c, r, 160, col);
}

/* health bar of C: 14 cells, P1 fills from the left, P2 from the right */
static void draw_bar(u8 t)
{
    u8 filled, half, col, k, ch, cc;
    u8 *sp, *cp;
    filled = t >> 1;
    half = t & 1;
    if (C.flash) col = C_WHITE;
    else if (C.hp * 2 > C.maxhp) col = C_GREEN;
    else if (C.hp * 4 > C.maxhp) col = C_YELLOW;
    else col = C_RED;
    sp = SCREEN + 5 * 40 + (ci ? 25 : 1);
    cp = COLRAM + 5 * 40 + (ci ? 25 : 1);
    if (ci) filled = 14 - filled;
    for (k = 0; k < 14; ++k) {
        ch = 160; cc = C_DGRAY;
        if (!ci) {
            if (k < filled) cc = col;
            else if (k == filled && half) { ch = 97; cc = col; }
        } else {
            if (k >= filled) cc = col;
            else if (k + 1 == filled && half) { ch = 97 | 0x80; cc = col; }
        }
        sp[k] = ch;
        cp[k] = cc;
    }
}

static void hud_full(void)
{
    static char n1[12], n2[12];
    strcpy(n1, sel.mode == 0 ? "CPU " : "P1 ");
    strcat(n1, chars[sel.ch[0]].name);
    strcpy(n2, sel.mode != 2 ? "CPU " : "P2 ");
    strcat(n2, chars[sel.ch[1]].name);
    text(1, 6, n1, C_WHITE);
    textr(38, 6, n2, C_WHITE);
    hud_nl[0] = (u8)strlen(n1);
    hud_nl[1] = (u8)strlen(n2);
    if (sel.hardcore) textc(7, "HARD CORE", C_RED);
    hud_bar[0] = hud_bar[1] = 255;
    hud_hp[0] = hud_hp[1] = -1;
    hud_bleed[0] = hud_bleed[1] = 255;
    hud_med[0] = hud_med[1] = 255;
    hud_sec = 255;
}

/* HUD parts of C */
static void hud_fighter(void)
{
    u8 v, k, x;
    k = C.flash ? 1 : 0;
    if (C.hp != hud_hp[ci] || k != hud_bar[ci]) {
        hud_hp[ci] = C.hp;
        hud_bar[ci] = k;
        draw_bar((u8)(((u16)C.hp * 28) / C.maxhp));
    }
    v = C.bleedT && ((frame_cnt >> 2) & 1);
    if (v != hud_bleed[ci]) {
        hud_bleed[ci] = v;
        x = ci ? 38 - hud_nl[1] - 1 : 1 + hud_nl[0] + 1;
        put(x, 6, v ? 81 : 32, C_RED);
    }
    if (C.medkits != hud_med[ci]) {
        hud_med[ci] = C.medkits;
        for (k = 0; k < MEDKIT_MAX; ++k) {
            x = ci ? 38 - k * 2 : 1 + k * 2;
            if (k < C.medkits) put(x, 7, '+' | 0x80, C_WHITE);
            else cell_restore(x, 7);
        }
    }
}

/* ---------------------------------------------------------------- bubbles */
static u8 bub_x[2], bub_w[2];
static const char *bub_s[2];

static void bubble(void)
{
    u8 w, row = 12 + ci;
    s8 cx;
    if (!C.bub || in_ko) {
        if (bub_s[ci]) { bg_restore(bub_x[ci], row, bub_w[ci]); bub_s[ci] = 0; }
        return;
    }
    w = (u8)strlen(C.bub) + 2;
    cx = (s8)(CX >> 3) - (s8)(w >> 1);
    if (cx < 0) cx = 0;
    if (cx > 40 - w) cx = 40 - w;
    if (bub_s[ci] == C.bub && bub_x[ci] == (u8)cx) return;
    if (bub_s[ci]) bg_restore(bub_x[ci], row, bub_w[ci]);
    put(cx, row, 160, C_WHITE);
    textrev(cx + 1, row, C.bub, C_WHITE);
    put(cx + w - 1, row, 160, C_WHITE);
    bub_s[ci] = C.bub;
    bub_x[ci] = cx;
    bub_w[ci] = w;
}

/* ---------------------------------------------------------------- pickup */
static u8 pk_on, pk_x, pk_vis;
static u16 pk_t, pk_wait;

static u16 pickup_interval(void)
{
    return sel.hardcore ? 1400 + ((u16)rnd() << 2) : 900 + ((u16)rnd() << 1) + (rnd() >> 2);
}

static void pk_show(u8 on)
{
    if (on == pk_vis) return;
    pk_vis = on;
    if (on) {
        put(pk_x, 19, '+' | 0x80, C_WHITE);
        put(pk_x + 1, 19, 0x62, C_WHITE);
    } else {
        cell_restore(pk_x, 19);
        cell_restore(pk_x + 1, 19);
    }
}

static void pickup_gone(void)
{
    pk_show(0);
    pk_on = 0;
    pk_wait = pickup_interval();
}

/* rag pickup; C is tested for grabbing it, then O */
static u8 pk_near(s16 x)
{
    s16 dx = x - ((pk_x << 3) + 8);
    if (dx < 0) dx = -dx;
    return dx < 14;
}

static u8 pk_grab(void)
{
    if (C.dead || !pk_near(CX)) return 0;
    if (C.medkits < MEDKIT_MAX) {
        ++C.medkits;
        say("POIMI +1", 60);
    }
    sfx_play(SFX_SELECT);
    pickup_gone();
    return 1;
}

static void pickup_update(void)
{
    if (!pk_on) {
        if (--pk_wait == 0) {
            pk_on = 1;
            pk_t = PICKUP_LIFE;
            pk_x = 5 + rndn(30);
            pk_vis = 0;
        }
        return;
    }
    if (--pk_t == 0) { pickup_gone(); return; }
    pk_show(pk_t > 120 || ((frame_cnt >> 2) & 1));
    if (pk_near(O.px) && !O.dead) sel_f(ci ^ 1);
    pk_grab();
}

/* ---------------------------------------------------------------- sprites */
/* 16-step sine sway, pre-rounded for amplitudes 1, 2 and 4 px */
static const s8 sway_t[3][16] = {
    { 0, 1, 1, 1, 1, 1, 1, 0, 0, -1, -1, -1, -1, -1, 0, 1 },
    { 0, 1, 2, 2, 2, 2, 1, 0, -1, -2, -2, -2, -2, -1, 0, 1 },
    { 0, 2, 4, 4, 4, 4, 2, 0, -2, -4, -4, -4, -4, -2, 0, 2 }
};

static s8 sway_off(void)
{
    u8 a;
    if (C.dead || C.state == ST_HITSTUN) return 0;
    a = C.sway - 1;              /* 1 -> 0, 2 -> 1 */
    if (C.stumble) ++a;
    return sway_t[a][((C.animT >> 1) + C.phase) & 15];
}

static u8 cur_frame(void)
{
    u8 t = C.t, k, s;
    switch (C.state) {
    case ST_IDLE:    return (C.animT >> 5) & 1 ? FR_IDLE1 : FR_IDLE0;
    case ST_WALK:    return (C.animT >> 3) & 1 ? FR_WALK1 : FR_WALK0;
    case ST_CROUCH:  return FR_CROUCH;
    case ST_BLOCK:   return FR_BLOCK;
    case ST_HITSTUN: return t < 11 ? FR_HIT0 : FR_HIT1;
    case ST_MEDKIT:  return t < 25 ? FR_MED0 : t < 90 ? FR_MED1 : FR_IDLE0;
    case ST_STAB:
        s = C.aStart[0];
        if (t < s) return FR_STAB0;
        s += C.aAct[0];
        if (t < s) return FR_STAB1;
        if (t < s + 5) return FR_STAB2;
        return FR_IDLE0;
    case ST_SLASH:
        s = C.aStart[1];
        if (t < s) return FR_SLASH0;
        k = C.aAct[1];
        if (t < s + (k >> 1)) return FR_SLASH1;
        if (t < s + k) return FR_SLASH2;
        return FR_IDLE0;
    case ST_DEAD:    return t < 30 ? FR_DEAD0 : FR_DEAD1;
    case ST_WIN:     k = (C.animT / 18) & 3; return FR_WIN0 + (k == 3 ? 1 : k);
    }
    return FR_IDLE0;
}

/* sprite + HUD + speech bubble of C */
static void visuals(u8 hud)
{
    spr_set(ci, cur_frame(), C.facing);
    spr_place(ci, C.xf < O.xf ? 3 : 0, CX, (u8)(GY - 41 + 50 + sway_off()), C.flash);
    if (hud) {
        bubble();
        hud_fighter();
    }
}

static void visuals_both(u8 hud)
{
    visuals(hud);
    sel_f(ci ^ 1);
    visuals(hud);
}

/* ---------------------------------------------------------------- flow */
static void fight_screen(void)
{
    screen_off();
    stage_draw(sel.stage);
    bg_save();
    hud_full();
    bub_s[0] = bub_s[1] = 0;
    screen_on();
}

void fight_start(void)
{
    fighter_init(0, 100, 0, sel.mode == 0);
    fighter_init(1, 220, 1, sel.mode != 2);
    bleed_dur = sel.hardcore ? 225 : 180;
    bleed_tick_len = sel.hardcore ? 32 : 45;
    blood_reset();
    ai_reset();
    tsec = 99; tsub = FPS;
    fightT = 0; koT = 0; timeUp = 0; in_ko = 0; paused = 0;
    winner = loser = 255;
    pk_on = 0; pk_vis = 0;
    pk_wait = pickup_interval();
    digi_q = 255;
    last_fc = frame_cnt;
    spr_colors(0, sel.ch[0]);
    spr_colors(1, sel.ch[1]);
    fight_screen();
    music_play(MUS_FIGHT);
}

static void start_ko(u8 w, u8 l, u8 tu)
{
    winner = w;
    loser = l;
    timeUp = tu;
    koT = 0;
    in_ko = 1;
    music_stop();
    C.bub = O.bub = 0;
}

/* big K.O. / AIKA! banner: colour picked per stage so it never melts into
 * the scenery behind it (orange kiosk + fence, orange/red/brown yard, red
 * ALKO sign), plus a black drop shadow for contrast on any background */
static const u8 ko_col[NSTAGES] = { C_WHITE, C_WHITE, C_ORANGE, C_YELLOW };
static void big_msg(const char *s)
{
    u8 x = (40 - blockwidth(s)) >> 1;
    blocktext(x + 1, 9, s, C_BLACK);
    blocktext(x, 8, s, ko_col[sel.stage]);
}

static void pause_draw(u8 on)
{
    u8 i;
    if (on) {
        frame_box(6, 6, 28, 14, C_WHITE);
        blocktext(8, 8, "TAUKO", C_WHITE);
        textc(16, "P = JATKA", C_GRAY);
        textc(17, "T = ALKURUUTU", C_GRAY);
    } else {
        bg_restore_rows(6, 19);
        hud_full();
        bub_s[0] = bub_s[1] = 0;
        for (i = 0; i < MAXDROPS; ++i) if (d_on[i]) put(d_x[i], d_y[i], 81, d_c[i]);
    }
}

static void update_one(u8 i, u8 live)
{
    u8 in = 0;
    sel_f(i);
    if (live) in = C.isCpu ? ai_input() : in_p[i];
    fighter_update(i, in);
}

/* one logic step (50 per second); vis = also draw the first fighter
 * between the two updates (saves a fighter swap). Returns 1 if the fight
 * screen was left. */
static u8 tick(u8 vis)
{
    u8 first;
    s16 x;
    if (digi_q != 255 && --digi_qT == 0) { digi_play(digi_q); digi_q = 255; }

    /* whoever is loaded in C goes first: update (+ draw) it, swap once,
     * then the other one (the pair alternates each frame) */
    first = ci;
    if (!in_ko) {
        ++fightT;
        if (fightT == 255) fightT = 200;
        PT(2);
        update_one(first, fightT >= 90);
        PT(3);
        if (vis) visuals(1);
        PT(4);
        update_one(first ^ 1, fightT >= 90);
        PT(5);
        if (fightT >= 90) {
            check_hits();
            pickup_update();
            PT(6);
            if (C.dead || O.dead) {
                if (C.dead && O.dead) start_ko(255, 255, 0);
                else if (C.dead) start_ko(ci ^ 1, ci, 0);
                else start_ko(ci, ci ^ 1, 0);
            } else if (--tsub == 0) {
                tsub = FPS;
                if (--tsec == 0) {
                    if (C.hp == O.hp) start_ko(255, 255, 1);
                    else if (C.hp > O.hp) start_ko(ci, ci ^ 1, 1);
                    else start_ko(ci ^ 1, ci, 1);
                }
            }
        }
        /* intro texts */
        if (fightT == 1) textc(10, "VALMISTAUTUKAA...", C_YELLOW);
        else if (fightT == 60) bg_restore(0, 10, 40);
        else if (fightT > 60 && fightT < 130) {
            if (((fightT >> 2) & 1)) textc(10, "TAISTELE!", C_ORANGE);
            else bg_restore(14, 10, 12);
        } else if (fightT == 130) bg_restore(0, 10, 40);
    } else {
        ++koT;
        update_one(first, 0);
        if (vis) visuals(1);
        update_one(first ^ 1, 0);
        if (koT == 1) {
            if (timeUp && winner == 255) textc(11, "TASAPELI!", C_YELLOW);
            else big_msg(timeUp ? "AIKA!" : "K.O.");
        }
        if (koT == 20) { sfx_play(SFX_KO); queue_digi(DIGI_AARGH, 10); }
        if (winner != 255 && koT == 45) {
            sel_f(winner);
            set_state(ST_WIN);
            x = C.xf;
            if (x < (60 << FP)) x = 60 << FP;
            if (x > (260 << FP)) x = 260 << FP;
            C.xf = x;
            C.px = x >> FP;
        }
        if (koT == 150) bg_restore_rows(8, 15);
        if (koT >= ((timeUp && winner == 255) ? 150 : 210)) {
            if (winner != 255) { goto_state(GS_VICTORY); return 1; }
            fight_start();        /* draw: one more round, no mercy */
            return 1;
        }
    }
    PT(7);
    blood_update();
    PT(8);
    return 0;
}

/* one main-loop pass: catches up with missed frames so the game speed
 * stays constant even when a heavy frame (digi + sprite mirroring) runs long */

u8 fight_frame(void)
{
    u8 n = frame_cnt - last_fc;
    last_fc = frame_cnt;
    if (n == 0 || n > 3) n = 1;

    if (sel.mode == 0 && (key_any || key_confirm)) {   /* attract mode: any key ends it */
        spr_all_off();
        goto_state(GS_TITLE);
        return 1;
    }
    if (key_pause && (in_ko || fightT >= 90)) {
        paused ^= 1;
        sfx_play(SFX_BLIP);
        pause_draw(paused);
    }
    if (paused) {
        if (key_t) {
            paused = 0;
            spr_all_off();
            goto_state(GS_TITLE);
            return 1;
        }
        return 0;
    }
    while (--n) if (tick(0)) return 1;
    if (tick(1)) return 1;
    if (tsec != hud_sec) { hud_sec = tsec; draw_timer(tsec); }
    visuals(1);
    PT(9);
    return 0;
}

/* ---------------------------------------------------------------- victory */
static const char *vic_taunt;
static u8 vic_t, vic_shown, vic_len;

void victory_enter(void)
{
    static char title[24];
    bg_restore_rows(0, 14);
    vic_t = 0;
    vic_shown = 0;
    vic_taunt = taunts[rndn(NTAUNTS)];
    vic_len = (u8)strlen(vic_taunt);
    sel_f(winner);
    strcpy(title, C.isCpu ? "CPU " : (winner == 0 ? "P1 " : "P2 "));
    strcat(title, chars[sel.ch[winner]].name);
    strcat(title, " VOITTI!");
    textc(2, title, C_ORANGE);
    sel_f(loser);
    C.dead = 1;
    C.state = ST_DEAD;
    C.t = 200;
    sel_f(winner);
    set_state(ST_WIN);
    sfx_play(SFX_TAUNT);
    queue_digi(curse_digi[rndn(14)], 25);
    music_play(MUS_VICTORY);
}

/* typewriter with word wrap over rows 4..6 */
static void taunt_draw(u8 n)
{
    u8 row = 4, start = 0, i, last, lw;
    while (start < n && row < 7) {
        lw = vic_len - start;
        if (lw > 38) {
            last = start + 38;
            while (last > start && vic_taunt[last] != ' ') --last;
            if (last == start) last = start + 38;
            lw = last - start;
        }
        i = (n - start < lw) ? n - start : lw;
        textn((40 - lw) >> 1, row, vic_taunt + start, i, C_WHITE);
        start += lw;
        if (vic_taunt[start] == ' ') ++start;
        ++row;
    }
}

void victory_frame(void)
{
    ++vic_t;
    if (vic_t == 255) vic_t = 200;
    if (digi_q != 255 && --digi_qT == 0) { digi_play(digi_q); digi_q = 255; }
    sel_f(winner);
    ++C.animT;
    if (vic_shown < vic_len && vic_t > 40) {
        ++vic_shown;
        taunt_draw(vic_shown);
        if (!(vic_shown & 1) && !digi_on) sfx_play(SFX_BLIP);
    }
    if (vic_t == 61) {
        frame_box(9, 16, 22, 5, C_GRAY);
        textc(17, "NAPPI = UUSI OTTELU", C_WHITE);
        textc(18, "C = HAHMOT", C_WHITE);
        textc(19, "T = ALKURUUTU", C_WHITE);
    }
    blood_update();
    visuals_both(0);
    if (sel.mode == 0) {
        if (vic_t >= 200 || key_any) { spr_all_off(); goto_state(GS_TITLE); }
        return;
    }
    if (vic_t > 60) {
        if (key_confirm) { sfx_play(SFX_SELECT); spr_all_off(); goto_state(GS_VS); }
        else if (key_c) { sfx_play(SFX_SELECT); spr_all_off(); goto_state(GS_CHARSEL); }
        else if (key_t) { sfx_play(SFX_SELECT); spr_all_off(); goto_state(GS_TITLE); }
    }
}
