/* hour.c — Power Hour for RAZ DC25000
 *          (N32G031K8Q7-1 + GC9107 128x160 LCD, Vaporware SDK)
 *
 * Set it on the table and forget it.  Sixty shots, one a minute, for an hour;
 * the device is the clock and the signal.  Between alerts it shows the
 * countdown big enough to read from across a room, and on the minute it goes
 * off hard enough that nobody can claim they missed it.
 *
 * ── There is no buzzer, so the backlight is the siren ────────────────────
 * The hardware has no LED, no piezo and no motor — the panel is the only
 * output.  A full display_fill() costs about 50 ms, so flashing by repainting
 * colours tops out around 10 Hz and looks sluggish.  The backlight is plain
 * GPIO though (PB4, active low), so toggling it is effectively free: the
 * alert paints one bright frame, then strobes the whole panel on and off at
 * 6 Hz.  A screen snapping black-to-blazing in a dim room catches the eye far
 * better than colours cycling at the same rate, and it costs nothing per
 * flash, so the alert can run six full seconds without burning SPI time.
 *
 * ── Running for an hour ──────────────────────────────────────────────────
 * Two things would otherwise break this, and both are easy to miss:
 *
 *   Auto-sleep.  The framework blanks the device after an idle timeout, and
 *   nobody touches a power-hour clock for a whole minute at a stretch — it
 *   would sleep during the first gap and never come back.  The timeout is set
 *   to 0 (disabled) for the run and restored on the way out.
 *
 *   Clock wrap.  ms_now() is a 16-bit millisecond counter that wraps every
 *   ~65 s, which is barely longer than the 60 s it has to measure.  Timing a
 *   minute directly off it would work right up until it did not.  So the run
 *   keeps a 32-bit accumulator fed by small per-frame deltas; each delta is a
 *   few tens of milliseconds, far from the wrap, and the total is good for
 *   days.
 */
#include "app.h"
#include "display.h"
#include "battery.h"
#include "system.h"

/* ===================================================================
 * Palette
 * =================================================================== */
#define COL_BG      COL_RGB( 12,  12,  20)
#define COL_INK     COL_RGB(245, 245, 250)
#define COL_DIM     COL_RGB(120, 120, 140)
#define COL_ACCENT  COL_RGB(255, 180,  30)
#define COL_TRACK   COL_RGB( 42,  42,  58)
#define COL_ALERT1  COL_RGB(255, 225,  90)
#define COL_ALERT2  COL_RGB(255,  60,  50)
#define COL_DONE    COL_RGB( 60, 205, 110)

/* ===================================================================
 * Tuning
 * =================================================================== */
#define SHOTS        60u
#define MINUTE_MS 60000ul
#define ALERT_MS   6000u     /* how long the siren runs                */
#define STROBE_MS    80u     /* half-period of the backlight strobe    */
#define HOLD_MS     900u     /* hold to pause, and to quit when paused */

/* ===================================================================
 * Layout
 * =================================================================== */
#define COUNT_Y     14
#define BIG_Y       38
#define BAR_X        8
#define BAR_Y      124
#define BAR_W      112
#define BAR_H       10

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

static void draw_text_mid(const char* s, uint16_t y, uint8_t scale, uint16_t fg) {
    draw_span_mid(s, str_len(s), y, scale, fg);
}

/* Number without leading zeros, for the hero countdown. */
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
 * State
 * =================================================================== */
#define ST_TITLE  0
#define ST_RUN    1
#define ST_PAUSE  2
#define ST_DONE   3

static uint8_t  g_state;
static uint8_t  g_shots;          /* alerts already delivered          */
static uint32_t g_ms;             /* ms since the hour began           */
static uint16_t g_tick;           /* last ms_now() sample              */
static uint16_t g_shown_secs;     /* countdown value currently drawn   */

static uint8_t  g_pressing;
static uint16_t g_press_t0;
static uint8_t  g_hold_fired;

static uint16_t g_bat_raw = BAT_FULL;

/* Small per-frame deltas keep the 32-bit total honest across the 16-bit
 * wrap; pass advance=0 to hold the clock while paused without letting the
 * sample go stale. */
static void clock_tick(uint8_t advance) {
    uint16_t now = ms_now();
    uint16_t d   = (uint16_t)(now - g_tick);
    g_tick = now;
    if(advance) g_ms += d;
}

static uint16_t secs_to_next(void) {
    uint32_t due = (uint32_t)g_shots * MINUTE_MS;
    if(g_ms >= due) return 0u;
    uint32_t rem = due - g_ms;
    return (uint16_t)((rem + 999ul) / 1000ul);   /* ceil, so it ends on 1 */
}

/* ===================================================================
 * Screens
 * =================================================================== */
static void draw_title(void) {
    display_fill(COL_BG);
    draw_text_mid("POWER", 26, 5, COL_ACCENT);
    draw_text_mid("HOUR", 62, 5, COL_INK);
    draw_text_mid("60 SHOTS 60 MINS", 100, 1, COL_DIM);
    draw_text_mid("TAP TO START", 124, 2, COL_INK);
    draw_text_mid("LEAVE IT ON THE TABLE", 146, 1, COL_DIM);
}

static void draw_bar(void) {
    display_fill_rect(BAR_X, BAR_Y, BAR_W, BAR_H, COL_TRACK);
    uint32_t w = ((uint32_t)g_shots * BAR_W) / SHOTS;
    if(w) display_fill_rect(BAR_X, BAR_Y, (uint16_t)w, BAR_H, COL_ACCENT);
}

/* Everything that does not change second to second. */
static void draw_run_static(void) {
    display_fill(COL_BG);
    draw_text_mid("SECONDS", 96, 1, COL_DIM);
    draw_bar();
    draw_text_mid("HOLD TO PAUSE", 146, 1, COL_DIM);
}

static void draw_shot_count(void) {
    char buf[4];
    uint8_t n = num_str(buf, g_shots);

    display_fill_rect(0, COUNT_Y, LCD_WIDTH, 12, COL_BG);

    /* "17 OF 60" assembled by hand so the count keeps the accent colour. */
    uint16_t total_w = (uint16_t)((n + 5u) * CELL_W(2) - 2u);
    uint16_t x = (uint16_t)((LCD_WIDTH - total_w) / 2);
    draw_span(buf, n, x, COUNT_Y, 2, COL_ACCENT);
    draw_span(" OF 60", 6, (uint16_t)(x + n * CELL_W(2)), COUNT_Y, 2, COL_DIM);
}

static void draw_countdown(uint16_t secs) {
    char buf[4];
    uint8_t n = num_str(buf, secs);
    display_fill_rect(0, BIG_Y, LCD_WIDTH, 46, COL_BG);
    draw_span_mid(buf, n, BIG_Y, 9, COL_INK);
    g_shown_secs = secs;
}

static void draw_pause(void) {
    display_fill(COL_BG);
    draw_text_mid("PAUSED", 40, 4, COL_ACCENT);
    draw_shot_count();
    draw_bar();
    draw_text_mid("TAP TO RESUME", 96, 2, COL_INK);
    draw_text_mid("HOLD TO QUIT", 146, 1, COL_DIM);
}

static void draw_done(void) {
    display_fill(COL_BG);
    draw_text_mid("POWER", 22, 4, COL_ACCENT);
    draw_text_mid("HOUR", 48, 4, COL_ACCENT);
    draw_text_mid("DONE", 84, 5, COL_DONE);
    draw_text_mid("60 OF 60", 126, 2, COL_INK);
    draw_text_mid("TAP", 148, 1, COL_DIM);
}

/* ===================================================================
 * The siren
 *
 * One bright frame, then strobe the panel with the backlight.  Blocks for up
 * to ALERT_MS, which is well inside the ~26 s watchdog, and delay_ms() feeds
 * the IWDG throughout.  A press cuts it short, for the table that is already
 * drinking and wants the countdown back.
 * =================================================================== */
static void alert(uint8_t shot_no) {
    char buf[4];
    uint8_t n = num_str(buf, shot_no);

    display_fill(COL_ALERT1);
    draw_text_mid("DRINK", 30, 5, COL_ALERT2);
    draw_span_mid(buf, n, 78, 8, COL_BG);
    draw_text_mid("OF 60", 134, 2, COL_ALERT2);

    uint16_t spent = 0;
    uint8_t  on    = 1;

    while(spent < ALERT_MS) {
        on = !on;
        display_set_backlight(on);

        for(uint16_t i = 0; i < STROBE_MS; i++) {
            delay_ms(1);
            if(button_raw()) { spent = ALERT_MS; break; }
        }
        spent = (uint16_t)(spent + STROBE_MS);
    }

    display_set_backlight(1);   /* never leave the table in the dark */

    /* The siren burned real time; fold it into the hour so the minutes stay
     * aligned to the wall clock rather than drifting six seconds a shot. */
    clock_tick(1);
}

/* ===================================================================
 * Run control
 * =================================================================== */
static void run_begin(void) {
    /* Nobody touches this for a minute at a time — sleep would kill it. */
    app_set_sleep_timeout(0);
    display_set_backlight(1);

    g_shots = 0;
    g_ms    = 0;
    g_tick  = ms_now();

    draw_run_static();
    draw_shot_count();
    draw_countdown(0);
    g_state = ST_RUN;
}

static void back_to_title(void) {
    app_set_sleep_timeout(30000);   /* idle screens may sleep again */
    display_set_backlight(1);
    g_state = ST_TITLE;
    draw_title();
}

/* ===================================================================
 * Framework callbacks
 * =================================================================== */
static void on_hard_reset(void) {
    back_to_title();
}

void app_init(void) {
    display_recover();

    app_set_sleep_timeout(30000);
    app_set_hold_reset(10000, on_hard_reset);

    g_bat_raw = bat_read_raw();

    g_state = ST_TITLE;
    display_fill(COL_BLACK);
    draw_title();
}

void app_update(uint32_t frame) {
    (void)frame;

    uint8_t btn  = button_raw();
    uint8_t edge = 0;

    if(btn && !g_pressing) {
        g_pressing   = 1;
        g_hold_fired = 0;
        g_press_t0   = ms_now();
        edge = 1;
    } else if(!btn && g_pressing) {
        g_pressing = 0;
    }

    uint8_t held = (btn && g_pressing && !g_hold_fired &&
                    (uint16_t)(ms_now() - g_press_t0) >= HOLD_MS) ? 1u : 0u;

    switch(g_state) {

    case ST_TITLE:
        if(edge) run_begin();
        break;

    case ST_RUN: {
        clock_tick(1);

        if(held) {
            g_hold_fired = 1;
            g_state = ST_PAUSE;
            draw_pause();
            break;
        }

        /* Shot 1 is due at 0:00, so this fires the moment the hour starts. */
        if(g_ms >= (uint32_t)g_shots * MINUTE_MS) {
            g_shots++;
            alert(g_shots);

            if(g_shots >= SHOTS) {
                app_set_sleep_timeout(30000);
                g_state = ST_DONE;
                draw_done();
                break;
            }

            draw_run_static();
            draw_shot_count();
            draw_countdown(secs_to_next());
            break;
        }

        uint16_t s = secs_to_next();
        if(s != g_shown_secs) draw_countdown(s);
        break;
    }

    case ST_PAUSE:
        clock_tick(0);          /* hold the hour, keep the sample fresh */

        if(held) {
            g_hold_fired = 1;
            back_to_title();
        } else if(edge) {
            draw_run_static();
            draw_shot_count();
            draw_countdown(secs_to_next());
            g_state = ST_RUN;
        }
        break;

    case ST_DONE:
        if(edge) back_to_title();
        break;
    }

    if((frame % 150u) == 0u && g_state != ST_RUN) {
        g_bat_raw = bat_read_raw();
    }
}

void app_wake(void) {
    /* Sleep is disabled during a run, so waking means the hour was already
     * over or never started. */
    g_pressing = 0;
    back_to_title();
}
