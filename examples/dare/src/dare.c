/* dare.c — truth or dare for RAZ DC25000
 *          (N32G031K8Q7-1 + GC9107 128x160 LCD, Vaporware SDK)
 *
 * Tap to spin, the wheel lands on TRUTH or DARE and deals a card, pass it on.
 * That is the entire loop — the device is a prop for a conversation, so the
 * job is to get out of the way fast and be readable across a room.
 *
 * A spice tier is chosen at the start (MILD / SPICY / UNHINGED) so the same
 * firmware suits a tame crowd or a feral one.  The tier is saved, because the
 * group that picked UNHINGED once will want it again and should not have to
 * re-declare that every time the thing wakes up.
 *
 * Decks live in deck.h — 108 cards, 18 truths and 18 dares per tier.
 *
 * Cards are drawn with a word-wrapping renderer rather than pre-split lines,
 * so writing a card is just writing a sentence.  The font is uppercase-only,
 * which is why every card is too: lowercase and punctuation would render as
 * blanks and read as holes in the text.
 *
 * Tier persists on NV_KEY_APP_0.
 */
#include "app.h"
#include "display.h"
#include "battery.h"
#include "system.h"
#include "deck.h"

/* ===================================================================
 * Palette
 * =================================================================== */
#define COL_BG      COL_RGB( 18,  14,  26)
#define COL_INK     COL_RGB(240, 238, 250)
#define COL_DIM     COL_RGB(120, 115, 140)
#define COL_TRUTH   COL_RGB( 60, 175, 255)
#define COL_DARE    COL_RGB(255,  70, 120)
#define COL_RULE    COL_RGB( 60,  54,  80)

#define COL_MILD    COL_RGB( 70, 205, 110)
#define COL_SPICY   COL_RGB(255, 160,  30)
#define COL_WILD    COL_RGB(255,  55,  55)

static const uint16_t g_tier_col[3] = { COL_MILD, COL_SPICY, COL_WILD };
static const char* const g_tier_name[3] = { "MILD", "SPICY", "UNHINGED" };

/* ===================================================================
 * Layout
 * =================================================================== */
#define HEAD_Y      16      /* TRUTH / DARE headline                     */
#define RULE_Y      50
#define CARD_Y      60      /* wrapped card text starts here             */
#define LINE_H      14      /* scale 2 glyphs are 10 tall, 4 of leading  */
#define WRAP_CHARS  15      /* 15 * 8 - 2 = 118 px, inside a 128 px panel */

#define HOLD_MS    550u     /* hold on the tier screen to lock it in      */
#define DOUBLE_MS  320u     /* window for the second tap of a skip        */
#define PENALTY_MS 1100u    /* how long the penalty card stays up         */

#define COL_SKIP    COL_RGB(255, 180,  40)

#define NV_KEY_TIER  NV_KEY_APP_0

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
                                  (uint16_t)(y + r * scale),
                                  scale, scale, fg);
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

/* ===================================================================
 * Word wrap
 *
 * Greedy: take as many whole words as fit in WRAP_CHARS, then break.  Words
 * longer than a line are let through rather than hyphenated — no card has
 * one, and silently truncating text on a party game would be worse than an
 * ugly line.  Returns the number of lines drawn.
 * =================================================================== */
static uint8_t draw_wrapped(const char* s, uint16_t y0, uint16_t fg) {
    uint16_t n = str_len(s);
    uint16_t i = 0;
    uint8_t  line = 0;

    while(i < n) {
        uint16_t take = 0;      /* chars committed to this line  */
        uint16_t look = 0;      /* scan position within the rest */

        for(;;) {
            /* Reach to the end of the next word. */
            uint16_t w = look;
            while(i + w < n && s[i + w] != ' ') w++;

            if(w > WRAP_CHARS && take == 0u) { take = w; break; }  /* oversized */
            if(w > WRAP_CHARS) break;

            take = w;
            if(i + w >= n) break;
            look = (uint16_t)(w + 1);    /* step past the space */
        }

        draw_span_mid(&s[i], take, (uint16_t)(y0 + line * LINE_H), 2, fg);
        line++;

        i = (uint16_t)(i + take);
        while(i < n && s[i] == ' ') i++;
    }

    return line;
}

/* ===================================================================
 * RNG
 * =================================================================== */
static uint32_t g_seed = 0xDA2E0FFUL;

static uint32_t rnd(uint32_t mod) {
    g_seed = g_seed * 1664525UL + 1013904223UL;
    return (g_seed >> 16) % mod;
}

/* ===================================================================
 * State
 * =================================================================== */
#define ST_TITLE  0
#define ST_TIER   1
#define ST_READY  2
#define ST_SPIN   3
#define ST_CARD   4
#define ST_PEN    5

static uint8_t  g_state;
static uint8_t  g_tier;
static uint8_t  g_is_dare;       /* what the wheel landed on            */
static uint8_t  g_idx;           /* card within the deck                */
static uint8_t  g_last_truth = 0xFF;
static uint8_t  g_last_dare  = 0xFF;

static uint16_t g_spin_t0;
static uint16_t g_spin_step;     /* ms until the next flip              */
static uint8_t  g_spin_face;
static uint16_t g_spin_total;

static uint8_t  g_pressing;
static uint16_t g_press_t0;
static uint8_t  g_held_fired;

/* Skip bookkeeping.  A single tap has to wait out the double-tap window
 * before it counts as "done", which costs about a third of a second on the
 * common action — unnoticeable while the device is handed round a circle,
 * and the price of having a second gesture at all. */
static uint8_t  g_tap_pending;
static uint16_t g_tap_t0;
static uint8_t  g_skip_cost;
static uint16_t g_penalty;

static uint16_t g_bat_raw = BAT_FULL;

static const char* current_card(void) {
    return g_is_dare ? g_dare[g_tier][g_idx] : g_truth[g_tier][g_idx];
}

/* ===================================================================
 * Chrome
 * =================================================================== */
static void draw_tier_tag(void) {
    display_fill_rect(0, 0, LCD_WIDTH, 12, COL_BG);
    draw_text_mid(g_tier_name[g_tier], 2, 1, g_tier_col[g_tier]);
}

/* ===================================================================
 * Screens
 * =================================================================== */
static void draw_title(void) {
    display_fill(COL_BG);
    draw_text_mid("TRUTH", 30, 4, COL_TRUTH);
    draw_text_mid("OR", 58, 3, COL_DIM);
    draw_text_mid("DARE", 82, 4, COL_DARE);
    display_fill_rect(20, 116, 88, 2, COL_RULE);
    draw_text_mid("TAP", 128, 2, COL_INK);
}

static void draw_tier(void) {
    display_fill(COL_BG);
    draw_text_mid("SPICE", 16, 3, COL_INK);

    /* All three tiers stay on screen so the choice is a comparison rather
     * than a guess at what the other options were. */
    for(uint8_t i = 0; i < 3; i++) {
        uint16_t y = (uint16_t)(52 + i * 30);
        uint8_t  on = (i == g_tier);

        if(on) display_fill_rect(6, (uint16_t)(y - 6), (uint16_t)(LCD_WIDTH - 12), 26,
                                 COL_RULE);
        draw_text_mid(g_tier_name[i], y, on ? 3 : 2,
                      on ? g_tier_col[i] : COL_DIM);
    }

    draw_text_mid("TAP CHANGE", 142, 1, COL_DIM);
    draw_text_mid("HOLD START", 152, 1, COL_INK);
}

static void draw_ready(void) {
    display_fill(COL_BG);
    draw_tier_tag();
    draw_text_mid("TAP TO", 56, 3, COL_DIM);
    draw_text_mid("SPIN", 84, 4, COL_INK);

    /* Running shame meter for the whole circle — the device cannot tell who
     * is holding it, and a shared total is funnier than none at all. */
    if(g_penalty) {
        char buf[4];
        uint16_t v = (g_penalty > 999u) ? 999u : g_penalty;
        buf[0] = (char)('0' + (v / 100u) % 10u);
        buf[1] = (char)('0' + (v / 10u) % 10u);
        buf[2] = (char)('0' + v % 10u);
        buf[3] = '\0';
        draw_text_mid("CHICKEN", 128, 1, COL_DIM);
        draw_text_mid(buf, 138, 2, COL_SKIP);
    }
}

/* One frame of the spin: the two faces alternate, slowing down. */
static void draw_spin_face(uint8_t dare) {
    display_fill_rect(0, 40, LCD_WIDTH, 60, COL_BG);
    draw_text_mid(dare ? "DARE" : "TRUTH", 56, 4, dare ? COL_DARE : COL_TRUTH);
}

static void draw_card(void) {
    display_fill(COL_BG);
    draw_tier_tag();

    uint16_t col = g_is_dare ? COL_DARE : COL_TRUTH;
    draw_text_mid(g_is_dare ? "DARE" : "TRUTH", HEAD_Y, 4, col);
    display_fill_rect(14, RULE_Y, (uint16_t)(LCD_WIDTH - 28), 2, col);

    draw_wrapped(card_text(current_card()), CARD_Y, COL_INK);

    draw_text_mid("TAP DONE", 142, 1, COL_DIM);
    draw_text_mid("TAP TWICE TO SKIP", 152, 1, COL_SKIP);
}

/* The skip screen states the price and nothing else.  The point is that the
 * cost is public — the table sees what you were willing to pay. */
static void draw_penalty(void) {
    display_fill(COL_BG);
    draw_tier_tag();

    draw_text_mid("SKIPPED", 40, 3, COL_DIM);

    char d[2];
    d[0] = (char)('0' + g_skip_cost);
    d[1] = '\0';
    draw_text_mid(d, 70, 6, COL_SKIP);

    draw_text_mid(g_skip_cost == 1u ? "FAIR ENOUGH"
                : g_skip_cost == 2u ? "THAT COST YOU"
                                    : "COWARD", 128, 1, COL_DIM);
}

/* ===================================================================
 * Dealing
 * =================================================================== */
static void deal(void) {
    g_is_dare = (uint8_t)rnd(2);

    /* One reroll is enough to stop the obvious back-to-back repeat without
     * the bookkeeping of a full shuffle bag. */
    uint8_t pick = (uint8_t)rnd(DECK_N);
    if(g_is_dare) {
        if(pick == g_last_dare) pick = (uint8_t)rnd(DECK_N);
        g_last_dare = pick;
    } else {
        if(pick == g_last_truth) pick = (uint8_t)rnd(DECK_N);
        g_last_truth = pick;
    }
    g_idx = pick;
}

static void spin_begin(void) {
    display_fill(COL_BG);
    draw_tier_tag();

    g_spin_face  = 0;
    g_spin_step  = 45u;
    g_spin_total = 0;
    g_spin_t0    = ms_now();
    g_state      = ST_SPIN;
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

    app_set_sleep_timeout(30000);
    app_set_hold_reset(10000, on_hard_reset);

    g_tier = (uint8_t)nv_read(NV_KEY_TIER, 0);
    if(g_tier > 2u) g_tier = 0;

    g_bat_raw = bat_read_raw();

    g_state = ST_TITLE;
    display_fill(COL_BLACK);
    draw_title();
}

void app_update(uint32_t frame) {
    (void)frame;

    uint8_t btn  = button_raw();
    uint8_t edge = 0;
    uint8_t rel  = 0;

    if(btn && !g_pressing) {
        g_pressing   = 1;
        g_held_fired = 0;
        g_press_t0   = ms_now();
        edge = 1;
    } else if(!btn && g_pressing) {
        g_pressing = 0;
        rel = g_held_fired ? 0u : 1u;
    }

    switch(g_state) {

    case ST_TITLE:
        if(edge) {
            g_state = ST_TIER;
            /* Claim this press: without it, a long tap on the title carries
             * straight through the hold check below and skips the tier
             * choice entirely. */
            g_held_fired = 1;
            draw_tier();
        }
        break;

    case ST_TIER:
        /* Tap cycles, hold commits — the same split the other games use for
         * "change something" versus "commit to it". */
        if(btn && g_pressing && !g_held_fired &&
           (uint16_t)(ms_now() - g_press_t0) >= HOLD_MS) {
            g_held_fired = 1;
            nv_write(NV_KEY_TIER, g_tier);
            g_seed ^= ((uint32_t)ms_now() << 9) ^ 0x9E3779B9UL;
            g_penalty = 0;      /* new tier, fresh round of shame */
            g_state = ST_READY;
            draw_ready();
        } else if(rel) {
            g_tier = (uint8_t)((g_tier + 1u) % 3u);
            draw_tier();
        }
        break;

    case ST_READY:
        if(edge) spin_begin();
        break;

    case ST_SPIN: {
        if((uint16_t)(ms_now() - g_spin_t0) >= g_spin_step) {
            g_spin_t0     = ms_now();
            g_spin_total  = (uint16_t)(g_spin_total + g_spin_step);
            g_spin_face  ^= 1u;
            draw_spin_face(g_spin_face);

            /* Ease out, then stop — the slowdown is the whole bit of drama
             * this loop gets, so it is worth the few lines. */
            g_spin_step = (uint16_t)(g_spin_step + 18u);

            if(g_spin_total > 900u) {
                deal();
                draw_card();
                g_state = ST_CARD;
            }
        }
        break;
    }

    case ST_CARD:
        /* First tap opens a window; a second inside it is a skip, and the
         * window closing on its own means the card was taken. */
        if(edge) {
            if(g_tap_pending) {
                g_tap_pending = 0;
                g_skip_cost   = card_cost(current_card());
                g_penalty     = (uint16_t)(g_penalty + g_skip_cost);
                draw_penalty();
                g_tap_t0 = ms_now();
                g_state  = ST_PEN;
            } else {
                g_tap_pending = 1;
                g_tap_t0      = ms_now();
            }
        } else if(g_tap_pending &&
                  (uint16_t)(ms_now() - g_tap_t0) >= DOUBLE_MS) {
            g_tap_pending = 0;
            g_state = ST_READY;
            draw_ready();
        }
        break;

    case ST_PEN:
        if((uint16_t)(ms_now() - g_tap_t0) >= PENALTY_MS) {
            g_state = ST_READY;
            draw_ready();
        }
        break;
    }

    if((frame % 150u) == 0u && g_state != ST_SPIN) {
        g_bat_raw = bat_read_raw();
    }
}

void app_wake(void) {
    /* Come back to the spin prompt rather than a stale card — whoever woke it
     * is a new turn. */
    g_pressing    = 0;
    g_tap_pending = 0;
    g_state       = ST_READY;
    draw_ready();
}
