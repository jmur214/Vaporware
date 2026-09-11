/* crawl.c — one-button dungeon crawler for RAZ DC25000
 *           (N32G031K8Q7-1 + GC9107 128x160 LCD, Vaporware SDK)
 *
 * Your character walks the dungeon on their own.  You only fight.
 *
 * ── One button has to mean three things ──────────────────────────────────
 * Attack, defend and interact, from a single input.  The split is by
 * duration, and which verb gets the fast gesture is the whole design:
 *
 *   TAP  (press, release inside TAP_MAX)   attack, or interact
 *   HOLD (past GUARD_ARM)                  raise guard, until released
 *
 * Guard is the one that has to be reachable under pressure, so it needs no
 * decision to start — you just hold, and it arms itself.  Attacking is the
 * deliberate act, because every swing is a window where you are not guarding.
 * The two windows do not overlap: released before TAP_MAX it was always a
 * swing, held past GUARD_ARM it was always a guard, and nothing lands in
 * between.  A gesture that could be read either way would be unplayable in a
 * fight where reads are worth health.
 *
 * ── Why the telegraphs carry the game ────────────────────────────────────
 * With one button there is no dodge, no combo, no target select.  All the
 * depth has to live in the read, so enemies announce two different attacks:
 *
 *   WHITE  a normal swing   — guard it, and a guard raised late enough is a
 *                             parry, which stuns and opens a free window
 *   RED    a guard breaker  — do NOT be holding; blocking one is worse than
 *                             eating it
 *
 * That single fork is what stops holding guard forever from being correct,
 * and it means every enemy type is really a different timing puzzle rather
 * than a different pile of hit points.  Five types vary windup, recovery and
 * how often they break — a rat is a reflex test, an orc is a patience test.
 *
 * Permadeath, procedural floors, four item types, a boss every fifth floor.
 * Deepest floor persists in NV_KEY_APP_1.
 */
#include "app.h"
#include "display.h"
#include "battery.h"
#include "system.h"

/* ===================================================================
 * Palette
 * =================================================================== */
#define COL_BG      COL_RGB( 20,  18,  28)
#define COL_STONE   COL_RGB( 58,  54,  74)
#define COL_FLOOR   COL_RGB( 44,  40,  56)
#define COL_INK     COL_RGB(238, 238, 246)
#define COL_DIM     COL_RGB(128, 124, 148)
#define COL_HP      COL_RGB( 70, 205, 100)
#define COL_HPLOW   COL_RGB(225,  60,  50)
#define COL_EHP     COL_RGB(200,  60,  70)
#define COL_TRACK   COL_RGB( 40,  38,  52)
#define COL_WARN    COL_RGB(245, 240, 220)   /* normal telegraph        */
#define COL_BREAK   COL_RGB(240,  50,  40)   /* guard-breaker telegraph */
#define COL_GUARD   COL_RGB( 80, 160, 255)
#define COL_GOLD    COL_RGB(255, 205,   0)
#define COL_HERO    COL_RGB(210, 215, 235)
#define COL_BLADE   COL_RGB(180, 220, 255)
#define COL_CHEST   COL_RGB(190, 140,  60)

/* ===================================================================
 * Layout
 * =================================================================== */
#define HUD_Y        4
#define EBAR_Y      26
#define SCENE_Y     44
#define SCENE_H     74
#define GROUND_Y   (SCENE_Y + SCENE_H - 12)
#define TELE_Y     124
#define TELE_H      10
#define MSG_Y      140

#define HERO_X      22
#define ENEMY_X     84

/* ===================================================================
 * Tuning
 * =================================================================== */
#define TAP_MAX     180u    /* released inside this is always a swing   */
#define GUARD_ARM   180u    /* held past this is always a guard         */
#define PARRY_MS    260u    /* guard raised this late parries           */
#define SWING_CD    240u
#define WALK_MS    1100u

#define NV_KEY_BEST  NV_KEY_APP_1

/* ===================================================================
 * 3x5 glyphs
 * =================================================================== */
static const uint8_t g_alpha[26][5] = {
    {0x2,0x5,0x7,0x5,0x5}, {0x6,0x5,0x6,0x5,0x6}, {0x3,0x4,0x4,0x4,0x3},
    {0x6,0x5,0x5,0x5,0x6}, {0x7,0x4,0x6,0x4,0x7}, {0x7,0x4,0x6,0x4,0x4},
    {0x3,0x4,0x5,0x5,0x3}, {0x5,0x5,0x7,0x5,0x5}, {0x7,0x2,0x2,0x2,0x7},
    {0x1,0x1,0x1,0x5,0x2}, {0x5,0x5,0x6,0x5,0x5}, {0x4,0x4,0x4,0x4,0x7},
    {0x5,0x7,0x7,0x5,0x5}, {0x5,0x7,0x7,0x7,0x5}, {0x2,0x5,0x5,0x5,0x2},
    {0x6,0x5,0x6,0x4,0x4}, {0x2,0x5,0x5,0x7,0x3}, {0x6,0x5,0x6,0x5,0x5},
    {0x3,0x4,0x2,0x1,0x6}, {0x7,0x2,0x2,0x2,0x2}, {0x5,0x5,0x5,0x5,0x7},
    {0x5,0x5,0x5,0x5,0x2}, {0x5,0x5,0x7,0x7,0x5}, {0x5,0x5,0x2,0x5,0x5},
    {0x5,0x5,0x2,0x2,0x2}, {0x7,0x1,0x2,0x4,0x7},
};

static const uint8_t g_digit[10][5] = {
    {0x7,0x5,0x5,0x5,0x7},{0x2,0x6,0x2,0x2,0x7},{0x7,0x1,0x7,0x4,0x7},
    {0x7,0x1,0x7,0x1,0x7},{0x5,0x5,0x7,0x1,0x1},{0x7,0x4,0x7,0x1,0x7},
    {0x7,0x4,0x7,0x5,0x7},{0x7,0x1,0x1,0x1,0x1},{0x7,0x5,0x7,0x5,0x7},
    {0x7,0x5,0x7,0x1,0x7},
};

#define CELL_W(scale) (4u * (scale))

static const uint8_t* glyph_for(char c) {
    if(c >= 'A' && c <= 'Z') return g_alpha[c - 'A'];
    if(c >= '0' && c <= '9') return g_digit[c - '0'];
    return 0;
}

static void draw_glyph(const uint8_t bm[5], uint16_t x, uint16_t y,
                       uint8_t scale, uint16_t fg) {
    for(uint8_t r = 0; r < 5; r++) {
        uint8_t bits = bm[r];
        for(uint8_t c = 0; c < 3; c++) {
            if((bits >> (2 - c)) & 1u) {
                display_fill_rect((uint16_t)(x + c * scale),
                                  (uint16_t)(y + r * scale), scale, scale, fg);
            }
        }
    }
}

static uint16_t str_len(const char* s) {
    uint16_t n = 0;
    while(s[n]) n++;
    return n;
}

static void draw_span(const char* s, uint16_t len, uint16_t x, uint16_t y,
                      uint8_t scale, uint16_t fg) {
    for(uint16_t i = 0; i < len; i++) {
        const uint8_t* bm = glyph_for(s[i]);
        if(bm) draw_glyph(bm, x, y, scale, fg);
        x = (uint16_t)(x + CELL_W(scale));
    }
}

static void draw_span_mid(const char* s, uint16_t len, uint16_t y,
                          uint8_t scale, uint16_t fg) {
    uint16_t w = len ? (uint16_t)(len * CELL_W(scale) - scale) : 0u;
    uint16_t x = (w >= LCD_WIDTH) ? 0u : (uint16_t)((LCD_WIDTH - w) / 2);
    draw_span(s, len, x, y, scale, fg);
}

static void draw_text(const char* s, uint16_t x, uint16_t y,
                      uint8_t scale, uint16_t fg) {
    draw_span(s, str_len(s), x, y, scale, fg);
}

static void draw_text_mid(const char* s, uint16_t y, uint8_t scale, uint16_t fg) {
    draw_span_mid(s, str_len(s), y, scale, fg);
}

static uint8_t num_str(char buf[4], uint16_t v) {
    if(v > 999u) v = 999u;
    uint8_t n = 0;
    if(v >= 100u) buf[n++] = (char)('0' + (v / 100u) % 10u);
    if(v >= 10u)  buf[n++] = (char)('0' + (v / 10u) % 10u);
    buf[n++] = (char)('0' + v % 10u);
    buf[n] = '\0';
    return n;
}

/* ===================================================================
 * RNG
 * =================================================================== */
static uint32_t g_seed = 0xD0465EEDUL;

static uint32_t rnd(uint32_t mod) {
    g_seed = g_seed * 1664525UL + 1013904223UL;
    return (g_seed >> 16) % mod;
}

static void fill_circle(int16_t cx, int16_t cy, int16_t r, uint16_t col) {
    for(int16_t dy = -r; dy <= r; dy++) {
        int16_t y = (int16_t)(cy + dy);
        if(y < 0 || y >= LCD_HEIGHT) continue;
        int16_t half = 0;
        while((int16_t)((half + 1) * (half + 1) + dy * dy) <= (int16_t)(r * r)) half++;
        int16_t x0 = (int16_t)(cx - half), w = (int16_t)(half * 2 + 1);
        if(x0 < 0) { w = (int16_t)(w + x0); x0 = 0; }
        if(x0 + w > LCD_WIDTH) w = (int16_t)(LCD_WIDTH - x0);
        if(w > 0) display_fill_rect((uint16_t)x0, (uint16_t)y, (uint16_t)w, 1, col);
    }
}

/* ===================================================================
 * Bestiary
 *
 * Each type is a timing profile first and a stat block second.  A rat wants
 * reflexes, an orc wants patience, a wraith punishes turtling — that variety
 * is the content, because the button is always the same.
 * =================================================================== */
typedef struct {
    const char* name;
    uint16_t    hp_base;
    uint16_t    hp_per;     /* added per floor                          */
    uint8_t     atk_base;
    uint8_t     atk_per4;   /* added per four floors                    */
    uint16_t    wind_ms;    /* telegraph length at floor 1              */
    uint16_t    wind_min;   /* floor it will not speed past             */
    uint16_t    recover_ms; /* vulnerable window after a swing          */
    uint8_t     break_pct;  /* chance an attack is a guard breaker      */
    uint16_t    col;
} Foe;

static const Foe g_foe[5] = {
/*  name        hp  hp+  atk  a+4  wind  wmin  recov  brk  colour            */
  { "SLIME",     8,   2,   2,   1,  1000,  620,   760,   0, COL_RGB( 90,205,110) },
  { "RAT",       6,   2,   2,   1,   520,  330,   520,  10, COL_RGB(170,120, 80) },
  { "SKELETON", 11,   3,   3,   1,   820,  500,   660,  30, COL_RGB(225,228,215) },
  { "ORC",      16,   4,   5,   2,  1180,  760,   900,  45, COL_RGB( 95,150, 85) },
  { "WRAITH",   12,   3,   4,   2,   600,  380,   560,  55, COL_RGB(170,110,235) },
};

/* ===================================================================
 * Run state
 * =================================================================== */
#define ST_TITLE   0
#define ST_WALK    1
#define ST_FIGHT   2
#define ST_CHEST   3
#define ST_MSG     4
#define ST_DEAD    5

/* Enemy phases */
#define EP_IDLE    0
#define EP_WIND    1
#define EP_RECOVER 2
#define EP_STUN    3

/* Encounter kinds */
#define EV_FOE     0
#define EV_CHEST   1
#define EV_FOUNT   2
#define EV_STAIRS  3

static uint8_t  g_state;

static uint16_t g_hp, g_maxhp;
static uint8_t  g_atk, g_armor;
static uint8_t  g_floor;
static uint8_t  g_best;

static uint8_t  g_room, g_rooms;   /* position within the floor         */
static uint8_t  g_event;

static uint8_t  g_ftype;           /* foe index                          */
static uint8_t  g_boss;
static uint16_t g_ehp, g_emaxhp;
static uint8_t  g_eatk;
static uint16_t g_ewind, g_erecov;
static uint8_t  g_ebreak;

static uint8_t  g_phase;
static uint16_t g_ph_t0, g_ph_dur;
static uint8_t  g_is_break;

static uint8_t  g_guard;           /* guard currently up                 */
static uint16_t g_guard_t0;
static uint16_t g_swing_t0;
static uint8_t  g_swinging;

static uint8_t  g_pressing;
static uint16_t g_press_t0;
static uint8_t  g_press_used;      /* hold already consumed this press    */

static uint16_t g_walk_t0;
static uint16_t g_msg_t0;
static const char* g_msg;
static const char* g_msg2;

/* Cached visual state, so the scene repaints only what moved. */
static uint8_t  g_drawn_phase = 0xFF;
static uint8_t  g_drawn_guard = 0xFF;
static uint8_t  g_drawn_swing = 0xFF;
static uint16_t g_drawn_hp    = 0xFFFF;
static uint16_t g_drawn_ehp   = 0xFFFF;
static int16_t  g_drawn_tele  = -1;

static uint16_t g_bat_raw = BAT_FULL;

/* ===================================================================
 * Sprites
 * =================================================================== */
static void clear_hero_box(void) {
    display_fill_rect(HERO_X - 12, SCENE_Y + 8, 40, SCENE_H - 20, COL_BG);
}

static void draw_hero(void) {
    clear_hero_box();

    uint16_t base = GROUND_Y;
    uint16_t bodyc = COL_HERO;

    /* legs, body, head */
    display_fill_rect(HERO_X + 2, (uint16_t)(base - 8), 4, 8, bodyc);
    display_fill_rect(HERO_X + 8, (uint16_t)(base - 8), 4, 8, bodyc);
    display_fill_rect(HERO_X + 1, (uint16_t)(base - 22), 12, 14, bodyc);
    display_fill_rect(HERO_X + 3, (uint16_t)(base - 32), 9, 10, bodyc);
    display_fill_rect(HERO_X + 9, (uint16_t)(base - 29), 3, 2, COL_BG);  /* eye */

    if(g_guard) {
        /* Shield forward — the pose has to read instantly, because the
         * player is checking whether their guard actually came up. */
        display_fill_rect(HERO_X + 14, (uint16_t)(base - 26), 5, 18, COL_GUARD);
        display_fill_rect(HERO_X + 13, (uint16_t)(base - 22), 2, 10, COL_GUARD);
    } else if(g_swinging) {
        display_fill_rect(HERO_X + 14, (uint16_t)(base - 24), 22, 3, COL_BLADE);
        display_fill_rect(HERO_X + 32, (uint16_t)(base - 28), 3, 10, COL_BLADE);
    } else {
        display_fill_rect(HERO_X + 14, (uint16_t)(base - 18), 3, 14, COL_BLADE);
    }
}

static void clear_foe_box(void) {
    display_fill_rect(ENEMY_X - 6, SCENE_Y + 4, 44, SCENE_H - 16, COL_BG);
}

static void draw_foe(void) {
    clear_foe_box();
    if(g_ehp == 0u) return;

    uint16_t base = GROUND_Y;
    uint16_t c    = g_foe[g_ftype].col;
    int16_t  lean = (g_phase == EP_WIND) ? 5 : 0;   /* reared back to strike */
    uint16_t x    = (uint16_t)(ENEMY_X + lean);
    uint8_t  big  = g_boss;

    switch(g_ftype) {
    case 0:  /* slime */
        fill_circle((int16_t)(x + 10), (int16_t)(base - 9), big ? 13 : 10, c);
        display_fill_rect((uint16_t)(x + 4), (uint16_t)(base - 12), 4, 3, COL_BG);
        display_fill_rect((uint16_t)(x + 13), (uint16_t)(base - 12), 4, 3, COL_BG);
        break;
    case 1:  /* rat */
        display_fill_rect(x, (uint16_t)(base - 10), 18, 9, c);
        fill_circle((int16_t)x, (int16_t)(base - 11), 6, c);
        display_fill_rect((uint16_t)(x + 16), (uint16_t)(base - 6), 12, 2, c);
        display_fill_rect((uint16_t)(x - 3), (uint16_t)(base - 13), 3, 2, COL_BG);
        break;
    case 2:  /* skeleton */
        display_fill_rect((uint16_t)(x + 4), (uint16_t)(base - 20), 9, 12, c);
        fill_circle((int16_t)(x + 8), (int16_t)(base - 26), 7, c);
        display_fill_rect((uint16_t)(x + 4), (uint16_t)(base - 28), 3, 3, COL_BG);
        display_fill_rect((uint16_t)(x + 10), (uint16_t)(base - 28), 3, 3, COL_BG);
        display_fill_rect((uint16_t)(x + 3), (uint16_t)(base - 8), 3, 8, c);
        display_fill_rect((uint16_t)(x + 11), (uint16_t)(base - 8), 3, 8, c);
        break;
    case 3:  /* orc */
        display_fill_rect((uint16_t)(x + 1), (uint16_t)(base - 24), 20, 16, c);
        fill_circle((int16_t)(x + 11), (int16_t)(base - 30), big ? 11 : 9, c);
        display_fill_rect((uint16_t)(x + 6), (uint16_t)(base - 32), 3, 3, COL_BG);
        display_fill_rect((uint16_t)(x + 14), (uint16_t)(base - 32), 3, 3, COL_BG);
        display_fill_rect((uint16_t)(x + 2), (uint16_t)(base - 8), 6, 8, c);
        display_fill_rect((uint16_t)(x + 14), (uint16_t)(base - 8), 6, 8, c);
        break;
    default: /* wraith — floats, so no legs and a ragged hem */
        fill_circle((int16_t)(x + 10), (int16_t)(base - 28), 8, c);
        display_fill_rect((uint16_t)(x + 3), (uint16_t)(base - 26), 15, 18, c);
        display_fill_rect((uint16_t)(x + 3), (uint16_t)(base - 8), 4, 4, c);
        display_fill_rect((uint16_t)(x + 10), (uint16_t)(base - 8), 4, 6, c);
        display_fill_rect((uint16_t)(x + 6), (uint16_t)(base - 30), 3, 3, COL_BG);
        display_fill_rect((uint16_t)(x + 13), (uint16_t)(base - 30), 3, 3, COL_BG);
        break;
    }

    if(g_phase == EP_STUN) draw_text("STUN", (uint16_t)(x - 2), SCENE_Y + 6, 1, COL_GOLD);
}

/* ===================================================================
 * HUD
 * =================================================================== */
static void draw_hud(void) {
    display_fill_rect(0, 0, LCD_WIDTH, 22, COL_BG);

    display_fill_rect(6, HUD_Y, 70, 9, COL_TRACK);
    uint32_t w = ((uint32_t)g_hp * 70u) / (g_maxhp ? g_maxhp : 1u);
    if(w) display_fill_rect(6, HUD_Y, (uint16_t)w, 9,
                            (g_hp * 4u <= g_maxhp) ? COL_HPLOW : COL_HP);

    char buf[4];
    uint8_t n;

    n = num_str(buf, g_floor);
    draw_text("F", 84, HUD_Y, 2, COL_DIM);
    draw_span(buf, n, 92, HUD_Y, 2, COL_INK);

    n = num_str(buf, g_atk);
    draw_text("A", 106, HUD_Y, 2, COL_DIM);
    draw_span(buf, n, 114, HUD_Y, 2, COL_GOLD);

    g_drawn_hp = g_hp;
}

static void draw_enemy_bar(void) {
    display_fill_rect(0, EBAR_Y - 2, LCD_WIDTH, 14, COL_BG);
    if(g_ehp == 0u) return;

    draw_text(g_foe[g_ftype].name, 6, EBAR_Y, 1, COL_DIM);
    if(g_boss) draw_text("BOSS", 100, EBAR_Y, 1, COL_GOLD);

    display_fill_rect(6, (uint16_t)(EBAR_Y + 7), 116, 4, COL_TRACK);
    uint32_t w = ((uint32_t)g_ehp * 116u) / (g_emaxhp ? g_emaxhp : 1u);
    if(w) display_fill_rect(6, (uint16_t)(EBAR_Y + 7), (uint16_t)w, 4, COL_EHP);

    g_drawn_ehp = g_ehp;
}

/* The telegraph bar is the single most important pixel region in the game,
 * so it gets its own strip and its own colour language. */
static void draw_telegraph(int16_t pct, uint8_t breaker) {
    display_fill_rect(0, TELE_Y - 2, LCD_WIDTH, TELE_H + 4, COL_BG);
    if(pct < 0) { g_drawn_tele = -1; return; }

    display_fill_rect(6, TELE_Y, 116, TELE_H, COL_TRACK);
    uint16_t w = (uint16_t)((116u * (uint16_t)pct) / 100u);
    if(w) display_fill_rect(6, TELE_Y, w, TELE_H, breaker ? COL_BREAK : COL_WARN);

    g_drawn_tele = pct;
}

static void draw_msg(const char* a, const char* b) {
    display_fill_rect(0, MSG_Y - 2, LCD_WIDTH, 22, COL_BG);
    if(a) draw_text_mid(a, MSG_Y, 2, COL_INK);
    if(b) draw_text_mid(b, (uint16_t)(MSG_Y + 12), 1, COL_DIM);
}

static void draw_room(void) {
    display_fill(COL_BG);
    display_fill_rect(0, SCENE_Y, LCD_WIDTH, 2, COL_STONE);
    display_fill_rect(0, GROUND_Y, LCD_WIDTH, (uint16_t)(SCENE_Y + SCENE_H - GROUND_Y),
                      COL_FLOOR);
    draw_hud();
    draw_enemy_bar();
    draw_hero();
    draw_foe();
    g_drawn_phase = g_phase;
    g_drawn_guard = g_guard;
    g_drawn_swing = g_swinging;
}

/* ===================================================================
 * Floor generation
 * =================================================================== */
static void spawn_foe(void) {
    /* Deeper floors unlock nastier bestiary entries rather than only
     * inflating numbers, so the fight itself changes shape as you descend. */
    uint8_t pool = (g_floor <= 1u) ? 2u
                 : (g_floor <= 3u) ? 3u
                 : (g_floor <= 6u) ? 4u : 5u;
    g_ftype = (uint8_t)rnd(pool);
    g_boss  = (g_room + 1u == g_rooms && (g_floor % 5u) == 0u) ? 1u : 0u;

    const Foe* f = &g_foe[g_ftype];

    g_emaxhp = (uint16_t)(f->hp_base + f->hp_per * g_floor);
    g_eatk   = (uint8_t)(f->atk_base + (f->atk_per4 * g_floor) / 4u);

    if(g_boss) {
        g_emaxhp = (uint16_t)(g_emaxhp * 2u);
        g_eatk   = (uint8_t)(g_eatk + 2u);
    }
    g_ehp = g_emaxhp;

    uint16_t wind = f->wind_ms;
    uint16_t cut  = (uint16_t)(g_floor * 34u);
    g_ewind  = (wind > cut && (uint16_t)(wind - cut) > f->wind_min)
                   ? (uint16_t)(wind - cut) : f->wind_min;
    g_erecov = f->recover_ms;
    g_ebreak = f->break_pct;

    g_phase  = EP_IDLE;
    g_ph_t0  = ms_now();
    g_ph_dur = (uint16_t)(420u + rnd(520));
}

static void gen_floor(void) {
    g_rooms = (uint8_t)(4u + rnd(3));
    g_room  = 0;
}

/* Pick what is in the next room. */
static void next_room(void) {
    if(g_room >= g_rooms) {
        g_event = EV_STAIRS;
        return;
    }
    uint32_t r = rnd(100);
    if(g_room + 1u == g_rooms) g_event = EV_FOE;       /* floors end in a fight */
    else if(r < 62u)           g_event = EV_FOE;
    else if(r < 84u)           g_event = EV_CHEST;
    else                       g_event = EV_FOUNT;
}

/* ===================================================================
 * Messages
 * =================================================================== */
static void show_msg(const char* a, const char* b) {
    g_msg = a;
    g_msg2 = b;
    g_msg_t0 = ms_now();
    draw_msg(a, b);
}

/* ===================================================================
 * Combat resolution
 * =================================================================== */
static void hero_hurt(uint16_t dmg) {
    if(dmg < 1u) dmg = 1u;
    g_hp = (g_hp > dmg) ? (uint16_t)(g_hp - dmg) : 0u;
    draw_hud();
}

static void enemy_strike(void) {
    uint16_t dmg = g_eatk;
    if(g_armor) dmg = (dmg > g_armor) ? (uint16_t)(dmg - g_armor) : 1u;

    if(g_is_break) {
        if(g_guard) {
            /* Blocking a breaker is the worst outcome available, which is
             * what makes the red telegraph worth reading. */
            hero_hurt((uint16_t)(dmg + dmg / 2u + 1u));
            show_msg("BROKEN", "RED MEANS RELEASE");
        } else {
            hero_hurt((uint16_t)(dmg / 2u));
            show_msg("GRAZED", 0);
        }
    } else if(g_guard) {
        uint16_t since = (uint16_t)(ms_now() - g_guard_t0);
        if(since <= PARRY_MS) {
            g_phase  = EP_STUN;
            g_ph_t0  = ms_now();
            g_ph_dur = 1200u;
            show_msg("PARRY", "FREE HITS");
            return;
        }
        hero_hurt(1u);          /* chip, so turtling is not free */
        show_msg("BLOCK", 0);
    } else {
        hero_hurt(dmg);
        show_msg("HIT", 0);
    }

    g_phase  = EP_RECOVER;
    g_ph_t0  = ms_now();
    g_ph_dur = g_erecov;
}

static void hero_swing(void) {
    uint16_t dmg = g_atk;
    uint8_t open = (g_phase == EP_RECOVER || g_phase == EP_STUN);
    if(open) dmg = (uint16_t)(dmg * 2u);

    g_ehp = (g_ehp > dmg) ? (uint16_t)(g_ehp - dmg) : 0u;
    draw_enemy_bar();
    if(open) show_msg("CRIT", 0);
}

/* ===================================================================
 * Items
 * =================================================================== */
static void give_item(void) {
    switch(rnd(4)) {
    case 0:
        g_atk++;
        show_msg("SHARPER", "ATTACK UP");
        break;
    case 1:
        g_armor++;
        show_msg("ARMOUR", "DAMAGE DOWN");
        break;
    case 2:
        g_maxhp = (uint16_t)(g_maxhp + 4u);
        g_hp    = (uint16_t)(g_hp + 4u);
        show_msg("VIGOUR", "MAX HP UP");
        break;
    default: {
        uint16_t heal = 8u;
        g_hp = (uint16_t)(g_hp + heal);
        if(g_hp > g_maxhp) g_hp = g_maxhp;
        show_msg("POTION", "HEALED");
        break;
    }
    }
    draw_hud();
}

/* ===================================================================
 * Screens
 * =================================================================== */
static void draw_title(void) {
    display_fill(COL_BG);
    draw_text_mid("DEEP", 20, 5, COL_INK);
    draw_text_mid("DARK", 54, 5, COL_GOLD);

    draw_text_mid("TAP ATTACK", 96, 2, COL_INK);
    draw_text_mid("HOLD GUARD", 112, 2, COL_GUARD);
    draw_text_mid("RED BREAKS GUARD", 130, 1, COL_BREAK);

    if(g_best) {
        char buf[4];
        uint8_t n = num_str(buf, g_best);
        draw_text("BEST F", 38, 146, 1, COL_DIM);
        draw_span(buf, n, 76, 144, 2, COL_GOLD);
    }
}

static void draw_dead(void) {
    display_fill(COL_BG);
    draw_text_mid("YOU DIED", 34, 3, COL_HPLOW);

    char buf[4];
    uint8_t n = num_str(buf, g_floor);
    draw_text_mid("FLOOR", 70, 2, COL_DIM);
    draw_span_mid(buf, n, 86, 6, COL_INK);

    if(g_floor >= g_best) draw_text_mid("DEEPEST YET", 130, 1, COL_GOLD);
    draw_text_mid("TAP", 148, 1, COL_DIM);
}

static void draw_walk(void) {
    display_fill(COL_BG);
    display_fill_rect(0, SCENE_Y, LCD_WIDTH, 2, COL_STONE);
    display_fill_rect(0, GROUND_Y, LCD_WIDTH, (uint16_t)(SCENE_Y + SCENE_H - GROUND_Y),
                      COL_FLOOR);
    draw_hud();
    g_guard = 0;
    g_swinging = 0;
    draw_hero();
    draw_text_mid("DESCENDING", MSG_Y, 2, COL_DIM);
}

/* ===================================================================
 * Run control
 * =================================================================== */
static void begin_walk(void) {
    g_walk_t0 = ms_now();
    draw_walk();
    g_state = ST_WALK;
}

static void enter_room(void) {
    next_room();

    switch(g_event) {
    case EV_STAIRS:
        g_floor++;
        if(g_floor > g_best) {
            g_best = g_floor;
            nv_write(NV_KEY_BEST, g_best);
        }
        gen_floor();
        begin_walk();
        break;

    case EV_FOE:
        spawn_foe();
        g_guard = 0;
        g_swinging = 0;
        draw_room();
        draw_telegraph(-1, 0);
        draw_msg(g_foe[g_ftype].name, g_boss ? "BOSS" : 0);
        g_state = ST_FIGHT;
        break;

    case EV_CHEST:
        display_fill(COL_BG);
        display_fill_rect(0, GROUND_Y, LCD_WIDTH,
                          (uint16_t)(SCENE_Y + SCENE_H - GROUND_Y), COL_FLOOR);
        draw_hud();
        display_fill_rect(52, (uint16_t)(GROUND_Y - 18), 26, 18, COL_CHEST);
        display_fill_rect(52, (uint16_t)(GROUND_Y - 22), 26, 5, COL_GOLD);
        draw_msg("CHEST", "HOLD TO OPEN");
        g_state = ST_CHEST;
        break;

    default:
        g_hp = (uint16_t)(g_hp + 6u);
        if(g_hp > g_maxhp) g_hp = g_maxhp;
        draw_hud();
        display_fill_rect(0, SCENE_Y, LCD_WIDTH, SCENE_H, COL_BG);
        fill_circle(64, GROUND_Y - 14, 12, COL_GUARD);
        show_msg("FOUNTAIN", "HEALED 6");
        g_state = ST_MSG;
        break;
    }
}

static void room_cleared(void) {
    g_room++;
    begin_walk();
}

static void run_begin(void) {
    g_seed ^= ((uint32_t)ms_now() << 11) ^ 0xDEADBEEFUL;

    g_maxhp = 22;
    g_hp    = g_maxhp;
    g_atk   = 3;
    g_armor = 0;
    g_floor = 1;

    gen_floor();
    begin_walk();
}

/* ===================================================================
 * Framework callbacks
 * =================================================================== */
static void on_hard_reset(void) {
    g_state = ST_TITLE;
    draw_title();
}

void app_init(void) {
    display_recover();

    app_set_sleep_timeout(45000);
    app_set_hold_reset(10000, on_hard_reset);

    g_best = (uint8_t)nv_read(NV_KEY_BEST, 0);
    if(g_best > 99u) g_best = 0;
    g_bat_raw = bat_read_raw();

    g_state = ST_TITLE;
    display_fill(COL_BLACK);
    draw_title();
}

void app_update(uint32_t frame) {
    (void)frame;

    uint8_t btn = button_raw();
    uint8_t tap = 0, hold = 0;

    if(btn && !g_pressing) {
        g_pressing   = 1;
        g_press_used = 0;
        g_press_t0   = ms_now();
    } else if(btn && g_pressing) {
        if(!g_press_used && (uint16_t)(ms_now() - g_press_t0) >= GUARD_ARM) {
            hold = 1;                 /* fires once, when the hold arms */
            g_press_used = 1;
        }
    } else if(!btn && g_pressing) {
        g_pressing = 0;
        /* Released inside the window, and the hold never armed, so this was
         * unambiguously a tap. */
        if(!g_press_used && (uint16_t)(ms_now() - g_press_t0) < TAP_MAX) tap = 1;
    }

    /* Guard is simply "the hold is currently armed". */
    uint8_t want_guard = (btn && g_press_used) ? 1u : 0u;

    switch(g_state) {

    case ST_TITLE:
        if(tap || hold) run_begin();
        break;

    case ST_WALK:
        if((uint16_t)(ms_now() - g_walk_t0) >= WALK_MS) enter_room();
        break;

    case ST_FIGHT: {
        if(want_guard != g_guard) {
            g_guard = want_guard;
            if(g_guard) g_guard_t0 = ms_now();
            draw_hero();
        }

        if(tap && !g_guard &&
           (uint16_t)(ms_now() - g_swing_t0) >= SWING_CD) {
            g_swing_t0 = ms_now();
            g_swinging = 1;
            draw_hero();
            hero_swing();

            if(g_ehp == 0u) {
                show_msg("SLAIN", 0);
                g_swinging = 0;
                draw_foe();
                draw_telegraph(-1, 0);
                g_msg_t0 = ms_now();
                g_state  = ST_MSG;
                break;
            }
        }

        if(g_swinging && (uint16_t)(ms_now() - g_swing_t0) >= 140u) {
            g_swinging = 0;
            draw_hero();
        }

        uint16_t el = (uint16_t)(ms_now() - g_ph_t0);

        switch(g_phase) {
        case EP_IDLE:
            if(el >= g_ph_dur) {
                g_is_break = (rnd(100) < g_ebreak) ? 1u : 0u;
                g_phase    = EP_WIND;
                g_ph_t0    = ms_now();
                g_ph_dur   = g_ewind;
                draw_foe();
                draw_msg(g_is_break ? "BREAKER" : "ATTACK",
                         g_is_break ? "DO NOT GUARD" : "GUARD NOW");
            }
            break;

        case EP_WIND: {
            int16_t pct = (int16_t)((el * 100u) / (g_ph_dur ? g_ph_dur : 1u));
            if(pct > 100) pct = 100;
            if(pct != g_drawn_tele) draw_telegraph(pct, g_is_break);

            if(el >= g_ph_dur) {
                enemy_strike();
                draw_telegraph(-1, 0);
                draw_foe();
                if(g_hp == 0u) {
                    g_state = ST_DEAD;
                    draw_dead();
                }
            }
            break;
        }

        case EP_RECOVER:
        case EP_STUN:
            if(el >= g_ph_dur) {
                g_phase  = EP_IDLE;
                g_ph_t0  = ms_now();
                g_ph_dur = (uint16_t)(420u + rnd(520));
                draw_foe();
                draw_msg(0, 0);
            }
            break;
        }
        break;
    }

    case ST_CHEST:
        if(hold) {
            give_item();
            g_msg_t0 = ms_now();
            g_state  = ST_MSG;
        }
        break;

    case ST_MSG:
        if((uint16_t)(ms_now() - g_msg_t0) >= 1100u) room_cleared();
        break;

    case ST_DEAD:
        if(tap || hold) {
            g_state = ST_TITLE;
            draw_title();
        }
        break;
    }

    if((frame % 150u) == 0u && g_state != ST_FIGHT) {
        g_bat_raw = bat_read_raw();
    }
}

void app_wake(void) {
    /* Permadeath means a run interrupted by sleep is simply over. */
    g_pressing = 0;
    g_state    = ST_TITLE;
    draw_title();
}
