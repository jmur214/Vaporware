/* crawl.c — one-button dungeon crawler for RAZ DC25000
 *           (N32G031K8Q7-1 + GC9107 128x160 LCD, Vaporware SDK)
 *
 * ── Design: why guarding costs something ─────────────────────────────────
 * A one-button fighter collapses if guarding is free — hold guard, release on
 * the recovery window, repeat, and every enemy is the same wall.  So guard
 * burns STAMINA: it is a burst, never a stance.  Run it dry and your guard
 * BREAKS.  A parry refunds stamina, so reading well is what funds the next
 * read.  Enemies FEINT (the wind-up stalls) to punish guarding on sight, and
 * attack in COMBOS of 1-3 so recovery has to be earned.
 *
 * ── Design: why the animation layer is this elaborate ────────────────────
 * The whole game is "press at the right moment", so every moment has to be
 * legible and every press has to feel like it hit something.  That is not
 * decoration, it is the interface:
 *
 *   HITSTOP   — the world freezes ~4 frames on contact.  One technique, and
 *               it does more for impact than any amount of extra art.
 *   PARRY ZONE— the telegraph bar shows the window you must press in, so the
 *               timing is a thing you can learn instead of guess.  A feint
 *               visibly slides the zone, which is what makes it fair.
 *   SMEAR     — the sword's first frame is already extended with an arc
 *               streak behind it, so the swing animates without ever costing
 *               the player a frame of input latency.
 *   SHIELD    — stamina is drawn as the fill level of the shield itself, so
 *               the resource lives where your eye already is.
 *   GHOST BAR — health drains behind a trailing white bar, so a big hit reads
 *               as big even after the number is gone.
 *
 * Everything animates out of dirty rectangles: each actor owns a box, the box
 * is repainted with floor/wall, then the pose is drawn.  Full-screen fills are
 * ~50 ms and never happen mid-fight.
 *
 * Controls — TAP attack / choose, HOLD guard / confirm.  Permadeath.
 */
#include "app.h"
#include "display.h"
#include "battery.h"
#include "system.h"

/* ===================================================================
 * Palette
 * =================================================================== */
#define COL_BG      COL_RGB( 18,  16,  26)
#define COL_WALL    COL_RGB( 40,  36,  54)
#define COL_FLOOR   COL_RGB( 46,  41,  58)
#define COL_INK     COL_RGB(238, 238, 246)
#define COL_DIM     COL_RGB(126, 122, 146)
#define COL_HP      COL_RGB( 70, 205, 100)
#define COL_HPLOW   COL_RGB(225,  60,  50)
#define COL_GHOST   COL_RGB(245, 235, 200)
#define COL_STAM    COL_RGB( 80, 165, 255)
#define COL_STAMLOW COL_RGB(255, 150,  40)
#define COL_EHP     COL_RGB(200,  60,  70)
#define COL_TRACK   COL_RGB( 34,  32,  46)
#define COL_ZONE    COL_RGB( 96,  80,  20)
#define COL_WARN    COL_RGB(245, 240, 220)
#define COL_BREAK   COL_RGB(240,  50,  40)
#define COL_FEINT   COL_RGB(120, 112,  90)
#define COL_GOLD    COL_RGB(255, 205,   0)
#define COL_HERO    COL_RGB(206, 212, 232)
#define COL_HEROD   COL_RGB(150, 156, 178)
#define COL_BLADE   COL_RGB(196, 228, 255)
#define COL_FLASH   COL_RGB(255, 255, 255)
#define COL_SPARK   COL_RGB(255, 236, 160)
#define COL_TORCH   COL_RGB(255, 170,  40)
#define COL_SEL_BAR COL_RGB(255, 205,   0)

/* ===================================================================
 * Layout
 * =================================================================== */
#define HP_Y         4
#define STAM_Y      13
#define EBAR_Y      22
#define FLOAT_Y     34
#define SCENE_Y     46
#define SCENE_H     68
#define GROUND_Y   (SCENE_Y + SCENE_H - 12)
#define TELE_Y     118
#define TELE_H      11
#define MSG_Y      134

#define HERO_X      20
#define ENEMY_X     82

/* Dirty boxes.  Sized to the widest pose plus shake padding. */
#define HB_X   12
#define HB_Y   58
#define HB_W   46
#define HB_H   48
#define FB_X   66
#define FB_Y   60
#define FB_W   50
#define FB_H   46

/* ===================================================================
 * Tuning
 * =================================================================== */
#define TAP_MAX      170u
#define GUARD_ARM    170u
#define PARRY_MS     280u
#define SWING_CD     230u

#define STAM_MAX     100
#define STAM_DRAIN     2
#define STAM_REGEN     1
#define STAM_BLOCK    18
#define STAM_PARRY    30
#define BREAK_MS     900u

#define HITSTOP_HIT    3
#define HITSTOP_BIG    6

#define NPART          12

#define NV_KEY_BEST  NV_KEY_APP_1

/* ===================================================================
 * Glyphs
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
#define GW(s) (4u * (s))

static const uint8_t* glyph_for(char c) {
    if(c >= 'A' && c <= 'Z') return g_alpha[c - 'A'];
    if(c >= '0' && c <= '9') return g_digit[c - '0'];
    return 0;
}
static void draw_glyph(const uint8_t bm[5], uint16_t x, uint16_t y,
                       uint8_t sc, uint16_t fg) {
    for(uint8_t r = 0; r < 5; r++)
        for(uint8_t c = 0; c < 3; c++)
            if((bm[r] >> (2 - c)) & 1u)
                display_fill_rect((uint16_t)(x + c * sc), (uint16_t)(y + r * sc),
                                  sc, sc, fg);
}
static uint16_t str_len(const char* s) { uint16_t n = 0; while(s[n]) n++; return n; }
static void draw_span(const char* s, uint16_t len, uint16_t x, uint16_t y,
                      uint8_t sc, uint16_t fg) {
    for(uint16_t i = 0; i < len; i++) {
        const uint8_t* bm = glyph_for(s[i]);
        if(bm) draw_glyph(bm, x, y, sc, fg);
        x = (uint16_t)(x + GW(sc));
    }
}
static void draw_span_mid(const char* s, uint16_t len, uint16_t y,
                          uint8_t sc, uint16_t fg) {
    uint16_t w = len ? (uint16_t)(len * GW(sc) - sc) : 0u;
    draw_span(s, len, (w >= LCD_WIDTH) ? 0u : (uint16_t)((LCD_WIDTH - w) / 2), y, sc, fg);
}
static void draw_text(const char* s, uint16_t x, uint16_t y, uint8_t sc, uint16_t c) {
    draw_span(s, str_len(s), x, y, sc, c);
}
static void draw_text_mid(const char* s, uint16_t y, uint8_t sc, uint16_t c) {
    draw_span_mid(s, str_len(s), y, sc, c);
}
static uint8_t num_str(char b[4], uint16_t v) {
    if(v > 999u) v = 999u;
    uint8_t n = 0;
    if(v >= 100u) b[n++] = (char)('0' + (v / 100u) % 10u);
    if(v >= 10u)  b[n++] = (char)('0' + (v / 10u) % 10u);
    b[n++] = (char)('0' + v % 10u);
    b[n] = '\0';
    return n;
}

/* ===================================================================
 * Maths
 * =================================================================== */
static uint32_t g_seed = 0xD0465EEDUL;
static uint32_t rnd(uint32_t m) {
    g_seed = g_seed * 1664525UL + 1013904223UL;
    return (g_seed >> 16) % m;
}

/* 16-step sine, Q7.  Index 0 = +x, 4 = +y (screen down), 12 = up. */
static const int8_t g_sin16[16] = {
    0, 49, 90, 117, 127, 117, 90, 49, 0, -49, -90, -117, -127, -117, -90, -49
};
static int16_t isin(uint8_t i) { return g_sin16[i & 15u]; }
static int16_t icos(uint8_t i) { return g_sin16[(i + 4u) & 15u]; }

static void px_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t col) {
    if(x < 0) { w = (int16_t)(w + x); x = 0; }
    if(y < 0) { h = (int16_t)(h + y); y = 0; }
    if(x + w > LCD_WIDTH)  w = (int16_t)(LCD_WIDTH - x);
    if(y + h > LCD_HEIGHT) h = (int16_t)(LCD_HEIGHT - y);
    if(w > 0 && h > 0)
        display_fill_rect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, col);
}

static void fill_circle(int16_t cx, int16_t cy, int16_t r, uint16_t col) {
    for(int16_t dy = -r; dy <= r; dy++) {
        int16_t h = 0;
        while((int16_t)((h + 1) * (h + 1) + dy * dy) <= (int16_t)(r * r)) h++;
        px_rect((int16_t)(cx - h), (int16_t)(cy + dy), (int16_t)(h * 2 + 1), 1, col);
    }
}

/* Thick Bresenham — the sword, and every angled thing on screen. */
static void draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      int16_t t, uint16_t col) {
    int16_t dx = (int16_t)(x1 - x0), dy = (int16_t)(y1 - y0);
    int16_t ax = dx < 0 ? (int16_t)-dx : dx, ay = dy < 0 ? (int16_t)-dy : dy;
    int16_t n  = (ax > ay ? ax : ay);
    if(n == 0) { px_rect(x0, y0, t, t, col); return; }
    for(int16_t i = 0; i <= n; i++)
        px_rect((int16_t)(x0 + (int32_t)dx * i / n),
                (int16_t)(y0 + (int32_t)dy * i / n), t, t, col);
}

/* Dotted arc — the swing smear, and the parry burst. */
static void draw_arc(int16_t cx, int16_t cy, int16_t r,
                     uint8_t a0, uint8_t a1, int16_t t, uint16_t col) {
    for(uint8_t a = a0; a != (uint8_t)((a1 + 1u) & 15u); a = (uint8_t)((a + 1u) & 15u))
        px_rect((int16_t)(cx + (r * icos(a) >> 7)),
                (int16_t)(cy + (r * isin(a) >> 7)), t, t, col);
}

/* ===================================================================
 * Bestiary
 * =================================================================== */
typedef struct {
    const char* name;
    uint16_t hp_base, hp_per;
    uint8_t  atk_base, atk_per4;
    uint16_t wind_ms, wind_min, recover_ms, gap_ms;
    uint8_t  break_pct, feint_pct, combo_max;
    uint16_t col;
} Foe;

static const Foe g_foe[5] = {
/*  name       hp hp+ atk a+4 wind wmin recov  gap brk fnt cmb colour        */
  { "SLIME",    9,  2,  2,  1, 1000, 640,  780, 300,  0,  0, 1, COL_RGB( 90,205,110) },
  { "RAT",      7,  2,  2,  1,  520, 340,  520, 220, 10, 15, 2, COL_RGB(178,126, 82) },
  { "SKELETON",12,  3,  3,  1,  820, 520,  660, 280, 30, 25, 2, COL_RGB(226,229,216) },
  { "ORC",     18,  4,  5,  2, 1180, 780,  920, 360, 45, 20, 2, COL_RGB( 96,152, 86) },
  { "WRAITH",  13,  3,  4,  2,  620, 400,  560, 200, 55, 40, 3, COL_RGB(174,114,238) },
};

/* ===================================================================
 * Items
 * =================================================================== */
#define IT_ATK    0
#define IT_ARMOR  1
#define IT_MAXHP  2
#define IT_HEAL   3
#define IT_STAM   4
#define IT_COUNT  5

static const char* const g_item_name[IT_COUNT] = {
    "WHETSTONE", "PLATING", "VIGOUR", "ELIXIR", "SECOND WIND"
};
static const char* const g_item_desc[IT_COUNT] = {
    "ATTACK UP", "TAKE LESS", "MAX HP UP", "HEAL FULLY", "STAMINA UP"
};

/* ===================================================================
 * State
 * =================================================================== */
#define ST_TITLE 0
#define ST_DOORS 1
#define ST_FIGHT 2
#define ST_PICK  3
#define ST_MSG   4
#define ST_DEAD  5

#define EP_IDLE 0
#define EP_WIND 1
#define EP_REC  2
#define EP_STUN 3

#define RM_FIGHT 0
#define RM_ELITE 1
#define RM_LOOT  2
#define RM_REST  3
#define RM_STAIR 4

static uint8_t  g_state;
static uint16_t g_hp, g_maxhp, g_hp_ghost;
static int16_t  g_stam, g_stammax;
static uint8_t  g_atk, g_armor, g_floor, g_best;
static uint8_t  g_room, g_rooms;

static uint8_t  g_door[2], g_sel, g_pick[2];

static uint8_t  g_ftype, g_elite;
static uint16_t g_ehp, g_emaxhp, g_ehp_ghost;
static uint8_t  g_eatk;
static uint16_t g_ewind, g_erecov, g_egap;
static uint8_t  g_ebreak, g_efeint, g_ecombo;

static uint8_t  g_phase, g_is_break, g_combo_left, g_followup;
static uint8_t  g_feint_at, g_feinted;
static uint16_t g_ph_t0, g_ph_dur;

static uint8_t  g_guard, g_broken, g_dying;
static uint16_t g_guard_t0, g_swing_t0, g_broke_t0;

/* animation */
static uint8_t  g_swing_f;        /* 0 = idle, 1..5 = swing frame          */
static uint8_t  g_hitstop;        /* frames the world is frozen            */
static uint8_t  g_shake;
static uint8_t  g_h_flash, g_e_flash;
static int16_t  g_hx, g_ex;       /* actor x offsets (knockback / lunge)   */
static uint8_t  g_death_f;
static uint8_t  g_bob;

static uint16_t g_tele_w;         /* last filled telegraph width           */
static int16_t  g_zone_x;

static uint8_t  g_pressing, g_press_used;
static uint16_t g_press_t0;

static uint16_t g_msg_t0;
static uint8_t  g_float_n, g_float_crit, g_float_f;

static uint16_t g_bat_raw = BAT_FULL;

typedef struct { int16_t x, y, vx, vy, ox, oy; uint8_t life; uint16_t col; } Part;
static Part g_part[NPART];

/* ===================================================================
 * Scene background
 * =================================================================== */
static void scene_clear(int16_t x, int16_t y, int16_t w, int16_t h) {
    if(x < 0) { w = (int16_t)(w + x); x = 0; }
    if(y < SCENE_Y) { h = (int16_t)(h - (SCENE_Y - y)); y = SCENE_Y; }
    if(x + w > LCD_WIDTH) w = (int16_t)(LCD_WIDTH - x);
    if(y + h > SCENE_Y + SCENE_H) h = (int16_t)(SCENE_Y + SCENE_H - y);
    if(w <= 0 || h <= 0) return;
    if(y < GROUND_Y) {
        int16_t hh = (int16_t)((y + h > GROUND_Y) ? (GROUND_Y - y) : h);
        px_rect(x, y, w, hh, COL_BG);
    }
    if(y + h > GROUND_Y) {
        int16_t y2 = (int16_t)(y > GROUND_Y ? y : GROUND_Y);
        px_rect(x, y2, w, (int16_t)((y + h) - y2), COL_FLOOR);
    }
}

/* Torches sit above every actor box, so they can flicker without ever
 * colliding with a dirty rect. */
static void draw_torch(int16_t x, uint8_t seed) {
    uint8_t f = (uint8_t)(seed & 3u);
    px_rect(x, 49, 5, 8, COL_BG);
    px_rect((int16_t)(x + 1), 53, 3, 4, COL_RGB(90, 70, 50));
    px_rect((int16_t)(x + 1 - (f & 1)), (int16_t)(49 + (f >> 1)), 3, 4,
            (f & 1) ? COL_TORCH : COL_RGB(255, 210, 90));
}

static void draw_scene_bg(void) {
    display_fill(COL_BG);
    px_rect(0, SCENE_Y, LCD_WIDTH, 2, COL_WALL);
    /* A few wall bricks give the band depth for ~200 px of cost. */
    for(int16_t x = 0; x < LCD_WIDTH; x = (int16_t)(x + 22))
        px_rect(x, (int16_t)(SCENE_Y + 2), 1, 5, COL_WALL);
    px_rect(0, GROUND_Y, LCD_WIDTH, (int16_t)(SCENE_Y + SCENE_H - GROUND_Y), COL_FLOOR);
    px_rect(0, GROUND_Y, LCD_WIDTH, 1, COL_WALL);
    draw_torch(6, 0);
    draw_torch(117, 2);
}

/* ===================================================================
 * Particles
 * =================================================================== */
static void burst(int16_t x, int16_t y, uint8_t n, uint16_t col, int16_t spread) {
    for(uint8_t i = 0; i < NPART && n; i++) {
        if(g_part[i].life) continue;
        g_part[i].x = (int16_t)(x << 6);
        g_part[i].y = (int16_t)(y << 6);
        g_part[i].vx = (int16_t)((int16_t)rnd((uint32_t)(spread * 2)) - spread);
        g_part[i].vy = (int16_t)(-(int16_t)rnd(70) - 20);
        g_part[i].ox = -1;
        g_part[i].life = (uint8_t)(6 + rnd(6));
        g_part[i].col = col;
        n--;
    }
}

static void parts_clear(void) {
    for(uint8_t i = 0; i < NPART; i++)
        if(g_part[i].life && g_part[i].ox >= 0)
            scene_clear(g_part[i].ox, g_part[i].oy, 2, 2);
}

static void parts_step_draw(void) {
    for(uint8_t i = 0; i < NPART; i++) {
        if(!g_part[i].life) continue;
        g_part[i].x = (int16_t)(g_part[i].x + g_part[i].vx);
        g_part[i].y = (int16_t)(g_part[i].y + g_part[i].vy);
        g_part[i].vy = (int16_t)(g_part[i].vy + 9);
        if(--g_part[i].life == 0) { g_part[i].ox = -1; continue; }
        int16_t px = (int16_t)(g_part[i].x >> 6), py = (int16_t)(g_part[i].y >> 6);
        if(py >= GROUND_Y - 1) { g_part[i].life = 0; g_part[i].ox = -1; continue; }
        g_part[i].ox = px; g_part[i].oy = py;
        px_rect(px, py, 2, 2, g_part[i].col);
    }
}

/* ===================================================================
 * HUD
 * =================================================================== */
static void draw_hp_bar(void) {
    px_rect(4, HP_Y, 84, 7, COL_TRACK);
    uint32_t w  = ((uint32_t)g_hp * 84u) / (g_maxhp ? g_maxhp : 1u);
    uint32_t wg = ((uint32_t)g_hp_ghost * 84u) / (g_maxhp ? g_maxhp : 1u);
    if(wg > w) px_rect((int16_t)(4 + w), HP_Y, (int16_t)(wg - w), 7, COL_GHOST);
    if(w) px_rect(4, HP_Y, (int16_t)w, 7, (g_hp * 4u <= g_maxhp) ? COL_HPLOW : COL_HP);
}

static uint16_t g_stam_w = 0xFFFFu;
static void draw_stam_bar(void) {
    int32_t s = g_stam < 0 ? 0 : g_stam;
    uint16_t w = (uint16_t)(((uint32_t)s * 84u) / (uint32_t)g_stammax);
    uint16_t col = (s * 4 <= g_stammax) ? COL_STAMLOW : COL_STAM;
    if(g_stam_w > 84u) g_stam_w = 84u;
    if(w == g_stam_w) return;
    if(w > g_stam_w) px_rect((int16_t)(4 + g_stam_w), STAM_Y, (int16_t)(w - g_stam_w), 5, col);
    else             px_rect((int16_t)(4 + w), STAM_Y, (int16_t)(g_stam_w - w), 5, COL_TRACK);
    if(w) px_rect(4, STAM_Y, (int16_t)w, 5, col);
    g_stam_w = w;
}
static void reset_stam_bar(void) {
    px_rect(4, STAM_Y, 84, 5, COL_TRACK);
    g_stam_w = 0;
    draw_stam_bar();
}

static void draw_stats(void) {
    char b[4];
    uint8_t n = num_str(b, g_floor);
    px_rect(90, HP_Y - 1, 38, 20, COL_BG);
    draw_text("F", 91, HP_Y, 2, COL_DIM);
    draw_span(b, n, 100, HP_Y, 2, COL_INK);
    n = num_str(b, g_atk);
    draw_text("A", 91, STAM_Y, 1, COL_DIM);
    draw_span(b, n, 97, STAM_Y, 1, COL_GOLD);
    n = num_str(b, g_armor);
    draw_text("D", 110, STAM_Y, 1, COL_DIM);
    draw_span(b, n, 116, STAM_Y, 1, COL_STAM);
}

static void draw_bars(void) { draw_hp_bar(); reset_stam_bar(); draw_stats(); }

static void draw_ebar(void) {
    if(g_ehp == 0u && g_ehp_ghost == 0u) { px_rect(0, EBAR_Y - 1, LCD_WIDTH, 11, COL_BG); return; }
    px_rect(4, (int16_t)(EBAR_Y + 5), 120, 5, COL_TRACK);
    uint32_t w  = ((uint32_t)g_ehp * 120u) / (g_emaxhp ? g_emaxhp : 1u);
    uint32_t wg = ((uint32_t)g_ehp_ghost * 120u) / (g_emaxhp ? g_emaxhp : 1u);
    if(wg > w) px_rect((int16_t)(4 + w), (int16_t)(EBAR_Y + 5), (int16_t)(wg - w), 5, COL_GHOST);
    if(w) px_rect(4, (int16_t)(EBAR_Y + 5), (int16_t)w, 5, COL_EHP);
}

static void draw_ename(void) {
    px_rect(0, EBAR_Y - 1, LCD_WIDTH, 6, COL_BG);
    draw_text(g_foe[g_ftype].name, 4, EBAR_Y, 1, COL_DIM);
    if(g_elite) draw_text("ELITE", 100, EBAR_Y, 1, COL_GOLD);
}

/* Damage numbers rise and shrink inside their own strip, so clearing one is
 * a single flat fill and they never fight the sprites for pixels. */
#define FLOAT_X (ENEMY_X - 2)
static void draw_float(void) {
    px_rect(FLOAT_X, FLOAT_Y, LCD_WIDTH - FLOAT_X, 11, COL_BG);
    if(!g_float_n) return;
    char b[4];
    uint8_t n = num_str(b, g_float_n);
    int16_t rise = (int16_t)(g_float_f > 8 ? 0 : (8 - g_float_f));
    draw_span(b, n, (uint16_t)(ENEMY_X + 2), (uint16_t)(FLOAT_Y + rise), 2,
              g_float_crit ? COL_GOLD : COL_INK);
    if(g_float_crit) draw_text("CRIT", (uint16_t)(ENEMY_X + 26),
                               (uint16_t)(FLOAT_Y + 2 + rise), 1, COL_GOLD);
}

static void pop_float(uint8_t dmg, uint8_t crit) {
    g_float_n = dmg; g_float_crit = crit; g_float_f = 16;
    draw_float();
}

static void draw_msg(const char* a, const char* b, uint16_t col) {
    px_rect(0, MSG_Y - 2, LCD_WIDTH, 24, COL_BG);
    if(a) draw_text_mid(a, MSG_Y, 2, col);
    if(b) draw_text_mid(b, (uint16_t)(MSG_Y + 13), 1, COL_DIM);
}
static void show_msg(const char* a, const char* b, uint16_t col) {
    g_msg_t0 = ms_now();
    draw_msg(a, b, col);
}

/* ===================================================================
 * Hero
 * =================================================================== */
static int16_t shake_off(void) {
    if(!g_shake) return 0;
    return (int16_t)((g_shake & 1u) ? 2 : -2);
}

static void draw_hero(void) {
    scene_clear(HB_X, HB_Y, HB_W, HB_H);

    int16_t sx = shake_off();
    int16_t x  = (int16_t)(HERO_X + g_hx + sx);
    int16_t base = GROUND_Y;
    int16_t bob = (int16_t)((g_bob < 2u && !g_swing_f && !g_guard) ? -1 : 0);
    base = (int16_t)(base + bob);

    uint16_t body = g_h_flash ? COL_FLASH : (g_broken ? COL_HPLOW : COL_HERO);
    uint16_t dark = g_h_flash ? COL_FLASH : COL_HEROD;

    /* A lean sells intent before the limbs do. */
    int16_t lean = g_swing_f ? ((g_swing_f <= 2u) ? 3 : 1) : (g_guard ? -2 : 0);
    int16_t bx = (int16_t)(x + lean);

    px_rect((int16_t)(bx + 2), (int16_t)(base - 8), 4, 8, dark);
    px_rect((int16_t)(bx + 8), (int16_t)(base - 8), 4, 8, dark);
    px_rect((int16_t)(bx + 1), (int16_t)(base - 21), 12, 13, body);
    px_rect((int16_t)(bx + 3), (int16_t)(base - 30), 9, 9, body);
    px_rect((int16_t)(bx + 9), (int16_t)(base - 27), 3, 2, COL_BG);

    int16_t px_ = (int16_t)(bx + 13), py = (int16_t)(base - 22);

    if(g_broken) {
        draw_text("BROKEN", (int16_t)(HB_X + 2), (int16_t)(HB_Y + 2), 1, COL_HPLOW);
        return;
    }

    if(g_guard) {
        /* Stamina IS the shield's fill level — the resource sits where the
         * player is already looking instead of in a bar they have to check. */
        int16_t sh = 20, sw = 6;
        int16_t syt = (int16_t)(base - 28);
        int32_t fill = ((int32_t)(g_stam < 0 ? 0 : g_stam) * sh) / g_stammax;
        uint16_t gc = (g_stam * 4 <= g_stammax) ? COL_STAMLOW : COL_STAM;
        px_rect((int16_t)(px_ + 1), syt, sw, sh, COL_TRACK);
        if(fill) px_rect((int16_t)(px_ + 1), (int16_t)(syt + sh - fill), sw,
                         (int16_t)fill, gc);
        px_rect((int16_t)(px_ + 1), syt, sw, 1, gc);
        px_rect((int16_t)(px_ + 1), (int16_t)(syt + sh - 1), sw, 1, gc);
        px_rect(px_, (int16_t)(syt + 5), 1, 10, gc);
        return;
    }

    if(g_swing_f) {
        /* Frame 1 is already fully extended and carries an arc smear behind
         * it, so the swing reads as motion without costing input latency. */
        static const uint8_t blade_a[6] = { 0, 0, 1, 2, 3, 4 };
        uint8_t a = blade_a[g_swing_f];
        int16_t r0 = 5, r1 = (int16_t)(g_swing_f <= 2u ? 19 : 15);
        draw_line((int16_t)(px_ + (r0 * icos(a) >> 7)),
                  (int16_t)(py + (r0 * isin(a) >> 7)),
                  (int16_t)(px_ + (r1 * icos(a) >> 7)),
                  (int16_t)(py + (r1 * isin(a) >> 7)), 2, COL_BLADE);
        if(g_swing_f == 1u) draw_arc(px_, py, 19, 14, 2, 2, COL_BLADE);
        if(g_swing_f == 2u) draw_arc(px_, py, 17, 15, 2, 2, COL_HEROD);
        return;
    }

    draw_line(px_, (int16_t)(py + 2), px_, (int16_t)(py + 15), 2, COL_BLADE);
}

/* ===================================================================
 * Enemy
 * =================================================================== */
static void draw_foe(void) {
    scene_clear(FB_X, FB_Y, FB_W, FB_H);
    if(g_ehp == 0u && !g_dying) return;

    int16_t sx = shake_off();
    int16_t x  = (int16_t)(ENEMY_X + g_ex + sx);
    int16_t base = GROUND_Y;

    uint16_t col = g_foe[g_ftype].col;
    if(g_e_flash) col = COL_FLASH;
    else if(g_phase == EP_WIND) col = g_is_break ? COL_BREAK : COL_WARN;

    /* Death: the body sinks into the floor over ~10 frames. */
    int16_t sink = 0;
    if(g_dying) {
        sink = (int16_t)((10 - (int16_t)g_death_f) * 2);
        if(g_death_f < 5u) col = COL_HEROD;
    }
    base = (int16_t)(base + sink);

    /* Charging pulls back and compresses; it is the tell you feel before you
     * read the colour. */
    int16_t sq = 0;
    if(g_phase == EP_WIND) {
        uint16_t el = (uint16_t)(ms_now() - g_ph_t0);
        int16_t pct = (int16_t)((el * 100u) / (g_ph_dur ? g_ph_dur : 1u));
        if(pct > 100) pct = 100;
        sq = (int16_t)(pct / 34);
    } else if(g_bob < 2u) {
        sq = 1;
    }

    switch(g_ftype) {
    case 0: {                                   /* SLIME — squash and stretch */
        int16_t r = (int16_t)((g_elite ? 12 : 10) + sq);
        fill_circle((int16_t)(x + 10), (int16_t)(base - r + 2), r, col);
        px_rect((int16_t)(x + 4), (int16_t)(base - r - 1), 4, 3, COL_BG);
        px_rect((int16_t)(x + 13), (int16_t)(base - r - 1), 4, 3, COL_BG);
        break;
    }
    case 1:                                     /* RAT */
        px_rect(x, (int16_t)(base - 10 + sq), 18, (int16_t)(9 - sq), col);
        fill_circle(x, (int16_t)(base - 11), 6, col);
        px_rect((int16_t)(x - 3), (int16_t)(base - 13), 3, 3, col);
        draw_line((int16_t)(x + 17), (int16_t)(base - 6),
                  (int16_t)(x + 28), (int16_t)(base - 11), 2, col);
        break;
    case 2:                                     /* SKELETON */
        px_rect((int16_t)(x + 4), (int16_t)(base - 19 + sq), 9, (int16_t)(11 - sq), col);
        for(int16_t i = 0; i < 3; i++)
            px_rect((int16_t)(x + 4), (int16_t)(base - 17 + sq + i * 4), 9, 1, COL_BG);
        fill_circle((int16_t)(x + 8), (int16_t)(base - 25 + sq), 7, col);
        px_rect((int16_t)(x + 4), (int16_t)(base - 27 + sq), 3, 3, COL_BG);
        px_rect((int16_t)(x + 10), (int16_t)(base - 27 + sq), 3, 3, COL_BG);
        px_rect((int16_t)(x + 3), (int16_t)(base - 8), 3, 8, col);
        px_rect((int16_t)(x + 11), (int16_t)(base - 8), 3, 8, col);
        draw_line((int16_t)(x + 1), (int16_t)(base - 18),
                  (int16_t)(x - 6), (int16_t)(base - 24), 2, COL_HEROD);
        break;
    case 3:                                     /* ORC */
        px_rect((int16_t)(x + 1), (int16_t)(base - 23 + sq), 20, (int16_t)(15 - sq), col);
        fill_circle((int16_t)(x + 11), (int16_t)(base - 27 + sq), g_elite ? 11 : 9, col);
        px_rect((int16_t)(x + 6), (int16_t)(base - 29 + sq), 3, 3, COL_BREAK);
        px_rect((int16_t)(x + 14), (int16_t)(base - 29 + sq), 3, 3, COL_BREAK);
        px_rect((int16_t)(x + 2), (int16_t)(base - 8), 6, 8, col);
        px_rect((int16_t)(x + 14), (int16_t)(base - 8), 6, 8, col);
        draw_line((int16_t)(x - 1), (int16_t)(base - 20),
                  (int16_t)(x - 8), (int16_t)(base - 28), 3, COL_HEROD);
        break;
    default: {                                  /* WRAITH — hovers, no legs */
        int16_t hov = (int16_t)((g_bob < 2u) ? -2 : 0);
        fill_circle((int16_t)(x + 10), (int16_t)(base - 26 + hov), 8, col);
        px_rect((int16_t)(x + 6), (int16_t)(base - 28 + hov), 3, 3, COL_BG);
        px_rect((int16_t)(x + 12), (int16_t)(base - 28 + hov), 3, 3, COL_BG);
        for(int16_t i = 0; i < 5; i++)
            px_rect((int16_t)(x + 3 + (i & 1)), (int16_t)(base - 22 + hov + i * 3),
                    (int16_t)(15 - i * 2), 2, col);
        break;
    }
    }

    if(g_phase == EP_STUN && !g_dying) {
        draw_text("STUN", (int16_t)(x - 2), (int16_t)(FB_Y + 1), 1, COL_GOLD);
        draw_arc((int16_t)(x + 10), (int16_t)(FB_Y + 6), 12, 12, 4, 1, COL_GOLD);
    }
}

/* ===================================================================
 * Telegraph — the parry window is drawn, so timing is learnable
 * =================================================================== */
static void draw_tele_track(void) {
    px_rect(0, TELE_Y - 2, LCD_WIDTH, TELE_H + 4, COL_BG);
    px_rect(4, TELE_Y, 120, TELE_H, COL_TRACK);
    uint16_t zw = (uint16_t)((120u * PARRY_MS) / (g_ph_dur ? g_ph_dur : 1u));
    if(zw > 120u) zw = 120u;
    g_zone_x = (int16_t)(4 + 120 - zw);
    px_rect(g_zone_x, TELE_Y, (int16_t)zw, TELE_H, COL_ZONE);
    px_rect(g_zone_x, TELE_Y, 1, TELE_H, COL_GOLD);
    g_tele_w = 0;
}

static void draw_tele_fill(uint16_t pct) {
    uint16_t w = (uint16_t)((120u * pct) / 100u);
    if(w <= g_tele_w) return;
    uint16_t col = g_feinted ? COL_FEINT : (g_is_break ? COL_BREAK : COL_WARN);
    /* Inside the parry window the fill brightens — that is the "press now".
     * Split the new slice at the zone edge so this is at most two rects. */
    int16_t a = (int16_t)(4 + g_tele_w), b = (int16_t)(4 + w);
    int16_t z = (g_is_break) ? b : g_zone_x;
    if(z > b) z = b;
    if(z < a) z = a;
    if(z > a) px_rect(a, TELE_Y, (int16_t)(z - a), TELE_H, col);
    if(b > z) px_rect(z, TELE_Y, (int16_t)(b - z), TELE_H, COL_GOLD);
    g_tele_w = w;
}

static void clear_tele(void) {
    px_rect(0, TELE_Y - 2, LCD_WIDTH, TELE_H + 4, COL_BG);
    g_tele_w = 0;
}

/* ===================================================================
 * Menus — partial repaint so a tap feels instant
 * =================================================================== */
static const char* room_name(uint8_t k) {
    switch(k) {
    case RM_FIGHT: return "FIGHT";
    case RM_ELITE: return "ELITE";
    case RM_LOOT:  return "TREASURE";
    case RM_REST:  return "REST";
    default:       return "STAIRS";
    }
}
static const char* room_hint(uint8_t k) {
    switch(k) {
    case RM_FIGHT: return "A FOE";
    case RM_ELITE: return "HARD  GOOD LOOT";
    case RM_LOOT:  return "PICK ONE";
    case RM_REST:  return "HEAL AND REFILL";
    default:       return "GO DEEPER";
    }
}

static void draw_panel(uint8_t i, const char* a, const char* b, uint16_t acol) {
    int16_t y = (int16_t)(54 + i * 42);
    uint8_t on = (i == g_sel);
    px_rect(8, y, 112, 36, on ? COL_WALL : COL_TRACK);
    if(on) {
        px_rect(8, y, 112, 2, COL_SEL_BAR);
        px_rect(8, (int16_t)(y + 34), 112, 2, COL_SEL_BAR);
        px_rect(8, y, 2, 36, COL_SEL_BAR);
        px_rect(118, y, 2, 36, COL_SEL_BAR);
    }
    draw_text_mid(a, (uint16_t)(y + 8), 2, on ? acol : COL_DIM);
    draw_text_mid(b, (uint16_t)(y + 24), 1, on ? COL_INK : COL_DIM);
}

static void draw_doors(uint8_t full) {
    if(full) {
        display_fill(COL_BG);
        draw_bars();
        draw_text_mid("CHOOSE A DOOR", 32, 1, COL_DIM);
        draw_text_mid("TAP SWITCH   HOLD GO", 146, 1, COL_DIM);
    }
    for(uint8_t i = 0; i < 2u; i++)
        draw_panel(i, room_name(g_door[i]), room_hint(g_door[i]),
                   (g_door[i] == RM_STAIR) ? COL_GOLD : COL_INK);
}

static void draw_pick(uint8_t full) {
    if(full) {
        display_fill(COL_BG);
        draw_bars();
        draw_text_mid("TAKE ONE", 32, 1, COL_DIM);
        draw_text_mid("TAP SWITCH   HOLD TAKE", 146, 1, COL_DIM);
    }
    for(uint8_t i = 0; i < 2u; i++)
        draw_panel(i, g_item_name[g_pick[i]], g_item_desc[g_pick[i]], COL_GOLD);
}

/* ===================================================================
 * Combat
 * =================================================================== */
#define FRAME_MS 33u

static void spawn_foe(uint8_t elite) {
    uint8_t pool = (g_floor <= 1u) ? 2u : (g_floor <= 3u) ? 3u
                 : (g_floor <= 6u) ? 4u : 5u;
    g_ftype = (uint8_t)rnd(pool);
    g_elite = elite;

    const Foe* f = &g_foe[g_ftype];
    g_emaxhp = (uint16_t)(f->hp_base + f->hp_per * g_floor);
    g_eatk   = (uint8_t)(f->atk_base + (f->atk_per4 * g_floor) / 4u);
    if(elite) { g_emaxhp = (uint16_t)(g_emaxhp * 2u); g_eatk = (uint8_t)(g_eatk + 2u); }
    g_ehp = g_emaxhp; g_ehp_ghost = g_emaxhp;

    uint16_t cut = (uint16_t)(g_floor * 32u);
    g_ewind  = (f->wind_ms > cut && (uint16_t)(f->wind_ms - cut) > f->wind_min)
                   ? (uint16_t)(f->wind_ms - cut) : f->wind_min;
    g_erecov = f->recover_ms;
    g_egap   = f->gap_ms;
    g_ebreak = f->break_pct;
    g_efeint = f->feint_pct;
    g_ecombo = f->combo_max;

    g_phase = EP_IDLE;
    g_ph_t0 = ms_now();
    g_ph_dur = (uint16_t)(500u + rnd(400));
    g_combo_left = 0; g_followup = 0; g_feint_at = 0; g_feinted = 0;
    g_guard = 0; g_broken = 0; g_dying = 0; g_death_f = 0;
    g_swing_f = 0; g_hitstop = 0; g_shake = 0;
    g_h_flash = 0; g_e_flash = 0; g_hx = 0; g_ex = 0;
    g_float_n = 0;
    for(uint8_t i = 0; i < NPART; i++) g_part[i].life = 0;

    draw_scene_bg();
    draw_bars();
    draw_ename();
    draw_ebar();
    draw_float();
    draw_hero();
    draw_foe();
    clear_tele();
    draw_msg(g_foe[g_ftype].name, elite ? "ELITE" : 0,
             elite ? COL_GOLD : COL_INK);
    g_state = ST_FIGHT;
}

static void hero_hurt(uint16_t dmg, uint8_t big) {
    if(dmg < 1u) dmg = 1u;
    g_hp = (g_hp > dmg) ? (uint16_t)(g_hp - dmg) : 0u;
    g_h_flash = 3;
    g_hx = (int16_t)(-3 - (big ? 3 : 0));
    g_shake = (uint8_t)(big ? 7 : 4);
    g_hitstop = (uint8_t)(big ? HITSTOP_BIG : HITSTOP_HIT);
    burst((int16_t)(HERO_X + 8), (int16_t)(GROUND_Y - 18),
          (uint8_t)(big ? 7 : 4), COL_HPLOW, 90);
}

static void guard_break(void) {
    g_broken = 1; g_guard = 0;
    g_broke_t0 = ms_now();
    g_stam = (int16_t)(g_stammax / 3);
    g_shake = 7; g_hitstop = HITSTOP_BIG;
    burst((int16_t)(HERO_X + 14), (int16_t)(GROUND_Y - 20), 8, COL_STAMLOW, 110);
    show_msg("GUARD BROKEN", "STAMINA RAN OUT", COL_STAMLOW);
}

static void enemy_strike(void) {
    uint16_t dmg = g_eatk;
    if(g_armor) dmg = (dmg > g_armor) ? (uint16_t)(dmg - g_armor) : 1u;

    g_ex = -9;                              /* the lunge lands the hit */

    if(g_broken) {
        hero_hurt((uint16_t)(dmg + dmg / 2u), 1);
        show_msg("PUNISHED", 0, COL_HPLOW);
    } else if(g_is_break) {
        if(g_guard) {
            hero_hurt((uint16_t)(dmg + dmg / 2u + 1u), 1);
            g_stam = (int16_t)(g_stam - STAM_BLOCK * 2);
            show_msg("SHATTERED", "RED MEANS RELEASE", COL_BREAK);
        } else {
            hero_hurt((uint16_t)(dmg / 2u), 0);
            show_msg("GRAZED", 0, COL_DIM);
        }
    } else if(g_guard) {
        uint16_t since = (uint16_t)(ms_now() - g_guard_t0);
        if(since <= PARRY_MS) {
            g_stam = (int16_t)(g_stam + STAM_PARRY);
            if(g_stam > g_stammax) g_stam = g_stammax;
            g_phase = EP_STUN; g_ph_t0 = ms_now(); g_ph_dur = 1200u;
            g_combo_left = 0; g_followup = 0;
            g_e_flash = 3; g_shake = 6; g_hitstop = HITSTOP_BIG;
            g_ex = 8;
            burst((int16_t)(HERO_X + 18), (int16_t)(GROUND_Y - 20), 9, COL_GOLD, 130);
            draw_stam_bar();
            show_msg("PARRY", "STAMINA BACK", COL_GOLD);
            return;
        }
        g_hp = (g_hp > 1u) ? (uint16_t)(g_hp - 1u) : 0u;
        g_stam = (int16_t)(g_stam - STAM_BLOCK);
        g_shake = 3; g_hitstop = HITSTOP_HIT;
        burst((int16_t)(HERO_X + 16), (int16_t)(GROUND_Y - 20), 4, COL_STAM, 90);
        show_msg("BLOCK", 0, COL_STAM);
    } else {
        hero_hurt(dmg, 0);
        show_msg("HIT", 0, COL_HPLOW);
    }

    if(g_stam <= 0 && !g_broken) { guard_break(); return; }

    if(g_combo_left > 0u) {
        g_combo_left--;
        g_followup = 1;
        g_phase = EP_IDLE; g_ph_t0 = ms_now(); g_ph_dur = g_egap;
    } else {
        g_followup = 0;
        g_phase = EP_REC; g_ph_t0 = ms_now(); g_ph_dur = g_erecov;
    }
}

static void hero_swing(void) {
    uint16_t dmg = g_atk;
    uint8_t open = (g_phase == EP_REC || g_phase == EP_STUN);
    if(open) dmg = (uint16_t)(dmg * 2u);
    g_ehp = (g_ehp > dmg) ? (uint16_t)(g_ehp - dmg) : 0u;

    g_e_flash = 2;
    g_ex = (int16_t)(g_ex + (open ? 7 : 4));    /* knockback */
    g_shake = (uint8_t)(open ? 5 : 2);
    g_hitstop = (uint8_t)(open ? HITSTOP_BIG : HITSTOP_HIT);
    burst((int16_t)(ENEMY_X + 2), (int16_t)(GROUND_Y - 16),
          (uint8_t)(open ? 8 : 5), open ? COL_GOLD : COL_SPARK, 110);
    pop_float((uint8_t)dmg, open);
}

/* ===================================================================
 * Screens
 * =================================================================== */
static void draw_title(void) {
    display_fill(COL_BG);
    draw_text_mid("DEEP", 12, 5, COL_INK);
    draw_text_mid("DARK", 44, 5, COL_GOLD);
    px_rect(24, 76, 80, 1, COL_WALL);
    draw_text_mid("TAP ATTACK", 84, 2, COL_INK);
    draw_text_mid("HOLD GUARD", 100, 2, COL_STAM);
    draw_text_mid("GUARD BURNS STAMINA", 120, 1, COL_STAMLOW);
    draw_text_mid("RED CANNOT BE BLOCKED", 131, 1, COL_BREAK);
    if(g_best) {
        char b[4];
        uint8_t n = num_str(b, g_best);
        draw_text("BEST F", 38, 150, 1, COL_DIM);
        draw_span(b, n, 76, 148, 2, COL_GOLD);
    }
}

static void draw_dead(void) {
    display_fill(COL_BG);
    draw_text_mid("YOU DIED", 30, 3, COL_HPLOW);
    char b[4];
    uint8_t n = num_str(b, g_floor);
    draw_text_mid("FLOOR", 70, 2, COL_DIM);
    draw_span_mid(b, n, 86, 6, COL_INK);
    if(g_floor >= g_best) draw_text_mid("DEEPEST YET", 132, 1, COL_GOLD);
    draw_text_mid("TAP", 148, 1, COL_DIM);
}

static void offer_doors(void);

static void run_begin(void) {
    g_seed ^= ((uint32_t)ms_now() << 11) ^ 0xDEADBEEFUL;
    g_maxhp = 24; g_hp = g_maxhp; g_hp_ghost = g_maxhp;
    g_stammax = STAM_MAX; g_stam = g_stammax;
    g_atk = 3; g_armor = 0; g_floor = 1;
    g_rooms = (uint8_t)(3u + rnd(2)); g_room = 0;
    offer_doors();
}

static uint8_t roll_room(void) {
    uint32_t r = rnd(100);
    if(r < 44u) return RM_FIGHT;
    if(r < 62u) return RM_ELITE;
    if(r < 84u) return RM_LOOT;
    return RM_REST;
}

static void offer_doors(void) {
    if(g_room >= g_rooms) {
        g_door[0] = RM_STAIR;
        g_door[1] = (rnd(2)) ? RM_LOOT : RM_REST;
    } else {
        g_door[0] = roll_room();
        do { g_door[1] = roll_room(); } while(g_door[1] == g_door[0]);
    }
    g_sel = 0;
    g_ehp = 0; g_ehp_ghost = 0;
    draw_doors(1);
    g_state = ST_DOORS;
}

static void offer_loot(void) {
    g_pick[0] = (uint8_t)rnd(IT_COUNT);
    do { g_pick[1] = (uint8_t)rnd(IT_COUNT); } while(g_pick[1] == g_pick[0]);
    g_sel = 0;
    draw_pick(1);
    g_state = ST_PICK;
}

static void take_item(uint8_t it) {
    switch(it) {
    case IT_ATK:   g_atk++; break;
    case IT_ARMOR: g_armor++; break;
    case IT_MAXHP: g_maxhp = (uint16_t)(g_maxhp + 6u); g_hp = (uint16_t)(g_hp + 6u); break;
    case IT_HEAL:  g_hp = g_maxhp; break;
    default:       g_stammax = (int16_t)(g_stammax + 25); g_stam = g_stammax; break;
    }
    if(g_hp > g_maxhp) g_hp = g_maxhp;
    if(g_hp_ghost < g_hp) g_hp_ghost = g_hp;
}

static void enter_room(uint8_t kind) {
    switch(kind) {
    case RM_STAIR:
        g_floor++;
        if(g_floor > g_best) { g_best = g_floor; nv_write(NV_KEY_BEST, g_best); }
        g_rooms = (uint8_t)(3u + rnd(2));
        g_room  = 0;
        display_fill(COL_BG);
        draw_bars();
        draw_text_mid("FLOOR", 66, 2, COL_DIM);
        { char b[4]; uint8_t n = num_str(b, g_floor); draw_span_mid(b, n, 82, 6, COL_GOLD); }
        g_msg_t0 = ms_now();
        g_state = ST_MSG;
        break;
    case RM_LOOT:
        offer_loot();
        break;
    case RM_REST:
        g_hp = (uint16_t)(g_hp + g_maxhp / 2u);
        if(g_hp > g_maxhp) g_hp = g_maxhp;
        if(g_hp_ghost < g_hp) g_hp_ghost = g_hp;
        g_stam = g_stammax;
        display_fill(COL_BG);
        draw_bars();
        fill_circle(64, 84, 16, COL_TORCH);
        fill_circle(64, 84, 9, COL_RGB(255, 230, 140));
        draw_text_mid("REST", 108, 2, COL_INK);
        draw_text_mid("HEALED AND REFILLED", 126, 1, COL_DIM);
        g_msg_t0 = ms_now();
        g_state = ST_MSG;
        break;
    case RM_ELITE: spawn_foe(1); break;
    default:       spawn_foe(0); break;
    }
}

static void room_done(void) { g_room++; offer_doors(); }

/* ===================================================================
 * Framework
 * =================================================================== */
static void on_hard_reset(void) { g_state = ST_TITLE; draw_title(); }

void app_init(void) {
    display_recover();
    app_set_sleep_timeout(45000);
    app_set_hold_reset(10000, on_hard_reset);
    g_best = (uint8_t)nv_read(NV_KEY_BEST, 0);
    if(g_best > 99u) g_best = 0;
    g_bat_raw = bat_read_raw();
    g_stammax = STAM_MAX;
    g_state = ST_TITLE;
    display_fill(COL_BLACK);
    draw_title();
}

/* One signature per actor: redraw only when the pose it encodes changes.
 * Without this both boxes repaint every frame and the fight drops frames. */
static uint16_t hero_sig(void) {
    return (uint16_t)((uint16_t)(g_hx + 16) | ((uint16_t)g_swing_f << 6)
         | ((uint16_t)g_guard << 9) | ((uint16_t)g_broken << 10)
         | ((uint16_t)(g_h_flash ? 1u : 0u) << 11)
         | ((uint16_t)(g_bob < 2u) << 12)
         | ((uint16_t)(g_shake & 1u) << 13)
         | ((uint16_t)((g_stam * 4) / (g_stammax + 1)) << 14));
}
static uint16_t foe_sig(void) {
    uint16_t sq = 0;
    if(g_phase == EP_WIND) {
        uint16_t el = (uint16_t)(ms_now() - g_ph_t0);
        uint16_t p = (uint16_t)((el * 100u) / (g_ph_dur ? g_ph_dur : 1u));
        sq = (uint16_t)((p > 100u ? 100u : p) / 34u);
    }
    return (uint16_t)((uint16_t)(g_ex + 16) | ((uint16_t)g_phase << 6)
         | ((uint16_t)(g_e_flash ? 1u : 0u) << 8)
         | ((uint16_t)(g_bob < 2u) << 9)
         | ((uint16_t)(g_shake & 1u) << 10)
         | (sq << 11) | ((uint16_t)(g_death_f & 3u) << 13));
}

void app_update(uint32_t frame) {
    uint8_t btn = button_raw(), tap = 0, hold = 0;
    if(btn && !g_pressing) {
        g_pressing = 1; g_press_used = 0; g_press_t0 = ms_now();
    } else if(btn && g_pressing) {
        if(!g_press_used && (uint16_t)(ms_now() - g_press_t0) >= GUARD_ARM) {
            hold = 1; g_press_used = 1;
        }
    } else if(!btn && g_pressing) {
        g_pressing = 0;
        if(!g_press_used && (uint16_t)(ms_now() - g_press_t0) < TAP_MAX) tap = 1;
    }
    uint8_t want_guard = (btn && g_press_used) ? 1u : 0u;

    switch(g_state) {

    case ST_TITLE:
        if(tap || hold) run_begin();
        break;

    case ST_DOORS:
        if(hold) enter_room(g_door[g_sel]);
        else if(tap) { g_sel ^= 1u; draw_doors(0); }
        break;

    case ST_PICK:
        if(hold) {
            take_item(g_pick[g_sel]);
            display_fill(COL_BG);
            draw_bars();
            draw_text_mid(g_item_name[g_pick[g_sel]], 70, 2, COL_GOLD);
            draw_text_mid(g_item_desc[g_pick[g_sel]], 92, 1, COL_INK);
            g_msg_t0 = ms_now();
            g_state = ST_MSG;
        } else if(tap) { g_sel ^= 1u; draw_pick(0); }
        break;

    case ST_MSG:
        if((uint16_t)(ms_now() - g_msg_t0) >= 1000u) room_done();
        break;

    case ST_DEAD:
        if(tap || hold) { g_state = ST_TITLE; draw_title(); }
        break;

    case ST_FIGHT: {
        /* ── hitstop: the world stops, the flash does not ──────────
         * Every timer is pushed forward by one frame so a freeze never
         * secretly advances the enemy's wind-up. */
        if(g_hitstop) {
            g_hitstop--;
            g_ph_t0    = (uint16_t)(g_ph_t0 + FRAME_MS);
            g_swing_t0 = (uint16_t)(g_swing_t0 + FRAME_MS);
            g_guard_t0 = (uint16_t)(g_guard_t0 + FRAME_MS);
            g_broke_t0 = (uint16_t)(g_broke_t0 + FRAME_MS);
            g_msg_t0   = (uint16_t)(g_msg_t0 + FRAME_MS);
            if(g_h_flash || g_e_flash) { draw_hero(); draw_foe(); }
            if(g_h_flash) g_h_flash--;
            if(g_e_flash) g_e_flash--;
            break;
        }

        if((frame & 7u) == 0u) g_bob = (uint8_t)((g_bob + 1u) & 3u);
        if(g_shake) g_shake--;
        if(g_h_flash) g_h_flash--;
        if(g_e_flash) g_e_flash--;
        /* Offsets ease home by halving — cheap, and reads as weight. */
        if(g_hx) g_hx = (int16_t)(g_hx - (g_hx > 0 ? (g_hx + 1) / 2 : (g_hx - 1) / 2));
        if(g_ex) g_ex = (int16_t)(g_ex - (g_ex > 0 ? (g_ex + 1) / 2 : (g_ex - 1) / 2));

        /* ── death ────────────────────────────────────────────── */
        if(g_dying) {
            parts_clear();
            if(g_death_f) {
                g_death_f--;
                draw_foe();
                parts_step_draw();
                if(g_ehp_ghost) { g_ehp_ghost = (uint16_t)(g_ehp_ghost > 1u ? g_ehp_ghost - 2u : 0u); draw_ebar(); }
                break;
            }
            g_dying = 0;
            draw_foe();
            px_rect(0, EBAR_Y - 1, LCD_WIDTH, 11, COL_BG);
            show_msg("SLAIN", 0, COL_GOLD);
            g_state = ST_MSG;
            break;
        }

        /* ── guard and its cost ───────────────────────────────── */
        if(g_broken) {
            if((uint16_t)(ms_now() - g_broke_t0) >= BREAK_MS) g_broken = 0;
        } else {
            if(want_guard != g_guard) {
                g_guard = want_guard;
                if(g_guard) g_guard_t0 = ms_now();
            }
            if(g_guard) {
                g_stam -= STAM_DRAIN;
                if(g_stam <= 0) { g_stam = 0; guard_break(); }
                draw_stam_bar();
            } else if(g_stam < g_stammax) {
                g_stam += STAM_REGEN;
                if(g_stam > g_stammax) g_stam = g_stammax;
                draw_stam_bar();
            }
        }

        /* ── swing ────────────────────────────────────────────── */
        if(tap && !g_guard && !g_broken &&
           (uint16_t)(ms_now() - g_swing_t0) >= SWING_CD) {
            g_swing_t0 = ms_now();
            g_swing_f = 1;
            hero_swing();
            if(g_ehp == 0u) {
                g_dying = 1; g_death_f = 10;
                g_shake = 8;
                burst((int16_t)(ENEMY_X + 8), (int16_t)(GROUND_Y - 16), 10,
                      g_foe[g_ftype].col, 130);
                clear_tele();
            }
        } else if(g_swing_f) {
            g_swing_f++;
            if(g_swing_f > 5u) g_swing_f = 0;
        }

        if(g_float_n) {
            g_float_f--;
            if(g_float_f == 0u) { g_float_n = 0; draw_float(); }
            else if(g_float_f > 8u) draw_float();
        }

        /* Health bars drain behind a trailing ghost, so a big hit stays big
         * on screen for a beat after the number is gone. */
        if(g_ehp_ghost > g_ehp) {
            g_ehp_ghost = (uint16_t)(g_ehp_ghost - 1u);
            draw_ebar();
        }
        if(g_hp_ghost > g_hp) {
            g_hp_ghost = (uint16_t)(g_hp_ghost - 1u);
            draw_hp_bar();
        } else if(g_hp_ghost < g_hp) {
            g_hp_ghost = g_hp;
            draw_hp_bar();
        }

        /* ── enemy rhythm ─────────────────────────────────────── */
        uint16_t el = (uint16_t)(ms_now() - g_ph_t0);

        switch(g_phase) {
        case EP_IDLE:
            if(el >= g_ph_dur) {
                if(!g_followup) {
                    uint8_t n = (uint8_t)(1u + rnd(g_ecombo));
                    g_combo_left = (uint8_t)(n - 1u);
                }
                g_is_break = (rnd(100) < g_ebreak) ? 1u : 0u;
                g_feint_at = (rnd(100) < g_efeint) ? 1u : 0u;
                g_feinted  = 0;
                g_phase = EP_WIND;
                g_ph_t0 = ms_now();
                g_ph_dur = g_followup ? (uint16_t)(g_ewind * 3u / 4u) : g_ewind;
                draw_tele_track();
                draw_msg(g_is_break ? "RED" : "WHITE",
                         g_is_break ? "DO NOT GUARD" : "GUARD LATE TO PARRY",
                         g_is_break ? COL_BREAK : COL_WARN);
            }
            break;

        case EP_WIND: {
            uint16_t pct = (uint16_t)((el * 100u) / (g_ph_dur ? g_ph_dur : 1u));
            if(pct > 100u) pct = 100u;

            /* The feint stalls the wind-up and visibly slides the parry zone
             * further out — which is exactly what makes it readable. */
            if(g_feint_at && !g_feinted && pct >= 70u) {
                g_feinted = 1;
                g_ph_dur = (uint16_t)(g_ph_dur + g_ph_dur / 2u);
                draw_tele_track();
                draw_tele_fill((uint16_t)((el * 100u) / g_ph_dur));
                draw_msg("FEINT", "IT HELD BACK", COL_FEINT);
            } else {
                draw_tele_fill(pct);
            }

            if(el >= g_ph_dur) {
                enemy_strike();
                clear_tele();
                if(g_hp == 0u) { g_state = ST_DEAD; draw_dead(); break; }
            }
            break;
        }

        case EP_REC:
        case EP_STUN:
            if(el >= g_ph_dur) {
                g_phase = EP_IDLE;
                g_ph_t0 = ms_now();
                g_ph_dur = (uint16_t)(400u + rnd(400));
                draw_msg(0, 0, COL_INK);
            }
            break;
        }

        if(g_state != ST_FIGHT) break;

        /* ── render ───────────────────────────────────────────── */
        parts_clear();
        {
            static uint16_t last_h = 0xFFFFu, last_f = 0xFFFFu;
            uint16_t hs = hero_sig(), fs = foe_sig();
            if(hs != last_h) { draw_hero(); last_h = hs; }
            if(fs != last_f) { draw_foe();  last_f = fs; }
        }
        parts_step_draw();
        if((frame & 7u) == 0u) { draw_torch(6, (uint8_t)rnd(4)); draw_torch(117, (uint8_t)rnd(4)); }
        break;
    }
    }

    if((frame % 150u) == 0u && g_state != ST_FIGHT) g_bat_raw = bat_read_raw();
}

void app_wake(void) {
    g_pressing = 0;
    g_state = ST_TITLE;
    draw_title();
}
