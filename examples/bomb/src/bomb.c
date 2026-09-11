/* bomb.c — Hot Potato for RAZ DC25000
 *          (N32G031K8Q7-1 + GC9107 128x160 LCD, Vaporware SDK)
 *
 * A pass-around party game.  Arm the bomb and hand it on: every tap counts as
 * one pass.  The fuse is a hidden random length, so nobody knows how long they
 * are holding it for — you only know it is getting worse, because the pulse
 * speeds up as the fuse burns down.  Whoever is holding it when it goes off
 * loses, and the group's best pass count is saved as a shared high score.
 *
 * Controls: PA7 button — tap to arm, then tap to pass.
 *
 * Design note: the fuse length is deliberately never shown.  A visible timer
 * turns this into a solved problem (hold until 1 s remain, then pass); hiding
 * it is what makes people panic-pass early and get laughed at.  The escalating
 * pulse gives the tension without leaking the answer.
 *
 * Rendering follows the house pattern: static furniture drawn once per state,
 * per-frame work limited to the pulse ring and the pass counter.
 *
 * High score: NV_KEY_APP_2 (dino owns APP_0, micro APP_1).
 */
#include "app.h"
#include "display.h"
#include "battery.h"
#include "system.h"

/* ===================================================================
 * Palette
 * =================================================================== */
#define COL_BG      COL_RGB( 18,  18,  24)  /* near-black backdrop      */
#define COL_INK     COL_RGB(240, 240, 240)  /* headline text            */
#define COL_CALM    COL_RGB( 70, 130, 255)  /* pulse, early fuse        */
#define COL_WARM    COL_RGB(255, 170,   0)  /* pulse, mid fuse          */
#define COL_HOT     COL_RGB(255,  60,  40)  /* pulse, late fuse         */
#define COL_BOOM    COL_RGB(255,  70,  40)  /* explosion                */
#define COL_GOLD    COL_RGB(255, 205,   0)  /* best score               */
#define COL_DIM     COL_RGB( 70,  70,  80)

/* ===================================================================
 * Layout (128x160)
 * =================================================================== */
#define RING_CX      64      /* pulse ring centre                      */
#define RING_CY      74
#define RING_MAX     44      /* largest ring half-extent               */
#define COUNT_Y     118      /* pass counter                           */

/* ===================================================================
 * Tuning
 * =================================================================== */
#define FUSE_MIN_MS    6000u   /* shortest possible fuse               */
#define FUSE_SPAN_MS  16000u   /* ... plus up to this much more        */
#define PULSE_SLOW      620u   /* pulse period at the start            */
#define PULSE_FAST      110u   /* pulse period just before detonation  */
#define BOOM_HOLD_MS   1400u   /* explosion before the result screen   */

#define NV_KEY_BOMB_BEST  NV_KEY_APP_2

/* ===================================================================
 * 3x5 glyphs (same font as the other examples), drawn at any scale
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
                                  (uint16_t)(y + r * scale),
                                  scale, scale, fg);
            }
        }
    }
}

static uint16_t text_w(const char* s, uint8_t scale) {
    uint16_t n = 0;
    for(const char* p = s; *p; p++) n++;
    return n ? (uint16_t)(n * CELL_W(scale) - scale) : 0u;
}

static void draw_text(const char* s, uint16_t x, uint16_t y,
                      uint8_t scale, uint16_t fg) {
    for(const char* p = s; *p; p++) {
        const uint8_t* bm = glyph_for(*p);
        if(bm) draw_glyph(bm, x, y, scale, fg);
        x = (uint16_t)(x + CELL_W(scale));
    }
}

static void draw_text_mid(const char* s, uint16_t y, uint8_t scale, uint16_t fg) {
    uint16_t w = text_w(s, scale);
    uint16_t x = (w >= LCD_WIDTH) ? 0u : (uint16_t)((LCD_WIDTH - w) / 2);
    draw_text(s, x, y, scale, fg);
}

/* Render a number right into a caller-supplied 4-char buffer. */
static void num_str(char buf[4], uint32_t v) {
    if(v > 999u) v = 999u;
    buf[0] = (char)('0' + (v / 100u) % 10u);
    buf[1] = (char)('0' + (v / 10u) % 10u);
    buf[2] = (char)('0' + v % 10u);
    buf[3] = '\0';
}

/* ===================================================================
 * RNG
 * =================================================================== */
static uint32_t g_seed = 0xB0FACADEUL;

static uint32_t rnd(uint32_t mod) {
    g_seed = g_seed * 1664525UL + 1013904223UL;
    return (g_seed >> 16) % mod;
}

/* ===================================================================
 * State
 * =================================================================== */
#define ST_TITLE  0
#define ST_LIVE   1
#define ST_BOOM   2
#define ST_RESULT 3

static uint8_t  g_state;
static uint16_t g_t0;          /* ms_now() when the fuse was lit       */
static uint16_t g_fuse_ms;     /* hidden fuse length                   */
static uint16_t g_elapsed;     /* ms since arming (accumulated safely) */
static uint32_t g_passes;
static uint32_t g_best;
static uint8_t  g_new_best;

static uint16_t g_pulse_t0;    /* ms_now() at last pulse               */
static uint8_t  g_pulse_on;    /* ring currently drawn                 */
static uint32_t g_shown;       /* pass count currently on screen       */
static uint16_t g_boom_t0;

static uint16_t g_bat_raw = BAT_FULL;

/* ===================================================================
 * Pulse ring — a square ring that shrinks and reddens as the fuse burns
 * =================================================================== */
static uint16_t fuse_colour(void) {
    uint32_t pct = ((uint32_t)g_elapsed * 100u) / g_fuse_ms;
    if(pct < 45u) return COL_CALM;
    if(pct < 78u) return COL_WARM;
    return COL_HOT;
}

/* Pulse period shortens as the fuse runs down.  This is the only cue the
 * player gets, and it is deliberately coarse — enough to feel the danger
 * rising, not enough to time the hand-off precisely. */
static uint16_t pulse_period(void) {
    uint32_t pct = ((uint32_t)g_elapsed * 100u) / g_fuse_ms;
    if(pct > 100u) pct = 100u;
    uint32_t span = PULSE_SLOW - PULSE_FAST;
    return (uint16_t)(PULSE_SLOW - (span * pct) / 100u);
}

static void draw_ring(uint8_t on) {
    /* Ring half-extent shrinks with the fuse, so the bomb visibly closes in. */
    uint32_t pct = ((uint32_t)g_elapsed * 100u) / g_fuse_ms;
    if(pct > 100u) pct = 100u;
    uint16_t half = (uint16_t)(RING_MAX - (RING_MAX / 2u) * pct / 100u);
    uint16_t col = on ? fuse_colour() : COL_BG;

    uint16_t x = (uint16_t)(RING_CX - half);
    uint16_t y = (uint16_t)(RING_CY - half);
    uint16_t w = (uint16_t)(half * 2u);

    /* Four edges — cheaper and steadier than filling the whole square. */
    display_fill_rect(x, y, w, 4, col);
    display_fill_rect(x, (uint16_t)(y + w - 4), w, 4, col);
    display_fill_rect(x, y, 4, w, col);
    display_fill_rect((uint16_t)(x + w - 4), y, 4, w, col);
}

/* Wipe the whole ring band, used when the ring size changes between pulses. */
static void clear_ring_area(void) {
    display_fill_rect((uint16_t)(RING_CX - RING_MAX),
                      (uint16_t)(RING_CY - RING_MAX),
                      (uint16_t)(RING_MAX * 2u), (uint16_t)(RING_MAX * 2u),
                      COL_BG);
}

static void draw_passes(void) {
    char buf[4];
    num_str(buf, g_passes);
    display_fill_rect(0, COUNT_Y, LCD_WIDTH, 22, COL_BG);
    draw_text_mid(buf, COUNT_Y, 4, COL_INK);
    g_shown = g_passes;
}

/* ===================================================================
 * Screens
 * =================================================================== */
static void draw_title(void) {
    display_fill(COL_BG);
    draw_text_mid("HOT", 24, 5, COL_HOT);
    draw_text_mid("POTATO", 58, 3, COL_INK);
    draw_text_mid("TAP TO ARM", 96, 2, COL_DIM);

    if(g_best) {
        char buf[4];
        num_str(buf, g_best);
        draw_text_mid("BEST", 126, 2, COL_GOLD);
        draw_text_mid(buf, 142, 2, COL_GOLD);
    }
}

static void draw_live_static(void) {
    display_fill(COL_BG);
    draw_text_mid("PASS IT", 18, 3, COL_INK);
    draw_passes();
}

static void draw_boom(void) {
    display_fill(COL_BOOM);
    draw_text_mid("BOOM", 50, 5, COL_WHITE);
    draw_text_mid("YOU LOSE", 96, 2, COL_WHITE);
}

static void draw_result(void) {
    display_fill(COL_BG);
    draw_text_mid("PASSES", 26, 3, COL_INK);

    char buf[4];
    num_str(buf, g_passes);
    draw_text_mid(buf, 54, 5, COL_HOT);

    draw_text_mid("BEST", 104, 2, COL_GOLD);
    num_str(buf, g_best);
    draw_text_mid(buf, 120, 3, COL_GOLD);

    draw_text_mid("TAP", 146, 2, COL_DIM);
}

/* ===================================================================
 * Round control
 * =================================================================== */
static void arm(void) {
    /* Mix in the wall clock so the fuse is not the same every power-on. */
    g_seed ^= ((uint32_t)ms_now() << 9) ^ (g_passes * 2654435761UL);

    g_fuse_ms = (uint16_t)(FUSE_MIN_MS + rnd(FUSE_SPAN_MS));
    g_passes  = 0;
    g_elapsed = 0;
    g_shown   = 0xFFFFFFFFUL;
    g_pulse_on = 0;

    draw_live_static();

    g_t0       = ms_now();
    g_pulse_t0 = g_t0;
    g_state    = ST_LIVE;
}

static void detonate(void) {
    if(g_passes > g_best) {
        g_best     = g_passes;
        g_new_best = 1;
    }
    draw_boom();
    g_boom_t0 = ms_now();
    g_state   = ST_BOOM;
}

/* ===================================================================
 * Framework callbacks
 * =================================================================== */
static void on_hard_reset(void) {
    g_best     = 0;
    g_new_best = 0;
    nv_write(NV_KEY_BOMB_BEST, 0);
    g_state = ST_TITLE;
    draw_title();
}

void app_init(void) {
    display_recover();

    app_set_sleep_timeout(30000);
    app_set_hold_reset(10000, on_hard_reset);

    g_best    = nv_read(NV_KEY_BOMB_BEST, 0);
    g_bat_raw = bat_read_raw();

    g_state = ST_TITLE;
    display_fill(COL_BLACK);
    draw_title();
}

void app_update(uint32_t frame) {
    (void)frame;

    static uint8_t s_prev = 0;
    uint8_t btn  = button_raw();
    uint8_t edge = (btn && !s_prev) ? 1u : 0u;
    s_prev = btn;

    switch(g_state) {

    case ST_TITLE:
        if(edge) arm();
        break;

    case ST_LIVE: {
        g_elapsed = (uint16_t)(ms_now() - g_t0);

        if(g_elapsed >= g_fuse_ms) {
            detonate();
            break;
        }

        if(edge) {
            g_passes++;
            /* Brief acknowledgement so the passer knows it registered — the
             * next person needs to see the count tick over. */
            draw_passes();
        }

        /* Pulse: toggle the ring on its own shrinking schedule. */
        if((uint16_t)(ms_now() - g_pulse_t0) >= pulse_period()) {
            g_pulse_t0 = ms_now();
            if(g_pulse_on) {
                clear_ring_area();
                g_pulse_on = 0;
            } else {
                draw_ring(1);
                g_pulse_on = 1;
            }
        }
        break;
    }

    case ST_BOOM:
        if((uint16_t)(ms_now() - g_boom_t0) >= BOOM_HOLD_MS) {
            if(g_new_best) {
                nv_write(NV_KEY_BOMB_BEST, g_best);
                g_new_best = 0;
            }
            draw_result();
            g_state = ST_RESULT;
        }
        break;

    case ST_RESULT:
        if(edge) {
            g_state = ST_TITLE;
            draw_title();
        }
        break;
    }

    if((frame % 150u) == 0u && g_state != ST_LIVE) {
        g_bat_raw = bat_read_raw();
    }
}

void app_wake(void) {
    /* Never resume a live fuse across a sleep — the timer reference is stale
     * and whoever is holding it did not agree to that. */
    switch(g_state) {
    case ST_RESULT: draw_result(); break;
    default:
        g_state = ST_TITLE;
        draw_title();
        break;
    }
}
