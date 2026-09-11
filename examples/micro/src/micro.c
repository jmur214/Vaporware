/* micro.c — WarioWare-style one-button microgame gauntlet for RAZ DC25000
 *           (N32G031K8Q7-1 + GC9107 128x160 LCD, Vaporware SDK)
 *
 * Three-second microgames, one after another, getting faster.  Four lives.
 * Six game types, and from round 6 onward the prompt may LIE: it shows the
 * wrong instruction first and swaps to the real one partway through, so
 * committing early gets you killed.
 *
 * Controls: PA7 button is the only input — tap, mash, or hold depending on
 * what the screen is (currently) telling you.
 *
 * Rendering follows the house pattern from flappy.c / dino.c: the static part
 * of a round is drawn once at round start, and each frame touches only what
 * actually moved (timer bar, sweep marker, mash counter).  A full-screen
 * display_fill() costs ~50 ms, which would eat most of a 1.2 s round.
 *
 * High score: NV_KEY_APP_1 (dino owns NV_KEY_APP_0, flappy NV_KEY_HIGH_SCORE).
 */
#include "app.h"
#include "display.h"
#include "battery.h"
#include "system.h"

/* ===================================================================
 * Palette
 * =================================================================== */
#define COL_BG      COL_RGB(245, 245, 245)  /* page white                */
#define COL_INK     COL_RGB( 30,  30,  30)  /* prompt text               */
#define COL_BAR     COL_BLACK               /* status bar                */
#define COL_CHROME  COL_WHITE               /* status bar text           */
#define COL_WIN     COL_RGB( 40, 190,  70)  /* pass flash, safe zone     */
#define COL_LOSE    COL_RGB(230,  40,  40)  /* fail flash, lost lives    */
#define COL_GO      COL_RGB( 40, 190,  70)  /* the "GO!" signal          */
#define COL_TIMER   COL_RGB(255, 170,   0)  /* countdown bar             */
#define COL_DIM     COL_RGB(190, 190, 190)  /* spent timer, empty slots  */
#define COL_GOLD    COL_RGB(255, 200,   0)  /* best score                */
#define COL_TBG     COL_RGB( 16,  16,  28)  /* title backdrop            */

/* Title palette — the letters and border cycle through these.  Bright and
 * clashing on purpose; this screen should look like a toy, not a utility. */
static const uint16_t g_pal[6] = {
    COL_RGB(255,  60,  60),
    COL_RGB(255, 160,   0),
    COL_RGB(255, 230,   0),
    COL_RGB( 60, 220,  90),
    COL_RGB(  0, 180, 255),
    COL_RGB(160,  90, 255),
};

/* ===================================================================
 * Layout (128x160)
 * =================================================================== */
#define BAR_H        14                 /* status bar height            */
#define PROMPT_Y     42                 /* prompt text top              */
#define PROMPT_H     24                 /* prompt cell height (scale 4) */
#define STAGE_Y      82                 /* per-game visuals top         */
#define STAGE_H      44
#define TIMER_Y     140
#define TIMER_H      12
#define TIMER_X       6
#define TIMER_W     (LCD_WIDTH - 12)

/* ===================================================================
 * Tuning
 * =================================================================== */
#define ROUND_START_MS  3000u   /* first round length                   */
#define ROUND_STEP_MS    120u   /* shaved per round                     */
#define ROUND_MIN_MS    1200u   /* floor — below this it is unreadable  */
#define RESULT_MS        550u   /* pass/fail flash                      */
#define LIVES_START        4u
#define LIE_FROM_ROUND     6u   /* prompts start lying here             */
#define MASH_TARGET        6u

/* ===================================================================
 * 3x5 glyphs, drawn at an arbitrary integer scale
 * =================================================================== */
static const uint8_t g_alpha[26][5] = {
    {0x2,0x5,0x7,0x5,0x5}, /* A */ {0x6,0x5,0x6,0x5,0x6}, /* B */
    {0x3,0x4,0x4,0x4,0x3}, /* C */ {0x6,0x5,0x5,0x5,0x6}, /* D */
    {0x7,0x4,0x6,0x4,0x7}, /* E */ {0x7,0x4,0x6,0x4,0x4}, /* F */
    {0x3,0x4,0x5,0x5,0x3}, /* G */ {0x5,0x5,0x7,0x5,0x5}, /* H */
    {0x7,0x2,0x2,0x2,0x7}, /* I */ {0x1,0x1,0x1,0x5,0x2}, /* J */
    {0x5,0x5,0x6,0x5,0x5}, /* K */ {0x4,0x4,0x4,0x4,0x7}, /* L */
    {0x5,0x7,0x7,0x5,0x5}, /* M */ {0x5,0x7,0x7,0x7,0x5}, /* N */
    {0x2,0x5,0x5,0x5,0x2}, /* O */ {0x6,0x5,0x6,0x4,0x4}, /* P */
    {0x2,0x5,0x5,0x7,0x3}, /* Q */ {0x6,0x5,0x6,0x5,0x5}, /* R */
    {0x3,0x4,0x2,0x1,0x6}, /* S */ {0x7,0x2,0x2,0x2,0x2}, /* T */
    {0x5,0x5,0x5,0x5,0x7}, /* U */ {0x5,0x5,0x5,0x5,0x2}, /* V */
    {0x5,0x5,0x7,0x7,0x5}, /* W */ {0x5,0x5,0x2,0x5,0x5}, /* X */
    {0x5,0x5,0x2,0x2,0x2}, /* Y */ {0x7,0x1,0x2,0x4,0x7}, /* Z */
};

static const uint8_t g_digit[10][5] = {
    {0x7,0x5,0x5,0x5,0x7},{0x2,0x6,0x2,0x2,0x7},{0x7,0x1,0x7,0x4,0x7},
    {0x7,0x1,0x7,0x1,0x7},{0x5,0x5,0x7,0x1,0x1},{0x7,0x4,0x7,0x1,0x7},
    {0x7,0x4,0x7,0x5,0x7},{0x7,0x1,0x1,0x1,0x1},{0x7,0x5,0x7,0x5,0x7},
    {0x7,0x5,0x7,0x1,0x7},
};

/* Cell advance for one character at a given scale (3 columns + 1 gap). */
#define CELL_W(scale) (4u * (scale))

static const uint8_t* glyph_for(char c) {
    if(c >= 'A' && c <= 'Z') return g_alpha[c - 'A'];
    if(c >= '0' && c <= '9') return g_digit[c - '0'];
    return 0;   /* space, and anything else, renders blank */
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
    if(n == 0) return 0;
    return (uint16_t)(n * CELL_W(scale) - scale);   /* drop trailing gap */
}

/* Draw text with its left edge at x.  Does not clear behind itself — callers
 * wipe the region first, which is cheaper than painting every blank pixel. */
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

/* ===================================================================
 * RNG — same LCG the other examples use
 * =================================================================== */
static uint32_t g_seed = 0x51F0C0DEUL;

static uint32_t rnd(uint32_t mod) {
    g_seed = g_seed * 1664525UL + 1013904223UL;
    return (g_seed >> 16) % mod;
}

/* ===================================================================
 * Microgames
 * =================================================================== */
enum {
    MG_TAP = 0,     /* press at least once                              */
    MG_NOTAP,       /* do not press at all                              */
    MG_MASH,        /* press MASH_TARGET times                          */
    MG_HOLD,        /* be holding when the timer runs out               */
    MG_WAIT,        /* wait for GO, then press — pressing early kills   */
    MG_STOP,        /* press while the sweeping marker is in the green  */
    MG_COUNT
};

/* Only TAP and NOTAP can lie: they are the pair that reads as a straight
 * swap, which is what makes the trick land instead of feeling arbitrary. */
static uint8_t mg_can_lie(uint8_t mg) {
    return (mg == MG_TAP || mg == MG_NOTAP) ? 1u : 0u;
}

static const char* mg_prompt(uint8_t mg) {
    switch(mg) {
    case MG_TAP:   return "TAP";
    case MG_NOTAP: return "NO TAP";
    case MG_MASH:  return "MASH";
    case MG_HOLD:  return "HOLD";
    case MG_WAIT:  return "WAIT";
    default:       return "STOP";
    }
}

/* ===================================================================
 * Game state
 * =================================================================== */
#define ST_TITLE   0
#define ST_HOWTO   1
#define ST_PLAY    2
#define ST_RESULT  3
#define ST_OVER    4

#define NV_KEY_MICRO_BEST  NV_KEY_APP_1

static uint8_t  g_state;
static uint32_t g_score;
static uint32_t g_best;
static uint8_t  g_new_best;
static uint8_t  g_lives;

static uint8_t  g_mg;            /* active microgame                    */
static uint16_t g_len_ms;        /* this round's length                 */
static uint16_t g_t0;            /* ms_now() at round start             */
static uint16_t g_result_t0;     /* ms_now() at result flash start      */

static uint8_t  g_lying;         /* prompt is currently showing a lie   */
static uint16_t g_lie_at;        /* ms offset when the prompt flips     */
static uint8_t  g_presses;       /* rising edges this round             */
static uint8_t  g_settled;       /* round already decided (early fail)  */
static uint8_t  g_passed;        /* outcome once settled                */

static uint16_t g_go_at;         /* MG_WAIT: when GO appears            */
static uint8_t  g_go_shown;
static uint8_t  g_zone_x;        /* MG_STOP: green zone left edge       */
static uint8_t  g_zone_w;
static int16_t  g_mark_prev;     /* MG_STOP: last marker x (-1 = none)  */
static uint8_t  g_mash_prev;     /* last mash count drawn               */

static uint16_t g_bat_raw = BAT_FULL;

/* Title animation */
static uint16_t g_hue_t0;
static uint8_t  g_hue;
static uint16_t g_blink_t0;
static uint8_t  g_blink_on;

/* ===================================================================
 * Status bar — score on the left, remaining lives on the right
 * =================================================================== */
static void draw_lives(void) {
    for(uint8_t i = 0; i < LIVES_START; i++) {
        uint16_t x = (uint16_t)(LCD_WIDTH - 8 - i * 9);
        display_fill_rect(x, 4, 6, 6, (i < g_lives) ? COL_LOSE : COL_DIM);
    }
}

static void draw_score_bar(void) {
    display_fill_rect(0, 0, LCD_WIDTH, BAR_H, COL_BAR);

    char buf[4];
    uint32_t s = (g_score > 999u) ? 999u : g_score;
    buf[0] = (char)('0' + (s / 100u) % 10u);
    buf[1] = (char)('0' + (s / 10u) % 10u);
    buf[2] = (char)('0' + s % 10u);
    buf[3] = '\0';
    draw_text(buf, 4, 3, 2, COL_CHROME);

    draw_lives();
}

/* ===================================================================
 * Timer bar — shrinks left-to-right as the round runs out
 * =================================================================== */
static void draw_timer(uint16_t elapsed) {
    uint32_t left = (elapsed >= g_len_ms) ? 0u : (uint32_t)(g_len_ms - elapsed);
    uint16_t fill = (uint16_t)((left * TIMER_W) / g_len_ms);

    if(fill) display_fill_rect(TIMER_X, TIMER_Y, fill, TIMER_H, COL_TIMER);
    if(fill < TIMER_W) {
        display_fill_rect((uint16_t)(TIMER_X + fill), TIMER_Y,
                          (uint16_t)(TIMER_W - fill), TIMER_H, COL_DIM);
    }
}

/* ===================================================================
 * Prompt + per-game stage furniture
 * =================================================================== */
static void draw_prompt(void) {
    const char* text = mg_prompt(g_mg);

    /* While lying, show the opposite instruction of the pair. */
    if(g_lying) text = (g_mg == MG_TAP) ? "NO TAP" : "TAP";

    display_fill_rect(0, PROMPT_Y, LCD_WIDTH, PROMPT_H, COL_BG);
    draw_text_mid(text, PROMPT_Y, 4, COL_INK);
}

static void draw_stage_static(void) {
    display_fill_rect(0, STAGE_Y, LCD_WIDTH, STAGE_H, COL_BG);

    if(g_mg == MG_STOP) {
        /* Track with the safe zone marked out. */
        display_fill_rect(6, (uint16_t)(STAGE_Y + 18), (uint16_t)(LCD_WIDTH - 12), 8, COL_DIM);
        display_fill_rect((uint16_t)(6 + g_zone_x), (uint16_t)(STAGE_Y + 18),
                          g_zone_w, 8, COL_WIN);
    }
}

/* ===================================================================
 * Round setup
 * =================================================================== */
static void round_begin(void) {
    g_mg = (uint8_t)rnd(MG_COUNT);

    uint32_t shave = (uint32_t)g_score * ROUND_STEP_MS;
    g_len_ms = (shave >= (ROUND_START_MS - ROUND_MIN_MS))
                   ? ROUND_MIN_MS
                   : (uint16_t)(ROUND_START_MS - shave);

    /* Lie about a third of the time, once the player knows the rules. */
    g_lying = 0;
    g_lie_at = 0;
    if(g_score + 1u >= LIE_FROM_ROUND && mg_can_lie(g_mg) && rnd(3) == 0) {
        g_lying  = 1;
        g_lie_at = (uint16_t)(g_len_ms / 2u);
    }

    g_presses  = 0;
    g_settled  = 0;
    g_passed   = 0;
    g_go_shown = 0;
    g_mash_prev = 0xFF;
    g_mark_prev = -1;

    /* MG_WAIT: GO lands somewhere in the middle half of the round. */
    g_go_at = (uint16_t)(g_len_ms / 4u + rnd(g_len_ms / 2u));

    /* MG_STOP: zone somewhere along the track, never flush to an edge. */
    g_zone_w = (uint8_t)(20 + rnd(14));
    g_zone_x = (uint8_t)(rnd((uint32_t)(LCD_WIDTH - 12 - g_zone_w)));

    display_fill_rect(0, BAR_H, LCD_WIDTH, (uint16_t)(LCD_HEIGHT - BAR_H), COL_BG);
    draw_score_bar();
    draw_prompt();
    draw_stage_static();
    draw_timer(0);

    g_t0 = ms_now();
}

static void game_begin(void) {
    g_seed ^= ((uint32_t)ms_now() << 11) ^ 0xA5A51234UL;
    g_score    = 0;
    g_lives    = LIVES_START;
    g_new_best = 0;
    g_state    = ST_PLAY;
    round_begin();
}

/* ===================================================================
 * Screens
 * =================================================================== */
/* ── Title ────────────────────────────────────────────────────────────
 * Animated, but only the parts that actually change are repainted: the ten
 * title letters, the border, and the blinking prompt.  Everything is drawn
 * over itself in a new colour rather than cleared first — the set pixels are
 * in the same places each pass, so there is nothing to erase.            */

#define TITLE_X      16     /* 5 chars at scale 5 = 95 px wide, centred   */
#define TITLE_Y1     34
#define TITLE_Y2     70
#define TITLE_STEP   20     /* CELL_W(5)                                  */
#define PROMPT_BLINK 480u
#define HUE_STEP     140u

static void title_letters(uint8_t off) {
    static const char top[] = "MICRO";
    static const char bot[] = "GAMES";

    for(uint8_t i = 0; i < 5; i++) {
        uint16_t x = (uint16_t)(TITLE_X + i * TITLE_STEP);

        const uint8_t* bm = glyph_for(top[i]);
        if(bm) draw_glyph(bm, x, TITLE_Y1, 5, g_pal[(i + off) % 6u]);

        bm = glyph_for(bot[i]);
        if(bm) draw_glyph(bm, x, TITLE_Y2, 5, g_pal[(i + off + 3u) % 6u]);
    }
}

static void title_border(uint8_t off) {
    uint16_t c = g_pal[off % 6u];
    display_fill_rect(0, 0, LCD_WIDTH, 4, c);
    display_fill_rect(0, (uint16_t)(LCD_HEIGHT - 4), LCD_WIDTH, 4, c);
    display_fill_rect(0, 0, 4, LCD_HEIGHT, c);
    display_fill_rect((uint16_t)(LCD_WIDTH - 4), 0, 4, LCD_HEIGHT, c);
}

static void title_prompt(uint8_t on) {
    display_fill_rect(6, 122, (uint16_t)(LCD_WIDTH - 12), 16, COL_TBG);
    if(on) draw_text_mid("TAP", 122, 3, COL_INK);
}

static void draw_title(void) {
    display_fill(COL_TBG);
    title_border(0);
    title_letters(0);
    title_prompt(1);

    if(g_best) {
        char buf[4];
        uint32_t b = (g_best > 999u) ? 999u : g_best;
        buf[0] = (char)('0' + (b / 100u) % 10u);
        buf[1] = (char)('0' + (b / 10u) % 10u);
        buf[2] = (char)('0' + b % 10u);
        buf[3] = '\0';
        draw_text_mid(buf, 144, 2, COL_GOLD);
    }
}

/* ── How to play ──────────────────────────────────────────────────────
 * Six rows: the prompt as it appears in-game on the left, what it actually
 * wants on the right.  Static, drawn once.                               */
static void draw_howto(void) {
    display_fill(COL_BG);

    draw_text_mid("HOW TO", 8, 3, COL_INK);

    static const char* const name[6] = {
        "TAP", "NO TAP", "MASH", "HOLD", "WAIT", "STOP"
    };
    static const char* const want[6] = {
        "PRESS", "DONT", "MASH X6", "HOLD IT", "AFTER GO", "ON GREEN"
    };

    for(uint8_t i = 0; i < 6; i++) {
        uint16_t y = (uint16_t)(34 + i * 16);
        draw_text(name[i], 6, y, 2, g_pal[i]);
        draw_text(want[i], 58, y, 2, COL_INK);
    }

    /* The one rule that is not obvious from playing. */
    draw_text_mid("PROMPTS LIE", 134, 2, COL_LOSE);
    draw_text_mid("TAP TO START", 150, 2, COL_DIM);
}

static void draw_over(void) {
    display_fill(COL_BG);
    draw_text_mid("GAME", 26, 4, COL_INK);
    draw_text_mid("OVER", 54, 4, COL_INK);

    char buf[4];
    uint32_t s = (g_score > 999u) ? 999u : g_score;
    buf[0] = (char)('0' + (s / 100u) % 10u);
    buf[1] = (char)('0' + (s / 10u) % 10u);
    buf[2] = (char)('0' + s % 10u);
    buf[3] = '\0';
    draw_text_mid(buf, 88, 4, COL_INK);

    draw_text_mid("BEST", 122, 2, COL_GOLD);
    uint32_t b = (g_best > 999u) ? 999u : g_best;
    buf[0] = (char)('0' + (b / 100u) % 10u);
    buf[1] = (char)('0' + (b / 10u) % 10u);
    buf[2] = (char)('0' + b % 10u);
    draw_text_mid(buf, 138, 2, COL_GOLD);
}

static void draw_result(uint8_t passed) {
    display_fill_rect(0, BAR_H, LCD_WIDTH, (uint16_t)(LCD_HEIGHT - BAR_H),
                      passed ? COL_WIN : COL_LOSE);
    draw_text_mid(passed ? "OK" : "MISS", 60, 5, COL_WHITE);
    draw_score_bar();
}

/* Enter the title screen and reset its animation clocks. */
static void enter_title(void) {
    g_state    = ST_TITLE;
    g_hue      = 0;
    g_blink_on = 1;
    draw_title();
    g_hue_t0   = ms_now();
    g_blink_t0 = g_hue_t0;
}

/* ===================================================================
 * Round resolution
 * =================================================================== */
static void settle(uint8_t passed) {
    if(g_settled) return;
    g_settled = 1;
    g_passed  = passed;
}

/* Evaluate whatever state the round ended in. */
static uint8_t judge(void) {
    if(g_settled) return g_passed;

    switch(g_mg) {
    case MG_TAP:   return (g_presses > 0) ? 1u : 0u;
    case MG_NOTAP: return (g_presses == 0) ? 1u : 0u;
    case MG_MASH:  return (g_presses >= MASH_TARGET) ? 1u : 0u;
    case MG_HOLD:  return button_pressed() ? 1u : 0u;
    case MG_WAIT:  return 0u;   /* never pressed after GO → miss */
    default:       return 0u;   /* MG_STOP: never pressed → miss */
    }
}

static void round_end(void) {
    uint8_t passed = judge();

    if(passed) {
        g_score++;
        if(g_score > g_best) {
            g_best     = g_score;
            g_new_best = 1;
        }
    } else if(g_lives) {
        g_lives--;
    }

    draw_result(passed);
    g_result_t0 = ms_now();
    g_state     = ST_RESULT;
}

/* ===================================================================
 * Framework callbacks
 * =================================================================== */
static void on_hard_reset(void) {
    if(g_new_best) {
        nv_write(NV_KEY_MICRO_BEST, g_best);
        g_new_best = 0;
    }
    g_state = ST_TITLE;
    draw_title();
}

void app_init(void) {
    display_recover();

    app_set_sleep_timeout(30000);
    app_set_hold_reset(10000, on_hard_reset);

    g_best    = nv_read(NV_KEY_MICRO_BEST, 0);
    g_bat_raw = bat_read_raw();

    g_state = ST_TITLE;
    display_fill(COL_BLACK);
    draw_title();
}

void app_update(uint32_t frame) {
    (void)frame;

    /* Sticky press latch: a tap shorter than a frame still counts.  Cleared
     * only once the round logic has consumed it. */
    static uint8_t s_prev = 0;
    uint8_t btn = button_raw();
    uint8_t edge = (btn && !s_prev) ? 1u : 0u;
    s_prev = btn;

    switch(g_state) {

    /* -------------------------------------------------------------- */
    case ST_TITLE:
        if(edge) {
            g_state = ST_HOWTO;
            draw_howto();
            break;
        }
        /* Cycle the letter/border hues and blink the prompt on their own
         * schedules, so the screen never sits still. */
        if((uint16_t)(ms_now() - g_hue_t0) >= HUE_STEP) {
            g_hue_t0 = ms_now();
            g_hue++;
            title_letters(g_hue);
            title_border(g_hue);
        }
        if((uint16_t)(ms_now() - g_blink_t0) >= PROMPT_BLINK) {
            g_blink_t0 = ms_now();
            g_blink_on = !g_blink_on;
            title_prompt(g_blink_on);
        }
        break;

    /* -------------------------------------------------------------- */
    case ST_HOWTO:
        if(edge) game_begin();
        break;

    /* -------------------------------------------------------------- */
    case ST_PLAY: {
        uint16_t elapsed = (uint16_t)(ms_now() - g_t0);

        /* The lie flips to the truth partway through. */
        if(g_lying && elapsed >= g_lie_at) {
            g_lying = 0;
            draw_prompt();
        }

        if(edge) g_presses++;

        /* Games that can be decided before the timer expires. */
        switch(g_mg) {
        case MG_WAIT:
            if(!g_go_shown && elapsed >= g_go_at) {
                g_go_shown = 1;
                display_fill_rect(0, STAGE_Y, LCD_WIDTH, STAGE_H, COL_GO);
                draw_text_mid("GO", (uint16_t)(STAGE_Y + 12), 4, COL_WHITE);
            }
            if(edge) settle(g_go_shown ? 1u : 0u);   /* early press = death */
            break;

        case MG_STOP: {
            /* Marker sweeps the track once, left to right. */
            uint16_t span = (uint16_t)(LCD_WIDTH - 12);
            uint16_t mx = (uint16_t)(((uint32_t)elapsed * span) / g_len_ms);
            if(mx >= span) mx = (uint16_t)(span - 1);

            if((int16_t)mx != g_mark_prev) {
                if(g_mark_prev >= 0) {
                    /* Repaint the vacated column back to track or zone. */
                    uint16_t px = (uint16_t)g_mark_prev;
                    uint8_t in_zone = (px >= g_zone_x && px < g_zone_x + g_zone_w);
                    display_fill_rect((uint16_t)(6 + px), (uint16_t)(STAGE_Y + 18),
                                      3, 8, in_zone ? COL_WIN : COL_DIM);
                }
                display_fill_rect((uint16_t)(6 + mx), (uint16_t)(STAGE_Y + 14),
                                  3, 16, COL_INK);
                g_mark_prev = (int16_t)mx;
            }

            if(edge) settle((mx >= g_zone_x && mx < g_zone_x + g_zone_w) ? 1u : 0u);
            break;
        }

        case MG_MASH:
            if(g_presses != g_mash_prev) {
                g_mash_prev = g_presses;
                display_fill_rect(0, STAGE_Y, LCD_WIDTH, STAGE_H, COL_BG);
                char buf[3];
                uint8_t n = (g_presses > 99u) ? 99u : g_presses;
                buf[0] = (char)('0' + (n / 10u));
                buf[1] = (char)('0' + (n % 10u));
                buf[2] = '\0';
                draw_text_mid(buf, (uint16_t)(STAGE_Y + 10), 4,
                              (g_presses >= MASH_TARGET) ? COL_WIN : COL_INK);
            }
            if(g_presses >= MASH_TARGET) settle(1);
            break;

        case MG_NOTAP:
            /* Pressing decides it immediately — but only against the prompt
             * that is true at the moment of the press.  Pressing while the
             * lie is still up is survivable; that is the whole joke. */
            if(edge && !g_lying) settle(0);
            break;

        default:
            break;
        }

        draw_timer(elapsed);

        if(g_settled || elapsed >= g_len_ms) round_end();
        break;
    }

    /* -------------------------------------------------------------- */
    case ST_RESULT:
        if((uint16_t)(ms_now() - g_result_t0) >= RESULT_MS) {
            if(g_lives == 0) {
                if(g_new_best) {
                    nv_write(NV_KEY_MICRO_BEST, g_best);
                    g_new_best = 0;
                }
                g_state = ST_OVER;
                draw_over();
            } else {
                round_begin();
                g_state = ST_PLAY;
            }
        }
        break;

    /* -------------------------------------------------------------- */
    case ST_OVER:
        if(edge) {
            g_state = ST_TITLE;
            draw_title();
        }
        break;
    }

    /* Battery refresh every ~5 s, only while idle screens are up. */
    if((frame % 150u) == 0u && (g_state == ST_TITLE || g_state == ST_OVER)) {
        g_bat_raw = bat_read_raw();
    }
}

void app_wake(void) {
    switch(g_state) {
    case ST_TITLE: draw_title(); break;
    case ST_OVER:  draw_over();  break;
    default:
        /* Do not resume a round that was interrupted by sleep — the timer
         * reference is long gone and the player never saw it. */
        enter_title();
        break;
    }
}
