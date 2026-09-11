/* swarm.c — one-button Galaga for RAZ DC25000
 *           (N32G031K8Q7-1 + GC9107 128x160 LCD, Vaporware SDK)
 *
 * A vertical shooter, which is the one arcade genre this screen is the right
 * shape for.
 *
 * ── A fixed turret, so timing is the aim ─────────────────────────────────
 * The ship does not move.  It sits centred at the bottom and fires straight
 * up, one shot per tap.  With no steering, aiming becomes a timing problem:
 * the formation sweeps overhead and you fire when a column crosses your lane.
 *
 * That single decision drives two others.
 *
 * The formation has to sweep wide enough that EVERY column passes over the
 * lane, or the outer columns would be permanently unkillable.  So it tracks
 * from x=-30 to x=56 and overhangs both edges, and sprites clip at the screen
 * bounds rather than being skipped.
 *
 * And every threat has to be destructible.  A ship that cannot dodge must not
 * face anything it can only absorb, so there are no enemy bullets at all —
 * the danger is divers peeling out of the formation and coming down at you.
 * They weave on the way in, crossing your lane and sliding out of it, so
 * killing one is the same timing read as farming the formation, just with a
 * deadline.  That is the whole tension: keep scoring off the formation, or
 * deal with the thing descending on you.
 */
#include "app.h"
#include "display.h"
#include "battery.h"
#include "system.h"

/* ===================================================================
 * Palette
 * =================================================================== */
#define COL_BG      COL_RGB(  6,   6,  16)
#define COL_INK     COL_RGB(235, 240, 250)
#define COL_DIM     COL_RGB(110, 115, 140)
#define COL_SHIP    COL_RGB( 90, 220, 255)
#define COL_SHOT    COL_RGB(255, 255, 180)
#define COL_EVIL    COL_RGB(255, 110,  90)
#define COL_R0      COL_RGB(235,  70,  90)
#define COL_R1      COL_RGB(250, 200,  60)
#define COL_R2      COL_RGB(110, 230, 180)
#define COL_GOLD    COL_RGB(255, 205,   0)
#define COL_BOOM    COL_RGB(255, 170,  60)
#define COL_LANE    COL_RGB( 26,  30,  50)

/* ===================================================================
 * Field
 * =================================================================== */
#define HUD_H        14
#define FIELD_TOP    16
#define FIELD_BOT   158

#define COLS          5
#define ROWS          3
#define CELL_XW      22
#define CELL_YH      15
#define FOE_W        14
#define FOE_H        10
#define FORM_Y       26
#define FORM_X_MIN  (-30)
#define FORM_X_MAX    56

#define SHIP_W       14
#define SHIP_H       11
#define SHIP_X       57      /* centred, and it never moves            */
#define SHIP_Y      142
#define LANE_X      (SHIP_X + 6)

#define MAX_SHOT      3
#define MAX_DIVER     2

/* ===================================================================
 * Tuning
 * =================================================================== */
#define TAP_MAX     220u
#define FIRE_CD     140u
#define SHOT_VY       6

#define NV_KEY_BEST  NV_KEY_APP_2

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

#define GW(scale) (4u * (scale))

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

static uint16_t str_len(const char* s) { uint16_t n = 0; while(s[n]) n++; return n; }

static void draw_span(const char* s, uint16_t len, uint16_t x, uint16_t y,
                      uint8_t scale, uint16_t fg) {
    for(uint16_t i = 0; i < len; i++) {
        const uint8_t* bm = glyph_for(s[i]);
        if(bm) draw_glyph(bm, x, y, scale, fg);
        x = (uint16_t)(x + GW(scale));
    }
}

static void draw_span_mid(const char* s, uint16_t len, uint16_t y,
                          uint8_t scale, uint16_t fg) {
    uint16_t w = len ? (uint16_t)(len * GW(scale) - scale) : 0u;
    uint16_t x = (w >= LCD_WIDTH) ? 0u : (uint16_t)((LCD_WIDTH - w) / 2);
    draw_span(s, len, x, y, scale, fg);
}

static void draw_text(const char* s, uint16_t x, uint16_t y, uint8_t sc, uint16_t c) {
    draw_span(s, str_len(s), x, y, sc, c);
}

static void draw_text_mid(const char* s, uint16_t y, uint8_t sc, uint16_t c) {
    draw_span_mid(s, str_len(s), y, sc, c);
}

static uint8_t num_str(char buf[7], uint32_t v) {
    if(v > 999999ul) v = 999999ul;
    uint32_t div = 1; uint8_t n = 1;
    while(v / div >= 10ul) { div *= 10ul; n++; }
    uint8_t i = 0;
    while(div) { buf[i++] = (char)('0' + (v / div) % 10ul); div /= 10ul; }
    buf[i] = '\0';
    return n;
}

/* ===================================================================
 * RNG
 * =================================================================== */
static uint32_t g_seed = 0x5A1A6AUL;
static uint32_t rnd(uint32_t m) {
    g_seed = g_seed * 1664525UL + 1013904223UL;
    return (g_seed >> 16) % m;
}

/* ===================================================================
 * Clipped drawing
 *
 * The formation deliberately overhangs both screen edges so that every
 * column crosses the firing lane, so sprites have to clip rather than be
 * dropped — a skipped sprite would pop in and out at the edges.
 * =================================================================== */
static void rect_clip(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t col) {
    if(x < 0) { w = (int16_t)(w + x); x = 0; }
    if(y < FIELD_TOP) { h = (int16_t)(h - (FIELD_TOP - y)); y = FIELD_TOP; }
    if(x + w > LCD_WIDTH) w = (int16_t)(LCD_WIDTH - x);
    if(y + h > FIELD_BOT) h = (int16_t)(FIELD_BOT - y);
    if(w > 0 && h > 0)
        display_fill_rect((uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, col);
}

static void blit_foe(int16_t x, int16_t y, uint16_t col) {
    rect_clip((int16_t)(x + 4), (int16_t)(y + 2), 6, 6, col);
    rect_clip(x,                (int16_t)(y + 3), 4, 4, col);
    rect_clip((int16_t)(x + 10),(int16_t)(y + 3), 4, 4, col);
    rect_clip((int16_t)(x + 5), y,                4, 2, col);
    rect_clip((int16_t)(x + 5), (int16_t)(y + 4), 2, 2, COL_BG);
    rect_clip((int16_t)(x + 8), (int16_t)(y + 4), 2, 2, COL_BG);
}

/* Erasing repaints the lane stripe, so the firing line stays visible under
 * everything that moves across it. */
static void erase_box(int16_t x, int16_t y, int16_t w, int16_t h) {
    rect_clip(x, y, w, h, COL_BG);
    if(x <= LANE_X + 1 && x + w >= LANE_X) {
        int16_t ly = (y < FIELD_TOP) ? FIELD_TOP : y;
        int16_t lh = (int16_t)(y + h - ly);
        if(lh > 0) rect_clip(LANE_X, ly, 2, lh, COL_LANE);
    }
}

static void blit_ship(uint16_t col) {
    display_fill_rect(SHIP_X + 6, SHIP_Y, 2, 4, col);
    display_fill_rect(SHIP_X + 4, SHIP_Y + 3, 6, 3, col);
    display_fill_rect(SHIP_X,     SHIP_Y + 5, 14, 4, col);
    display_fill_rect(SHIP_X + 1, SHIP_Y + 9, 3, 2, col);
    display_fill_rect(SHIP_X + 10,SHIP_Y + 9, 3, 2, col);
}

/* ===================================================================
 * State
 * =================================================================== */
typedef struct { int16_t x, y, px, py; uint8_t live; } Shot;
typedef struct {
    uint8_t live, cell;
    int16_t x, y, px, py, cx;
    uint8_t wob;
} Diver;

#define ST_TITLE 0
#define ST_PLAY  1
#define ST_WAVE  2
#define ST_OVER  3

static uint8_t  g_state;
static uint8_t  g_cell[ROWS * COLS];
static uint8_t  g_alive;
static int16_t  g_form_x, g_form_dir;
static uint16_t g_drift_ms, g_drift_t0;

static uint16_t g_fire_t0;
static Shot     g_shot[MAX_SHOT];
static Diver    g_diver[MAX_DIVER];

static uint16_t g_dive_t0, g_dive_ms;
static uint8_t  g_dive_speed, g_dive_max;

static uint32_t g_score, g_best;
static uint8_t  g_lives, g_wave;
static uint16_t g_hit_t0;
static uint8_t  g_invuln;

static uint8_t  g_pressing, g_press_used;
static uint16_t g_press_t0, g_wave_t0;
static uint16_t g_bat_raw = BAT_FULL;

/* ===================================================================
 * Formation
 * =================================================================== */
static uint16_t row_col(uint8_t r) {
    return (r == 0u) ? COL_R0 : (r == 1u) ? COL_R1 : COL_R2;
}

static int16_t cell_x(uint8_t i) { return (int16_t)(g_form_x + (i % COLS) * CELL_XW); }
static int16_t cell_y(uint8_t i) { return (int16_t)(FORM_Y + (i / COLS) * CELL_YH); }

static void draw_formation(void) {
    for(uint8_t i = 0; i < ROWS * COLS; i++)
        if(g_cell[i]) blit_foe(cell_x(i), cell_y(i), row_col((uint8_t)(i / COLS)));
}

static void erase_formation(void) {
    for(uint8_t i = 0; i < ROWS * COLS; i++)
        if(g_cell[i]) erase_box(cell_x(i), cell_y(i), FOE_W, FOE_H);
}

/* ===================================================================
 * HUD
 * =================================================================== */
static void draw_hud(void) {
    display_fill_rect(0, 0, LCD_WIDTH, HUD_H, COL_BG);

    char buf[7];
    uint8_t n = num_str(buf, g_score);
    draw_span(buf, n, 4, 3, 2, COL_INK);

    for(uint8_t i = 0; i < g_lives; i++)
        display_fill_rect((uint16_t)(LCD_WIDTH - 10 - i * 9), 4, 6, 6, COL_SHIP);

    n = num_str(buf, g_wave);
    draw_text("W", 74, 3, 2, COL_DIM);
    draw_span(buf, n, 82, 3, 2, COL_GOLD);
}

static void draw_lane(void) {
    display_fill_rect(LANE_X, FIELD_TOP, 2, (uint16_t)(SHIP_Y - FIELD_TOP), COL_LANE);
}

/* ===================================================================
 * Waves
 * =================================================================== */
static void start_wave(void) {
    for(uint8_t i = 0; i < ROWS * COLS; i++) g_cell[i] = 1;
    g_alive    = ROWS * COLS;
    g_form_x   = 12;
    g_form_dir = 1;

    /* Everything that scales is a timing number.  A later wave asks for a
     * sharper read, not more shots into the same enemy. */
    g_drift_ms   = (g_wave >= 8u) ? 40u : (uint16_t)(110u - g_wave * 9u);
    g_dive_ms    = (g_wave >= 8u) ? 1200u : (uint16_t)(3400u - g_wave * 280u);
    g_dive_speed = (uint8_t)(1u + g_wave / 4u);
    if(g_dive_speed > 3u) g_dive_speed = 3u;
    g_dive_max   = (g_wave >= 4u) ? 2u : 1u;

    for(uint8_t i = 0; i < MAX_SHOT; i++)  g_shot[i].live  = 0;
    for(uint8_t i = 0; i < MAX_DIVER; i++) g_diver[i].live = 0;

    g_drift_t0 = ms_now();
    g_dive_t0  = ms_now();

    display_fill(COL_BG);
    draw_hud();
    draw_lane();
    draw_formation();
    blit_ship(COL_SHIP);
}

static void launch_diver(void) {
    uint8_t used = 0;
    for(uint8_t d = 0; d < MAX_DIVER; d++) if(g_diver[d].live) used++;
    if(used >= g_dive_max) return;

    /* Only the lowest live enemy of a column peels off, so a dive always
     * reads as leaving the formation rather than passing through it. */
    uint8_t pick = 0xFF;
    for(uint8_t t = 0; t < 12u && pick == 0xFF; t++) {
        uint8_t c = (uint8_t)rnd(COLS);
        for(int8_t r = ROWS - 1; r >= 0; r--) {
            uint8_t i = (uint8_t)(r * COLS + c);
            if(g_cell[i]) { pick = i; break; }
        }
    }
    if(pick == 0xFF) return;

    for(uint8_t d = 0; d < MAX_DIVER; d++) {
        if(g_diver[d].live) continue;
        g_cell[pick] = 0;
        erase_box(cell_x(pick), cell_y(pick), FOE_W, FOE_H);

        g_diver[d].live = 1;
        g_diver[d].cell = pick;
        g_diver[d].x  = cell_x(pick);
        g_diver[d].cx = cell_x(pick);
        g_diver[d].y  = cell_y(pick);
        g_diver[d].px = -999;
        g_diver[d].wob = 0;
        return;
    }
}

static void fire_player(void) {
    for(uint8_t i = 0; i < MAX_SHOT; i++) {
        if(g_shot[i].live) continue;
        g_shot[i].live = 1;
        g_shot[i].x  = LANE_X;
        g_shot[i].y  = SHIP_Y - 5;
        g_shot[i].px = -999;
        return;
    }
}

static uint8_t overlap(int16_t ax, int16_t ay, int16_t aw, int16_t ah,
                       int16_t bx, int16_t by, int16_t bw, int16_t bh) {
    return (ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by);
}

/* ===================================================================
 * Screens
 * =================================================================== */
static void draw_title(void) {
    display_fill(COL_BG);
    draw_text_mid("SWARM", 22, 5, COL_R0);

    blit_foe(28, 60, COL_R0);
    blit_foe(57, 60, COL_R1);
    blit_foe(86, 60, COL_R2);

    draw_text_mid("TAP TO FIRE", 92, 2, COL_INK);
    draw_text_mid("YOU CANNOT MOVE", 112, 1, COL_DIM);
    draw_text_mid("SHOOT THE DIVERS DOWN", 124, 1, COL_EVIL);

    if(g_best) {
        char buf[7];
        uint8_t n = num_str(buf, g_best);
        draw_span_mid(buf, n, 142, 2, COL_GOLD);
    }
}

static void draw_over(void) {
    display_fill(COL_BG);
    draw_text_mid("GAME", 30, 4, COL_INK);
    draw_text_mid("OVER", 56, 4, COL_INK);

    char buf[7];
    uint8_t n = num_str(buf, g_score);
    draw_span_mid(buf, n, 92, 4, COL_GOLD);

    if(g_score >= g_best) draw_text_mid("NEW BEST", 128, 2, COL_R1);
    draw_text_mid("TAP", 148, 1, COL_DIM);
}

static void run_begin(void) {
    g_seed ^= ((uint32_t)ms_now() << 11) ^ 0x51A2B3C4UL;
    g_score  = 0;
    g_lives  = 3;
    g_wave   = 1;
    g_invuln = 0;
    start_wave();
    g_state = ST_PLAY;
}

static void player_hit(void) {
    if(g_invuln) return;
    g_lives  = g_lives ? (uint8_t)(g_lives - 1u) : 0u;
    g_invuln = 1;
    g_hit_t0 = ms_now();

    blit_ship(COL_BOOM);
    draw_hud();

    if(g_lives == 0u) {
        if(g_score > g_best) {
            g_best = g_score;
            nv_write(NV_KEY_BEST, g_best);
        }
        g_state = ST_OVER;
        draw_over();
    }
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

    g_best = nv_read(NV_KEY_BEST, 0);
    g_bat_raw = bat_read_raw();

    g_state = ST_TITLE;
    display_fill(COL_BLACK);
    draw_title();
}

void app_update(uint32_t frame) {
    (void)frame;

    uint8_t btn = button_raw();
    uint8_t tap = 0;

    if(btn && !g_pressing) {
        g_pressing = 1;
        g_press_used = 0;
        g_press_t0 = ms_now();
    } else if(!btn && g_pressing) {
        g_pressing = 0;
        if((uint16_t)(ms_now() - g_press_t0) < TAP_MAX) tap = 1;
    }

    switch(g_state) {

    case ST_TITLE:
        if(tap) run_begin();
        break;

    case ST_OVER:
        if(tap) { g_state = ST_TITLE; draw_title(); }
        break;

    case ST_WAVE:
        if((uint16_t)(ms_now() - g_wave_t0) >= 1100u) {
            start_wave();
            g_state = ST_PLAY;
        }
        break;

    case ST_PLAY: {
        if(g_invuln && (uint16_t)(ms_now() - g_hit_t0) >= 900u) {
            g_invuln = 0;
            blit_ship(COL_SHIP);
        }

        if(tap && (uint16_t)(ms_now() - g_fire_t0) >= FIRE_CD) {
            g_fire_t0 = ms_now();
            fire_player();
        }

        /* ── formation drift ──────────────────────────────────── */
        if((uint16_t)(ms_now() - g_drift_t0) >= g_drift_ms) {
            g_drift_t0 = ms_now();
            erase_formation();
            g_form_x = (int16_t)(g_form_x + g_form_dir * 2);
            if(g_form_x <= FORM_X_MIN) { g_form_x = FORM_X_MIN; g_form_dir = 1; }
            if(g_form_x >= FORM_X_MAX) { g_form_x = FORM_X_MAX; g_form_dir = -1; }
            draw_formation();
        }

        if((uint16_t)(ms_now() - g_dive_t0) >= g_dive_ms) {
            g_dive_t0 = ms_now();
            launch_diver();
        }

        /* ── shots ────────────────────────────────────────────── */
        for(uint8_t i = 0; i < MAX_SHOT; i++) {
            if(!g_shot[i].live) continue;
            if(g_shot[i].px != -999) erase_box(g_shot[i].px, g_shot[i].py, 2, 6);
            g_shot[i].y = (int16_t)(g_shot[i].y - SHOT_VY);
            if(g_shot[i].y < FIELD_TOP) { g_shot[i].live = 0; continue; }

            uint8_t gone = 0;

            for(uint8_t d = 0; d < MAX_DIVER && !gone; d++) {
                if(!g_diver[d].live) continue;
                if(overlap(g_shot[i].x, g_shot[i].y, 2, 6,
                           g_diver[d].x, g_diver[d].y, FOE_W, FOE_H)) {
                    erase_box(g_diver[d].x, g_diver[d].y, FOE_W, FOE_H);
                    g_diver[d].live = 0;
                    g_alive--;
                    g_score += 60u;      /* worth more: it was about to land */
                    draw_hud();
                    g_shot[i].live = 0;
                    gone = 1;
                }
            }
            for(uint8_t c = 0; c < ROWS * COLS && !gone; c++) {
                if(!g_cell[c]) continue;
                if(overlap(g_shot[i].x, g_shot[i].y, 2, 6,
                           cell_x(c), cell_y(c), FOE_W, FOE_H)) {
                    g_cell[c] = 0;
                    g_alive--;
                    erase_box(cell_x(c), cell_y(c), FOE_W, FOE_H);
                    g_score += (uint32_t)(30u - (c / COLS) * 10u);
                    draw_hud();
                    g_shot[i].live = 0;
                    gone = 1;
                }
            }

            if(g_shot[i].live) {
                display_fill_rect((uint16_t)g_shot[i].x, (uint16_t)g_shot[i].y,
                                  2, 6, COL_SHOT);
                g_shot[i].px = g_shot[i].x;
                g_shot[i].py = g_shot[i].y;
            }
        }

        /* ── divers ───────────────────────────────────────────── */
        for(uint8_t d = 0; d < MAX_DIVER; d++) {
            if(!g_diver[d].live) continue;
            if(g_diver[d].px != -999)
                erase_box(g_diver[d].px, g_diver[d].py, FOE_W, FOE_H);

            /* Home on the lane, but weave across it — so a diver is only
             * hittable part of the time and the shot has to be timed. */
            g_diver[d].wob++;
            static const int8_t wv[8] = { 0, 8, 12, 8, 0, -8, -12, -8 };
            int8_t off = wv[(g_diver[d].wob >> 2) & 7u];

            if(g_diver[d].cx < SHIP_X) g_diver[d].cx++;
            else if(g_diver[d].cx > SHIP_X) g_diver[d].cx--;

            g_diver[d].x = (int16_t)(g_diver[d].cx + off);
            g_diver[d].y = (int16_t)(g_diver[d].y + g_dive_speed);

            if(overlap(g_diver[d].x, g_diver[d].y, FOE_W, FOE_H,
                       SHIP_X, SHIP_Y, SHIP_W, SHIP_H) ||
               g_diver[d].y > SHIP_Y) {
                erase_box(g_diver[d].x, g_diver[d].y, FOE_W, FOE_H);
                g_diver[d].live = 0;
                /* It rejoins the formation, the way a Galaga diver loops
                 * back round — getting past you is not a free kill for it. */
                g_cell[g_diver[d].cell] = 1;
                blit_foe(cell_x(g_diver[d].cell), cell_y(g_diver[d].cell),
                         row_col((uint8_t)(g_diver[d].cell / COLS)));
                player_hit();
                if(g_state != ST_PLAY) break;
                continue;
            }

            blit_foe(g_diver[d].x, g_diver[d].y, COL_EVIL);
            g_diver[d].px = g_diver[d].x;
            g_diver[d].py = g_diver[d].y;
        }
        if(g_state != ST_PLAY) break;

        if(g_alive == 0u) {
            g_score += (uint32_t)(100u * g_wave);
            g_wave++;
            display_fill(COL_BG);
            draw_hud();
            draw_text_mid("WAVE", 60, 3, COL_INK);
            char buf[7];
            uint8_t n = num_str(buf, g_wave);
            draw_span_mid(buf, n, 86, 5, COL_GOLD);
            g_wave_t0 = ms_now();
            g_state = ST_WAVE;
        }
        break;
    }
    }

    if((frame % 150u) == 0u && g_state != ST_PLAY) g_bat_raw = bat_read_raw();
}

void app_wake(void) {
    g_pressing = 0;
    g_state = ST_TITLE;
    draw_title();
}
