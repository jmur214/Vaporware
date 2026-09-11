/* crawl.c — one-button dungeon crawler for RAZ DC25000
 *           (N32G031K8Q7-1 + GC9107 128x160 LCD, Vaporware SDK)
 *
 * ── The problem this design exists to solve ──────────────────────────────
 * A one-button fighter collapses instantly if guarding is free.  Hold guard,
 * wait for the recovery window, tap twice, repeat — every fight identical,
 * every enemy a different-coloured wall.  So guard costs STAMINA, and that
 * single rule is what the whole game hangs off:
 *
 *   Guard drains fast and refills slowly, so it is a burst, never a stance.
 *   Run it to zero and your guard BREAKS — stunned, and hit for extra.
 *   A parry refunds stamina, so reading well is what pays for reading again.
 *
 * Now the question stops being "am I holding" and becomes "can I afford to
 * hold, right now, for this long".  Guarding early to be safe is exactly what
 * kills you, which is why enemies FEINT: the wind-up stalls partway and the
 * player who guarded on sight is empty when the hit finally lands.
 *
 * Attacks come in COMBOS of one to three, so recovery windows are earned
 * rather than handed out every few seconds, and stamina has to last a whole
 * exchange rather than one hit.
 *
 * Two attack colours remain the core read — white guards, red must NOT be
 * guarded — but they now appear mid-combo, so a single sequence can demand
 * guard, release, guard again on a rhythm.
 *
 * ── Structure ────────────────────────────────────────────────────────────
 * Between rooms you pick one of two doors, so the run is a series of choices
 * rather than a corridor: fight, elite, treasure or rest.  Treasure offers a
 * choice of two items, which is where a build comes from.  Everything you
 * carry shows on the HUD, because an upgrade you cannot see is not a reward.
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
#define COL_STONE   COL_RGB( 56,  52,  72)
#define COL_FLOOR   COL_RGB( 42,  38,  54)
#define COL_INK     COL_RGB(238, 238, 246)
#define COL_DIM     COL_RGB(126, 122, 146)
#define COL_HP      COL_RGB( 70, 205, 100)
#define COL_HPLOW   COL_RGB(225,  60,  50)
#define COL_STAM    COL_RGB( 80, 165, 255)
#define COL_STAMLOW COL_RGB(255, 150,  40)
#define COL_EHP     COL_RGB(200,  60,  70)
#define COL_TRACK   COL_RGB( 38,  36,  50)
#define COL_WARN    COL_RGB(245, 240, 220)
#define COL_BREAK   COL_RGB(240,  50,  40)
#define COL_FEINT   COL_RGB(150, 140, 110)
#define COL_GOLD    COL_RGB(255, 205,   0)
#define COL_HERO    COL_RGB(210, 215, 235)
#define COL_BLADE   COL_RGB(180, 220, 255)
#define COL_FLASH   COL_RGB(255, 255, 255)
#define COL_SEL     COL_RGB(255, 205,   0)

/* ===================================================================
 * Layout
 * =================================================================== */
#define HP_Y         4
#define STAM_Y      13
#define EBAR_Y      24
#define FLOAT_Y     36
#define SCENE_Y     48
#define SCENE_H     64
#define GROUND_Y   (SCENE_Y + SCENE_H - 10)
#define TELE_Y     118
#define TELE_H      10
#define MSG_Y      134

#define HERO_X      20
#define ENEMY_X     84

/* ===================================================================
 * Tuning — the numbers that define the fight
 * =================================================================== */
#define TAP_MAX      170u
#define GUARD_ARM    170u
#define PARRY_MS     260u
#define SWING_CD     230u

#define STAM_MAX     100
#define STAM_DRAIN     2     /* per frame guarding, ~60/s: 1.7 s of guard */
#define STAM_REGEN     1     /* per frame free,     ~30/s: 3.3 s to refill */
#define STAM_BLOCK    18
#define STAM_PARRY    30
#define BREAK_MS     900u

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

static uint32_t g_seed = 0xD0465EEDUL;
static uint32_t rnd(uint32_t m) {
    g_seed = g_seed * 1664525UL + 1013904223UL;
    return (g_seed >> 16) % m;
}
static void fill_circle(int16_t cx, int16_t cy, int16_t r, uint16_t col) {
    for(int16_t dy = -r; dy <= r; dy++) {
        int16_t y = (int16_t)(cy + dy);
        if(y < 0 || y >= LCD_HEIGHT) continue;
        int16_t h = 0;
        while((int16_t)((h + 1) * (h + 1) + dy * dy) <= (int16_t)(r * r)) h++;
        int16_t x0 = (int16_t)(cx - h), w = (int16_t)(h * 2 + 1);
        if(x0 < 0) { w = (int16_t)(w + x0); x0 = 0; }
        if(x0 + w > LCD_WIDTH) w = (int16_t)(LCD_WIDTH - x0);
        if(w > 0) display_fill_rect((uint16_t)x0, (uint16_t)y, (uint16_t)w, 1, col);
    }
}

/* ===================================================================
 * Bestiary — each entry is a rhythm, not a stat block
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
  { "RAT",      7,  2,  2,  1,  520, 340,  520, 220, 10, 15, 2, COL_RGB(170,120, 80) },
  { "SKELETON",12,  3,  3,  1,  820, 520,  660, 280, 30, 25, 2, COL_RGB(225,228,215) },
  { "ORC",     18,  4,  5,  2, 1180, 780,  920, 360, 45, 20, 2, COL_RGB( 95,150, 85) },
  { "WRAITH",  13,  3,  4,  2,  620, 400,  560, 200, 55, 40, 3, COL_RGB(170,110,235) },
};

/* ===================================================================
 * Items — visible, because an upgrade you cannot see is not a reward
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
static uint16_t g_hp, g_maxhp;
static int16_t  g_stam, g_stammax;
static uint8_t  g_atk, g_armor, g_floor, g_best;
static uint8_t  g_room, g_rooms;

static uint8_t  g_door[2], g_sel;
static uint8_t  g_pick[2];

static uint8_t  g_ftype, g_elite;
static uint16_t g_ehp, g_emaxhp;
static uint8_t  g_eatk;
static uint16_t g_ewind, g_erecov, g_egap;
static uint8_t  g_ebreak, g_efeint, g_ecombo;

static uint8_t  g_phase, g_is_break, g_combo_left, g_followup;
static uint8_t  g_feint_at, g_feinted;
static uint16_t g_ph_t0, g_ph_dur;

static uint8_t  g_guard, g_swinging, g_broken;
static uint16_t g_guard_t0, g_swing_t0, g_broke_t0;

static uint8_t  g_pressing, g_press_used;
static uint16_t g_press_t0;

static uint16_t g_msg_t0;
static uint8_t  g_float_n, g_float_crit;
static uint16_t g_float_t0;

static int16_t  g_drawn_tele = -1;
static uint16_t g_bat_raw = BAT_FULL;

/* ===================================================================
 * Bars
 * =================================================================== */
static void draw_bars(void) {
    display_fill_rect(0, 0, LCD_WIDTH, 22, COL_BG);

    display_fill_rect(4, HP_Y, 84, 7, COL_TRACK);
    uint32_t w = ((uint32_t)g_hp * 84u) / (g_maxhp ? g_maxhp : 1u);
    if(w) display_fill_rect(4, HP_Y, (uint16_t)w, 7,
                            (g_hp * 4u <= g_maxhp) ? COL_HPLOW : COL_HP);

    /* Stamina sits directly under health because it is just as lethal —
     * running it dry is what gets you killed, not chip damage. */
    display_fill_rect(4, STAM_Y, 84, 5, COL_TRACK);
    int32_t s = g_stam < 0 ? 0 : g_stam;
    w = ((uint32_t)s * 84u) / (uint32_t)g_stammax;
    if(w) display_fill_rect(4, STAM_Y, (uint16_t)w, 5,
                            (s * 4 <= g_stammax) ? COL_STAMLOW : COL_STAM);

    char b[4];
    uint8_t n = num_str(b, g_floor);
    draw_text("F", 92, HP_Y, 2, COL_DIM);
    draw_span(b, n, 100, HP_Y, 2, COL_INK);

    n = num_str(b, g_atk);
    draw_text("A", 92, STAM_Y - 1, 1, COL_DIM);
    draw_span(b, n, 98, STAM_Y - 1, 1, COL_GOLD);
    n = num_str(b, g_armor);
    draw_text("D", 110, STAM_Y - 1, 1, COL_DIM);
    draw_span(b, n, 116, STAM_Y - 1, 1, COL_STAM);
}

static void draw_ebar(void) {
    display_fill_rect(0, EBAR_Y - 2, LCD_WIDTH, 12, COL_BG);
    if(g_ehp == 0u) return;
    draw_text(g_foe[g_ftype].name, 4, EBAR_Y, 1, COL_DIM);
    if(g_elite) draw_text("ELITE", 98, EBAR_Y, 1, COL_GOLD);
    display_fill_rect(4, (uint16_t)(EBAR_Y + 6), 120, 4, COL_TRACK);
    uint32_t w = ((uint32_t)g_ehp * 120u) / (g_emaxhp ? g_emaxhp : 1u);
    if(w) display_fill_rect(4, (uint16_t)(EBAR_Y + 6), (uint16_t)w, 4, COL_EHP);
}

/* Damage numbers get a dedicated strip so they never fight the sprites for
 * pixels — clearing one is a single flat fill. */
static void draw_float(void) {
    display_fill_rect(0, FLOAT_Y, LCD_WIDTH, 10, COL_BG);
    if(!g_float_n) return;
    char b[4];
    uint8_t n = num_str(b, g_float_n);
    draw_span(b, n, ENEMY_X + 4, FLOAT_Y, 2, g_float_crit ? COL_GOLD : COL_INK);
    if(g_float_crit) draw_text("CRIT", ENEMY_X + 24, FLOAT_Y + 2, 1, COL_GOLD);
}

static void pop_float(uint8_t dmg, uint8_t crit) {
    g_float_n = dmg; g_float_crit = crit; g_float_t0 = ms_now();
    draw_float();
}

/* ===================================================================
 * Sprites
 * =================================================================== */
static void draw_hero(void) {
    display_fill_rect(HERO_X - 10, SCENE_Y + 2, 42, SCENE_H - 8, COL_BG);
    uint16_t base = GROUND_Y;
    uint16_t c = g_broken ? COL_HPLOW : COL_HERO;

    display_fill_rect(HERO_X + 2, (uint16_t)(base - 8), 4, 8, c);
    display_fill_rect(HERO_X + 8, (uint16_t)(base - 8), 4, 8, c);
    display_fill_rect(HERO_X + 1, (uint16_t)(base - 21), 12, 13, c);
    display_fill_rect(HERO_X + 3, (uint16_t)(base - 30), 9, 9, c);
    display_fill_rect(HERO_X + 9, (uint16_t)(base - 27), 3, 2, COL_BG);

    if(g_broken) {
        draw_text("BROKEN", HERO_X - 8, SCENE_Y + 4, 1, COL_HPLOW);
    } else if(g_guard) {
        uint16_t gc = (g_stam * 4 <= g_stammax) ? COL_STAMLOW : COL_STAM;
        display_fill_rect(HERO_X + 14, (uint16_t)(base - 25), 5, 17, gc);
        display_fill_rect(HERO_X + 13, (uint16_t)(base - 21), 2, 9, gc);
    } else if(g_swinging) {
        display_fill_rect(HERO_X + 14, (uint16_t)(base - 23), 21, 3, COL_BLADE);
        display_fill_rect(HERO_X + 31, (uint16_t)(base - 27), 3, 9, COL_BLADE);
    } else {
        display_fill_rect(HERO_X + 14, (uint16_t)(base - 17), 3, 13, COL_BLADE);
    }
}

static void draw_foe_at(uint16_t col) {
    display_fill_rect(ENEMY_X - 6, SCENE_Y + 2, 44, SCENE_H - 6, COL_BG);
    if(g_ehp == 0u) return;

    uint16_t base = GROUND_Y;
    int16_t lean = (g_phase == EP_WIND) ? 5 : 0;
    uint16_t x = (uint16_t)(ENEMY_X + lean);

    switch(g_ftype) {
    case 0:
        fill_circle((int16_t)(x + 10), (int16_t)(base - 9), g_elite ? 12 : 10, col);
        display_fill_rect((uint16_t)(x + 4), (uint16_t)(base - 12), 4, 3, COL_BG);
        display_fill_rect((uint16_t)(x + 13), (uint16_t)(base - 12), 4, 3, COL_BG);
        break;
    case 1:
        display_fill_rect(x, (uint16_t)(base - 10), 18, 9, col);
        fill_circle((int16_t)x, (int16_t)(base - 11), 6, col);
        display_fill_rect((uint16_t)(x + 16), (uint16_t)(base - 6), 12, 2, col);
        break;
    case 2:
        display_fill_rect((uint16_t)(x + 4), (uint16_t)(base - 19), 9, 11, col);
        fill_circle((int16_t)(x + 8), (int16_t)(base - 25), 7, col);
        display_fill_rect((uint16_t)(x + 4), (uint16_t)(base - 27), 3, 3, COL_BG);
        display_fill_rect((uint16_t)(x + 10), (uint16_t)(base - 27), 3, 3, COL_BG);
        display_fill_rect((uint16_t)(x + 3), (uint16_t)(base - 8), 3, 8, col);
        display_fill_rect((uint16_t)(x + 11), (uint16_t)(base - 8), 3, 8, col);
        break;
    case 3:
        display_fill_rect((uint16_t)(x + 1), (uint16_t)(base - 23), 20, 15, col);
        fill_circle((int16_t)(x + 11), (int16_t)(base - 28), g_elite ? 11 : 9, col);
        display_fill_rect((uint16_t)(x + 6), (uint16_t)(base - 30), 3, 3, COL_BG);
        display_fill_rect((uint16_t)(x + 14), (uint16_t)(base - 30), 3, 3, COL_BG);
        display_fill_rect((uint16_t)(x + 2), (uint16_t)(base - 8), 6, 8, col);
        display_fill_rect((uint16_t)(x + 14), (uint16_t)(base - 8), 6, 8, col);
        break;
    default:
        fill_circle((int16_t)(x + 10), (int16_t)(base - 26), 8, col);
        display_fill_rect((uint16_t)(x + 3), (uint16_t)(base - 24), 15, 16, col);
        display_fill_rect((uint16_t)(x + 3), (uint16_t)(base - 8), 4, 4, col);
        display_fill_rect((uint16_t)(x + 10), (uint16_t)(base - 8), 4, 6, col);
        break;
    }

    if(g_phase == EP_STUN) draw_text("STUN", (uint16_t)(x - 2), SCENE_Y + 4, 1, COL_GOLD);
}

/* The enemy itself carries the tell: it whitens as a normal swing charges and
 * reddens for a breaker, so the read lives on the thing you are watching
 * rather than only on a bar at the bottom of the screen. */
static void draw_foe(void) {
    uint16_t c = g_foe[g_ftype].col;
    if(g_phase == EP_WIND) c = g_is_break ? COL_BREAK : COL_WARN;
    draw_foe_at(c);
}

static void draw_telegraph(int16_t pct, uint8_t breaker, uint8_t stalled) {
    display_fill_rect(0, TELE_Y - 2, LCD_WIDTH, TELE_H + 4, COL_BG);
    if(pct < 0) { g_drawn_tele = -1; return; }
    display_fill_rect(4, TELE_Y, 120, TELE_H, COL_TRACK);
    uint16_t w = (uint16_t)((120u * (uint16_t)pct) / 100u);
    if(w) display_fill_rect(4, TELE_Y, w,
                            TELE_H, stalled ? COL_FEINT : (breaker ? COL_BREAK : COL_WARN));
    g_drawn_tele = pct;
}

static void draw_msg(const char* a, const char* b) {
    display_fill_rect(0, MSG_Y - 2, LCD_WIDTH, 24, COL_BG);
    if(a) draw_text_mid(a, MSG_Y, 2, COL_INK);
    if(b) draw_text_mid(b, (uint16_t)(MSG_Y + 12), 1, COL_DIM);
}

static void show_msg(const char* a, const char* b) {
    g_msg_t0 = ms_now();
    draw_msg(a, b);
}

static void draw_scene_bg(void) {
    display_fill(COL_BG);
    display_fill_rect(0, SCENE_Y, LCD_WIDTH, 2, COL_STONE);
    display_fill_rect(0, GROUND_Y, LCD_WIDTH,
                      (uint16_t)(SCENE_Y + SCENE_H - GROUND_Y), COL_FLOOR);
}

/* ===================================================================
 * Doors — a run of choices instead of a corridor
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
    case RM_ELITE: return "HARD LOOT";
    case RM_LOOT:  return "PICK ONE";
    case RM_REST:  return "HEAL HALF";
    default:       return "GO DEEPER";
    }
}

static void draw_doors(void) {
    display_fill(COL_BG);
    draw_bars();
    draw_text_mid("CHOOSE", 32, 2, COL_DIM);

    for(uint8_t i = 0; i < 2u; i++) {
        uint16_t y = (uint16_t)(54 + i * 42);
        uint8_t on = (i == g_sel);
        display_fill_rect(8, y, 112, 36, on ? COL_STONE : COL_TRACK);
        if(on) {
            display_fill_rect(8, y, 112, 2, COL_SEL);
            display_fill_rect(8, (uint16_t)(y + 34), 112, 2, COL_SEL);
        }
        draw_text_mid(room_name(g_door[i]), (uint16_t)(y + 7), 2,
                      on ? COL_INK : COL_DIM);
        draw_text_mid(room_hint(g_door[i]), (uint16_t)(y + 23), 1, COL_DIM);
    }
    draw_text_mid("TAP SWITCH  HOLD GO", 146, 1, COL_DIM);
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
    draw_doors();
    g_state = ST_DOORS;
}

/* ===================================================================
 * Loot — two options, because picking is where a build comes from
 * =================================================================== */
static void draw_pick(void) {
    display_fill(COL_BG);
    draw_bars();
    draw_text_mid("TAKE ONE", 32, 2, COL_DIM);

    for(uint8_t i = 0; i < 2u; i++) {
        uint16_t y = (uint16_t)(54 + i * 42);
        uint8_t on = (i == g_sel);
        display_fill_rect(8, y, 112, 36, on ? COL_STONE : COL_TRACK);
        if(on) {
            display_fill_rect(8, y, 112, 2, COL_SEL);
            display_fill_rect(8, (uint16_t)(y + 34), 112, 2, COL_SEL);
        }
        draw_text_mid(g_item_name[g_pick[i]], (uint16_t)(y + 7), 1,
                      on ? COL_GOLD : COL_DIM);
        draw_text_mid(g_item_desc[g_pick[i]], (uint16_t)(y + 20), 1, COL_DIM);
    }
    draw_text_mid("TAP SWITCH  HOLD TAKE", 146, 1, COL_DIM);
}

static void offer_loot(void) {
    g_pick[0] = (uint8_t)rnd(IT_COUNT);
    do { g_pick[1] = (uint8_t)rnd(IT_COUNT); } while(g_pick[1] == g_pick[0]);
    g_sel = 0;
    draw_pick();
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
}

/* ===================================================================
 * Combat
 * =================================================================== */
static void spawn_foe(uint8_t elite) {
    uint8_t pool = (g_floor <= 1u) ? 2u : (g_floor <= 3u) ? 3u
                 : (g_floor <= 6u) ? 4u : 5u;
    g_ftype = (uint8_t)rnd(pool);
    g_elite = elite;

    const Foe* f = &g_foe[g_ftype];
    g_emaxhp = (uint16_t)(f->hp_base + f->hp_per * g_floor);
    g_eatk   = (uint8_t)(f->atk_base + (f->atk_per4 * g_floor) / 4u);
    if(elite) { g_emaxhp = (uint16_t)(g_emaxhp * 2u); g_eatk = (uint8_t)(g_eatk + 2u); }
    g_ehp = g_emaxhp;

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
    g_ph_dur = (uint16_t)(400u + rnd(460));
    g_combo_left = 0; g_followup = 0; g_feint_at = 0; g_feinted = 0;
    g_guard = 0; g_swinging = 0; g_broken = 0;
    g_float_n = 0;

    draw_scene_bg();
    draw_bars();
    draw_ebar();
    draw_float();
    draw_hero();
    draw_foe();
    draw_telegraph(-1, 0, 0);
    draw_msg(g_foe[g_ftype].name, elite ? "ELITE" : 0);
    g_state = ST_FIGHT;
}

static void hero_hurt(uint16_t dmg) {
    if(dmg < 1u) dmg = 1u;
    g_hp = (g_hp > dmg) ? (uint16_t)(g_hp - dmg) : 0u;
    draw_bars();
}

static void guard_break(void) {
    g_broken = 1;
    g_guard  = 0;
    g_broke_t0 = ms_now();
    g_stam = g_stammax / 3;
    draw_hero();
    draw_bars();
    show_msg("GUARD BROKEN", "STAMINA RAN OUT");
}

static void enemy_strike(void) {
    uint16_t dmg = g_eatk;
    if(g_armor) dmg = (dmg > g_armor) ? (uint16_t)(dmg - g_armor) : 1u;

    if(g_broken) {
        hero_hurt((uint16_t)(dmg + dmg / 2u));
        show_msg("PUNISHED", 0);
    } else if(g_is_break) {
        if(g_guard) {
            hero_hurt((uint16_t)(dmg + dmg / 2u + 1u));
            g_stam -= STAM_BLOCK * 2;
            show_msg("BROKEN", "RED MEANS RELEASE");
        } else {
            hero_hurt((uint16_t)(dmg / 2u));
            show_msg("GRAZED", 0);
        }
    } else if(g_guard) {
        uint16_t since = (uint16_t)(ms_now() - g_guard_t0);
        if(since <= PARRY_MS) {
            /* Parry refunds stamina, so reading well is what funds the next
             * read — the skill loop pays for itself. */
            g_stam += STAM_PARRY;
            if(g_stam > g_stammax) g_stam = g_stammax;
            g_phase = EP_STUN; g_ph_t0 = ms_now(); g_ph_dur = 1200u;
            g_combo_left = 0; g_followup = 0;
            draw_bars();
            show_msg("PARRY", "STAMINA BACK");
            return;
        }
        hero_hurt(1u);
        g_stam -= STAM_BLOCK;
        show_msg("BLOCK", 0);
    } else {
        hero_hurt(dmg);
        show_msg("HIT", 0);
    }

    if(g_stam <= 0 && !g_broken) { guard_break(); return; }
    draw_bars();

    /* A combo keeps coming: short gap, then the next swing.  Recovery has to
     * be earned by surviving the whole sequence. */
    if(g_combo_left > 0u) {
        g_combo_left--;
        g_followup = 1;
        g_phase = EP_IDLE;
        g_ph_t0 = ms_now();
        g_ph_dur = g_egap;
    } else {
        g_followup = 0;
        g_phase = EP_REC;
        g_ph_t0 = ms_now();
        g_ph_dur = g_erecov;
    }
}

static void hero_swing(void) {
    uint16_t dmg = g_atk;
    uint8_t open = (g_phase == EP_REC || g_phase == EP_STUN);
    if(open) dmg = (uint16_t)(dmg * 2u);
    g_ehp = (g_ehp > dmg) ? (uint16_t)(g_ehp - dmg) : 0u;
    draw_ebar();
    pop_float((uint8_t)dmg, open);
}

/* ===================================================================
 * Screens
 * =================================================================== */
static void draw_title(void) {
    display_fill(COL_BG);
    draw_text_mid("DEEP", 14, 5, COL_INK);
    draw_text_mid("DARK", 46, 5, COL_GOLD);
    draw_text_mid("TAP ATTACK", 84, 2, COL_INK);
    draw_text_mid("HOLD GUARD", 100, 2, COL_STAM);
    draw_text_mid("GUARD BURNS STAMINA", 120, 1, COL_STAMLOW);
    draw_text_mid("RED CANNOT BE BLOCKED", 132, 1, COL_BREAK);
    if(g_best) {
        char b[4];
        uint8_t n = num_str(b, g_best);
        draw_text("BEST F", 38, 150, 1, COL_DIM);
        draw_span(b, n, 76, 148, 2, COL_GOLD);
    }
}

static void draw_dead(void) {
    display_fill(COL_BG);
    draw_text_mid("YOU DIED", 34, 3, COL_HPLOW);
    char b[4];
    uint8_t n = num_str(b, g_floor);
    draw_text_mid("FLOOR", 72, 2, COL_DIM);
    draw_span_mid(b, n, 88, 6, COL_INK);
    if(g_floor >= g_best) draw_text_mid("DEEPEST YET", 132, 1, COL_GOLD);
    draw_text_mid("TAP", 148, 1, COL_DIM);
}

static void run_begin(void) {
    g_seed ^= ((uint32_t)ms_now() << 11) ^ 0xDEADBEEFUL;
    g_maxhp = 24; g_hp = g_maxhp;
    g_stammax = STAM_MAX; g_stam = g_stammax;
    g_atk = 3; g_armor = 0; g_floor = 1;
    g_rooms = (uint8_t)(3u + rnd(2)); g_room = 0;
    offer_doors();
}

static void enter_room(uint8_t kind) {
    switch(kind) {
    case RM_STAIR:
        g_floor++;
        if(g_floor > g_best) { g_best = g_floor; nv_write(NV_KEY_BEST, g_best); }
        g_rooms = (uint8_t)(3u + rnd(2));
        g_room  = 0;
        offer_doors();
        break;
    case RM_LOOT:
        offer_loot();
        break;
    case RM_REST:
        g_hp = (uint16_t)(g_hp + g_maxhp / 2u);
        if(g_hp > g_maxhp) g_hp = g_maxhp;
        g_stam = g_stammax;
        display_fill(COL_BG);
        draw_bars();
        fill_circle(64, 86, 20, COL_STAM);
        show_msg("REST", "HEALED");
        g_state = ST_MSG;
        break;
    case RM_ELITE: spawn_foe(1); break;
    default:       spawn_foe(0); break;
    }
}

static void room_done(void) {
    g_room++;
    offer_doors();
}

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
    g_state = ST_TITLE;
    display_fill(COL_BLACK);
    draw_title();
}

void app_update(uint32_t frame) {
    (void)frame;

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
        if(hold) { uint8_t k = g_door[g_sel]; enter_room(k); }
        else if(tap) { g_sel ^= 1u; draw_doors(); }
        break;

    case ST_PICK:
        if(hold) {
            take_item(g_pick[g_sel]);
            draw_bars();
            display_fill(COL_BG);
            draw_bars();
            draw_text_mid(g_item_name[g_pick[g_sel]], 70, 2, COL_GOLD);
            draw_text_mid(g_item_desc[g_pick[g_sel]], 92, 1, COL_DIM);
            g_msg_t0 = ms_now();
            g_state = ST_MSG;
        } else if(tap) { g_sel ^= 1u; draw_pick(); }
        break;

    case ST_MSG:
        if((uint16_t)(ms_now() - g_msg_t0) >= 1000u) room_done();
        break;

    case ST_DEAD:
        if(tap || hold) { g_state = ST_TITLE; draw_title(); }
        break;

    case ST_FIGHT: {
        /* ── guard, and what it costs ─────────────────────────── */
        if(g_broken) {
            if((uint16_t)(ms_now() - g_broke_t0) >= BREAK_MS) {
                g_broken = 0;
                draw_hero();
            }
        } else {
            if(want_guard != g_guard) {
                g_guard = want_guard;
                if(g_guard) g_guard_t0 = ms_now();
                draw_hero();
            }
            if(g_guard) {
                g_stam -= STAM_DRAIN;
                if(g_stam <= 0) { g_stam = 0; guard_break(); }
                else draw_bars();
            } else if(g_stam < g_stammax) {
                g_stam += STAM_REGEN;
                if(g_stam > g_stammax) g_stam = g_stammax;
                draw_bars();
            }
        }

        /* ── swing ────────────────────────────────────────────── */
        if(tap && !g_guard && !g_broken &&
           (uint16_t)(ms_now() - g_swing_t0) >= SWING_CD) {
            g_swing_t0 = ms_now();
            g_swinging = 1;
            draw_hero();
            hero_swing();
            if(g_ehp == 0u) {
                g_swinging = 0;
                draw_foe();
                draw_telegraph(-1, 0, 0);
                show_msg("SLAIN", 0);
                g_state = ST_MSG;
                break;
            }
        }
        if(g_swinging && (uint16_t)(ms_now() - g_swing_t0) >= 130u) {
            g_swinging = 0;
            draw_hero();
        }
        if(g_float_n && (uint16_t)(ms_now() - g_float_t0) >= 620u) {
            g_float_n = 0;
            draw_float();
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
                /* Roll the feint once, up front, so its odds are the number in
                 * the bestiary rather than a per-frame coin flip. */
                g_feint_at = (rnd(100) < g_efeint) ? 1u : 0u;
                g_feinted  = 0;
                g_phase = EP_WIND;
                g_ph_t0 = ms_now();
                /* Follow-ups land faster than the opener. */
                g_ph_dur = g_followup ? (uint16_t)(g_ewind * 3u / 4u) : g_ewind;
                draw_foe();
                draw_msg(g_is_break ? "RED" : "WHITE",
                         g_is_break ? "DO NOT GUARD" : "GUARD IT");
            }
            break;

        case EP_WIND: {
            int16_t pct = (int16_t)((el * 100u) / (g_ph_dur ? g_ph_dur : 1u));
            if(pct > 100) pct = 100;

            /* Feint: the wind-up stalls near the end.  Guarding on sight now
             * costs the stamina you needed for the real hit. */
            if(g_feint_at && !g_feinted && pct >= 72) {
                g_feinted = 1;
                g_ph_dur = (uint16_t)(g_ph_dur + g_ph_dur / 2u);
                draw_msg("FEINT", "IT HELD BACK");
            }
            if(pct != g_drawn_tele) draw_telegraph(pct, g_is_break, g_feinted);

            if(el >= g_ph_dur) {
                enemy_strike();
                draw_telegraph(-1, 0, 0);
                draw_foe();
                if(g_hp == 0u) { g_state = ST_DEAD; draw_dead(); }
            }
            break;
        }

        case EP_REC:
        case EP_STUN:
            if(el >= g_ph_dur) {
                g_phase = EP_IDLE;
                g_ph_t0 = ms_now();
                g_ph_dur = (uint16_t)(400u + rnd(460));
                draw_foe();
                draw_msg(0, 0);
            }
            break;
        }
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
