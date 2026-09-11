/* kings.c — Kings Cup for RAZ DC25000
 *           (N32G031K8Q7-1 + GC9107 128x160 LCD, Vaporware SDK)
 *
 * The whole card game in one app, with the vape as the dealer.  Tap to draw;
 * the device shows the card, states the rule, and keeps the bookkeeping a
 * physical deck cannot: a real 52-card shoe dealt without replacement, and a
 * cup that fills a quarter on every king.  The fourth king empties it and
 * ends the game.
 *
 * Dealing without replacement is the part that matters.  Picking a random
 * card each time would be easier, but then kings arrive on a coin flip, the
 * cup fills at no predictable rate and the game has no shape.  A real shuffled
 * shoe means the tension builds the way it does with cardboard — three kings
 * down and a thinning deck is a different room than three kings down and forty
 * cards left.
 *
 * Rule text is deliberately the largest thing on screen after the card.  This
 * gets read aloud across a table by someone who is not concentrating, so the
 * rule name is sized to the widest scale that still fits and the body wraps
 * underneath it.
 */
#include "app.h"
#include "display.h"
#include "battery.h"
#include "system.h"

/* ===================================================================
 * Palette — card table
 * =================================================================== */
#define COL_BG      COL_RGB( 16,  62,  44)   /* felt                   */
#define COL_FELT2   COL_RGB( 12,  50,  36)
#define COL_INK     COL_RGB(240, 245, 238)
#define COL_DIM     COL_RGB(140, 175, 158)
#define COL_CARD    COL_RGB(248, 246, 240)
#define COL_SUIT_RED COL_RGB(205,  35,  45)
#define COL_SUIT_BLK COL_RGB( 24,  24,  28)
#define COL_CUP     COL_RGB(212, 214, 224)
#define COL_DRINK   COL_RGB(240, 170,  40)
#define COL_GOLD    COL_RGB(255, 205,   0)

/* ===================================================================
 * Layout
 * =================================================================== */
#define CARD_X       6
#define CARD_Y       8
#define CARD_W      36
#define CARD_H      46

#define CUP_CX     108
#define CUP_Y       10
#define CUP_H       30
#define CUP_TW      24
#define CUP_BW      14

#define NAME_Y      62
#define BODY_Y      88
#define LINE_H      14
#define WRAP_CHARS  15

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

static uint16_t span_w(uint16_t len, uint8_t scale) {
    return len ? (uint16_t)(len * CELL_W(scale) - scale) : 0u;
}

static void draw_span_mid(const char* s, uint16_t len, uint16_t y,
                          uint8_t scale, uint16_t fg) {
    uint16_t w = span_w(len, scale);
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

/* Rule names vary from "ME" to "QUESTION MASTER", so pick the biggest scale
 * that still fits rather than sizing everything to the longest. */
static void draw_text_fit(const char* s, uint16_t y, uint8_t max_scale,
                          uint16_t fg) {
    uint16_t n = str_len(s);
    uint8_t sc = max_scale;
    while(sc > 1u && span_w(n, sc) > (LCD_WIDTH - 6u)) sc--;
    draw_span_mid(s, n, y, sc, fg);
}

/* Greedy word wrap; see the same routine in dare.c. */
static uint8_t draw_wrapped(const char* s, uint16_t y0, uint16_t fg) {
    uint16_t n = str_len(s), i = 0;
    uint8_t line = 0;

    while(i < n) {
        uint16_t take = 0, look = 0;
        for(;;) {
            uint16_t w = look;
            while(i + w < n && s[i + w] != ' ') w++;
            if(w > WRAP_CHARS && take == 0u) { take = w; break; }
            if(w > WRAP_CHARS) break;
            take = w;
            if(i + w >= n) break;
            look = (uint16_t)(w + 1);
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
static uint32_t g_seed = 0x4B1465CUL;

static uint32_t rnd(uint32_t mod) {
    g_seed = g_seed * 1664525UL + 1013904223UL;
    return (g_seed >> 16) % mod;
}

/* ===================================================================
 * Suits — drawn, because "H D C S" on a card face looks like a spreadsheet
 * =================================================================== */
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

/* Rows widening then narrowing. */
static void draw_diamond(int16_t cx, int16_t cy, int16_t r, uint16_t col) {
    for(int16_t i = -r; i <= r; i++) {
        int16_t a = (i < 0) ? (int16_t)(-i) : i;
        int16_t w = (int16_t)((r - a) * 2 + 1);
        display_fill_rect((uint16_t)(cx - w / 2), (uint16_t)(cy + i),
                          (uint16_t)w, 1, col);
    }
}

static void draw_heart(int16_t cx, int16_t cy, int16_t r, uint16_t col) {
    fill_circle((int16_t)(cx - r / 2), (int16_t)(cy - r / 3), (int16_t)(r / 2 + 1), col);
    fill_circle((int16_t)(cx + r / 2), (int16_t)(cy - r / 3), (int16_t)(r / 2 + 1), col);
    for(int16_t i = 0; i <= r; i++) {
        int16_t w = (int16_t)((r - i) * 2 + 1);
        display_fill_rect((uint16_t)(cx - w / 2), (uint16_t)(cy - r / 3 + i),
                          (uint16_t)w, 1, col);
    }
}

static void draw_spade(int16_t cx, int16_t cy, int16_t r, uint16_t col) {
    for(int16_t i = 0; i <= r; i++) {
        int16_t w = (int16_t)(i * 2 + 1);
        display_fill_rect((uint16_t)(cx - w / 2), (uint16_t)(cy - r + i),
                          (uint16_t)w, 1, col);
    }
    fill_circle((int16_t)(cx - r / 2), cy, (int16_t)(r / 2 + 1), col);
    fill_circle((int16_t)(cx + r / 2), cy, (int16_t)(r / 2 + 1), col);
    display_fill_rect((uint16_t)(cx - 1), (uint16_t)(cy + r / 2), 3,
                      (uint16_t)(r / 2 + 2), col);
}

static void draw_club(int16_t cx, int16_t cy, int16_t r, uint16_t col) {
    fill_circle(cx, (int16_t)(cy - r / 2), (int16_t)(r / 2 + 1), col);
    fill_circle((int16_t)(cx - r / 2), (int16_t)(cy + r / 4), (int16_t)(r / 2 + 1), col);
    fill_circle((int16_t)(cx + r / 2), (int16_t)(cy + r / 4), (int16_t)(r / 2 + 1), col);
    display_fill_rect((uint16_t)(cx - 1), (uint16_t)(cy + r / 2), 3,
                      (uint16_t)(r / 2 + 2), col);
}

/* Suit order: 0 spades, 1 hearts, 2 diamonds, 3 clubs. */
static void draw_suit(uint8_t suit, int16_t cx, int16_t cy, int16_t r, uint16_t col) {
    switch(suit) {
    case 0:  draw_spade(cx, cy, r, col);   break;
    case 1:  draw_heart(cx, cy, r, col);   break;
    case 2:  draw_diamond(cx, cy, r, col); break;
    default: draw_club(cx, cy, r, col);    break;
    }
}

static uint8_t suit_is_red(uint8_t suit) { return (suit == 1u || suit == 2u); }

/* ===================================================================
 * The rules
 * =================================================================== */
static const char* const g_rule_name[13] = {
    "WATERFALL",        /* A  */
    "YOU",              /* 2  */
    "ME",               /* 3  */
    "WHORES",           /* 4  */
    "JIVE",             /* 5  */
    "DICKS",            /* 6  */
    "HEAVEN",           /* 7  */
    "MATE",             /* 8  */
    "TRUTH OR DARE",    /* 9  */
    "NEW RULE",         /* 10 */
    "NEVER EVER",       /* J  */
    "QUESTION MASTER",  /* Q  */
    "CUP",              /* K  */
};

static const char* const g_rule_body[13] = {
    "YOU START DRINKING EVERYONE FOLLOWS NOBODY STOPS UNTIL YOU DO",
    "PICK SOMEONE THEY DRINK",
    "YOU DRINK",
    "ALL THE GIRLS DRINK",
    "START A DANCE MOVE EVERYONE COPIES LAST ONE IN DRINKS",
    "ALL THE GUYS DRINK",
    "POINT AT THE SKY LAST PERSON TO DO IT DRINKS",
    "PICK A MATE THEY DRINK EVERY TIME YOU DRINK",
    "",     /* 9  — filled from the prompt deck */
    "MAKE UP A RULE IT LASTS UNTIL SOMEONE DRAWS THE NEXT 10",
    "",     /* J  — filled from the prompt deck */
    "ANYONE WHO ANSWERS YOUR QUESTIONS DRINKS UNTIL THE NEXT QUEEN",
    "POUR INTO THE CUP THE FOURTH KING DRINKS THE LOT",
};

/* Card 9 deals a truth-or-dare prompt; the drawer picks the victim. */
static const char* const g_tod[16] = {
    "WHO HERE WOULD YOU SWAP LIVES WITH",
    "SHOW THE LAST PHOTO ON YOUR PHONE",
    "WHAT IS YOUR MOST TOXIC TRAIT",
    "LET SOMEONE READ YOUR LAST TEXT",
    "WHO HERE HAVE YOU THOUGHT ABOUT",
    "DO YOUR WORST DANCE FOR 15 SECONDS",
    "WHAT IS THE LAST LIE YOU TOLD",
    "SWAP SHIRTS WITH SOMEONE HERE",
    "WHAT IS YOUR BODY COUNT",
    "SEND A VOICE NOTE TO YOUR GROUP CHAT",
    "WHO HERE WOULD YOU KISS",
    "READ YOUR LAST SEARCH OUT LOUD",
    "WHAT IS THE WORST THING YOU HAVE DONE",
    "LET THE GROUP POST ON YOUR STORY",
    "WHO HERE DO YOU FIND MOST ATTRACTIVE",
    "TAKE OFF ONE ITEM OF CLOTHING",
};

/* Card J: the header already says NEVER EVER, so the body is just the rest
 * of the sentence — the reader says the whole thing aloud. */
static const char* const g_never[16] = {
    "BEEN DUMPED BY TEXT",
    "GHOSTED SOMEONE",
    "LIED TO GET OUT OF WORK",
    "KISSED SOMEONE IN THIS ROOM",
    "STALKED AN EX ONLINE",
    "SENT A TEXT TO THE WRONG PERSON",
    "FAKED BEING ILL",
    "BEEN THROWN OUT OF SOMEWHERE",
    "CHEATED ON A TEST",
    "STOLEN SOMETHING",
    "FALLEN FOR A FRIEND",
    "SENT A NUDE",
    "BEEN CAUGHT LYING",
    "HAD A CRUSH ON A TEACHER",
    "PRETENDED TO KNOW SOMEONE",
    "BROKEN SOMETHING AND HIDDEN IT",
};

static const char* rank_str(uint8_t rank) {
    static const char* const r[13] = {
        "A","2","3","4","5","6","7","8","9","10","J","Q","K"
    };
    return r[rank];
}

/* ===================================================================
 * Shoe
 * =================================================================== */
static uint8_t  g_shoe[52];
static uint8_t  g_left;          /* cards still face down               */
static uint8_t  g_rank;
static uint8_t  g_suit;
static uint8_t  g_kings;
static const char* g_body;       /* body text for the current card      */

static uint8_t  g_state;
static uint8_t  g_pressing;
static uint16_t g_bat_raw = BAT_FULL;

#define ST_TITLE  0
#define ST_CARD   1
#define ST_OVER   2

static void shuffle(void) {
    for(uint8_t i = 0; i < 52u; i++) g_shoe[i] = i;

    /* Fisher-Yates, so every ordering is equally likely and no card can be
     * dealt twice — the reason kings arrive at a believable rate. */
    for(uint8_t i = 51u; i > 0u; i--) {
        uint8_t j = (uint8_t)rnd((uint32_t)i + 1u);
        uint8_t t = g_shoe[i];
        g_shoe[i] = g_shoe[j];
        g_shoe[j] = t;
    }
    g_left = 52u;
}

/* ===================================================================
 * Drawing the table
 * =================================================================== */
static void draw_cup(uint8_t kings, uint8_t level_override, uint8_t use_override) {
    uint8_t level = use_override ? level_override : kings;
    uint16_t fill_top = (uint16_t)(CUP_Y + CUP_H - (CUP_H * level) / 4u);

    for(int16_t i = 0; i < CUP_H; i++) {
        int16_t w = (int16_t)(CUP_TW - (((CUP_TW - CUP_BW) * i) / CUP_H));
        int16_t x = (int16_t)(CUP_CX - w / 2);
        uint16_t y = (uint16_t)(CUP_Y + i);

        display_fill_rect((uint16_t)(x + 2), y, (uint16_t)(w - 4), 1,
                          (y >= fill_top) ? COL_DRINK : COL_FELT2);
        display_fill_rect((uint16_t)x, y, 2, 1, COL_CUP);
        display_fill_rect((uint16_t)(x + w - 2), y, 2, 1, COL_CUP);
    }
    display_fill_rect((uint16_t)(CUP_CX - 8), (uint16_t)(CUP_Y + CUP_H), 16, 3, COL_CUP);
}

static void draw_card_face(void) {
    display_fill_rect(CARD_X, CARD_Y, CARD_W, CARD_H, COL_CARD);

    uint16_t col = suit_is_red(g_suit) ? COL_SUIT_RED : COL_SUIT_BLK;
    draw_text(rank_str(g_rank), CARD_X + 4, CARD_Y + 4, 2, col);
    draw_suit(g_suit, (int16_t)(CARD_X + CARD_W / 2),
              (int16_t)(CARD_Y + CARD_H - 15), 7, col);
}

static void draw_left_count(void) {
    char buf[3];
    uint8_t v = g_left;
    buf[0] = (char)('0' + (v / 10u));
    buf[1] = (char)('0' + (v % 10u));
    buf[2] = '\0';
    draw_span(buf, 2, 56, 18, 2, COL_INK);
    draw_text("LEFT", 54, 34, 1, COL_DIM);
}

static void draw_table(void) {
    display_fill(COL_BG);
    draw_card_face();
    draw_cup(g_kings, 0, 0);
    draw_left_count();

    uint16_t col = (g_rank == 12u) ? COL_GOLD : COL_INK;
    draw_text_fit(g_rule_name[g_rank], NAME_Y, 3, col);
    draw_wrapped(g_body, BODY_Y, COL_INK);

    draw_text_mid("TAP", 150, 1, COL_DIM);
}

static void draw_title(void) {
    display_fill(COL_BG);
    draw_text_mid("KINGS", 26, 5, COL_GOLD);
    draw_text_mid("CUP", 62, 5, COL_INK);

    draw_cup(4, 0, 0);          /* a full cup, purely as decoration */
    draw_text_mid("TAP TO DEAL", 140, 2, COL_INK);
}

static void draw_over(void) {
    display_fill(COL_BG);
    draw_text_mid("FOURTH", 18, 3, COL_INK);
    draw_text_mid("KING", 40, 4, COL_GOLD);
    draw_text_mid("DRINK IT ALL", 70, 2, COL_INK);

    display_fill_rect(0, 92, LCD_WIDTH, 2, COL_FELT2);
    draw_text_mid("GAME", 104, 4, COL_INK);
    draw_text_mid("OVER", 130, 4, COL_INK);
}

/* ===================================================================
 * Dealing
 * =================================================================== */
static void deal_next(void) {
    if(g_left == 0u) {          /* cannot happen with four kings in the shoe */
        g_state = ST_OVER;
        draw_over();
        return;
    }

    uint8_t card = g_shoe[--g_left];
    g_rank = (uint8_t)(card % 13u);
    g_suit = (uint8_t)(card / 13u);

    if(g_rank == 8u)       g_body = g_tod[rnd(16)];      /* 9 */
    else if(g_rank == 10u) g_body = g_never[rnd(16)];    /* J */
    else                   g_body = g_rule_body[g_rank];

    if(g_rank == 12u) {         /* K */
        g_kings++;
        if(g_kings >= 4u) {
            /* Show the full cup draining before the verdict — it is the one
             * moment the whole game has been building to. */
            draw_table();
            for(uint8_t lvl = 4u; lvl > 0u; lvl--) {
                draw_cup(0, lvl, 1);
                delay_ms(140);
            }
            draw_cup(0, 0, 1);
            delay_ms(320);
            g_state = ST_OVER;
            draw_over();
            return;
        }
    }

    draw_table();
    g_state = ST_CARD;
}

static void game_begin(void) {
    g_seed ^= ((uint32_t)ms_now() << 11) ^ 0xC0FFEE11UL;
    g_kings = 0;
    shuffle();
    deal_next();
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

    g_bat_raw = bat_read_raw();

    g_state = ST_TITLE;
    display_fill(COL_BLACK);
    draw_title();
}

void app_update(uint32_t frame) {
    (void)frame;

    uint8_t btn  = button_raw();
    uint8_t edge = (btn && !g_pressing) ? 1u : 0u;
    g_pressing = btn;

    switch(g_state) {
    case ST_TITLE:
        if(edge) game_begin();
        break;

    case ST_CARD:
        if(edge) deal_next();
        break;

    case ST_OVER:
        if(edge) {
            g_state = ST_TITLE;
            draw_title();
        }
        break;
    }

    if((frame % 150u) == 0u && g_state != ST_CARD) {
        g_bat_raw = bat_read_raw();
    }
}

void app_wake(void) {
    /* A half-dealt shoe means nothing once the screen has been dark and the
     * table has moved on — start clean. */
    g_pressing = 0;
    g_state    = ST_TITLE;
    draw_title();
}
