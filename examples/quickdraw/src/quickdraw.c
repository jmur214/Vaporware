/* quickdraw.c — wild-west reaction duel for RAZ DC25000
 *               (N32G031K8Q7-1 + GC9107 128x160 LCD, Vaporware SDK)
 *
 * The whole game is one honest moment: the wait.  A randomised 1.5-4.0 s
 * standoff, then DRAW flashes and you slap the button.  Your reaction in
 * milliseconds is the score, and drawing before the signal loses the round
 * outright — that total cost is what makes the wait actually tense.
 *
 * Depth, cheaply:
 *   - Fake-outs.  The screen twitches, or bluffs a decoy word (BANG, FIRE,
 *     NOW...) in the same loud band the real signal uses.  Reacting to the
 *     flash is no longer enough; you have to read it.
 *   - Best of five.  Each round is won by beating your ghost.
 *   - The ghost is your own personal-best draw, so you are always duelling
 *     the best version of yourself rather than an arbitrary number.
 *
 * ── Why this blocks instead of running on the frame loop ─────────────────
 * The framework ticks app_update() at ~30 fps, so sampling the button there
 * would quantise every reaction to the nearest 33 ms — useless when good and
 * bad draws are 60 ms apart.  The timed section therefore spins on
 * button_raw() with delay_ms(1) (which feeds the IWDG), giving ~1 ms
 * resolution.  The blocking window is bounded at roughly 5 s, far inside the
 * 26 s watchdog, and everything outside that window is an ordinary state.
 *
 * Personal best: NV_KEY_WINS.  Only one firmware is on the device at a time,
 * so sharing the slot machine's key costs nothing.
 */
#include "app.h"
#include "display.h"
#include "battery.h"
#include "system.h"

/* ===================================================================
 * Palette — high-noon desert
 * =================================================================== */
#define COL_SKY     COL_RGB(250, 205, 140)
#define COL_SUN     COL_RGB(255, 245, 150)
#define COL_SUNRIM  COL_RGB(255, 170,  50)
#define COL_SAND    COL_RGB(200, 145,  85)
#define COL_DARK    COL_RGB( 62,  38,  26)
#define COL_CACTUS  COL_RGB( 52, 100,  58)
#define COL_SIGNAL  COL_RGB(215,  30,  30)   /* the real thing           */
#define COL_DECOY   COL_RGB(195,  85,  20)   /* the bluff                */
#define COL_TWITCH  COL_RGB(255, 238, 200)   /* a flinch, no word        */
#define COL_TEXT    COL_RGB(255, 246, 225)
#define COL_WIN     COL_RGB( 55, 185,  80)
#define COL_LOSE    COL_RGB(205,  40,  40)
#define COL_GOLD    COL_RGB(255, 205,   0)
#define COL_PEND    COL_RGB(160, 118,  76)

/* ===================================================================
 * Layout (128x160)
 * =================================================================== */
#define GROUND_Y    96
#define BAND_Y      52      /* sits entirely in sky, clear of sun and    */
#define BAND_H      42      /* ground, so clearing it is one flat fill   */
#define SUN_CX      64
#define SUN_CY      30
#define SUN_R       15

/* The two duellists face each other across the sand, below everything the
 * signal touches, so firing never disturbs the band or the timing path. */
#define GUN_W       37
#define GUN_H       25
#define GUN_Y      120
#define GUN_L_X      6      /* the ghost, pointing right                 */
#define GUN_R_X     86      /* the player, pointing left                 */

#define COL_FLASH_A COL_RGB(255, 255, 240)
#define COL_FLASH_B COL_RGB(255, 240, 120)
#define COL_FLASH_C COL_RGB(255, 160,  40)

/* ===================================================================
 * Tuning
 * =================================================================== */
#define WAIT_MIN_MS   1500u
#define WAIT_SPAN_MS  2500u   /* ... so 1.5 s to 4.0 s                   */
#define REACT_MAX_MS  1200u   /* slower than this is a miss              */
#define DECOY_MS       170u   /* how long a bluff stays up               */
#define GHOST_DEFAULT  400u   /* first-run target, roughly average human */
#define ROUNDS           5u

#define NV_KEY_QD_BEST  NV_KEY_WINS

/* ===================================================================
 * 3x5 glyphs, drawn at an arbitrary integer scale
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

/* Milliseconds as four characters, e.g. "247" + "MS" drawn separately. */
static void ms_str(char buf[4], uint16_t v) {
    if(v > 999u) v = 999u;
    buf[0] = (char)('0' + (v / 100u) % 10u);
    buf[1] = (char)('0' + (v / 10u) % 10u);
    buf[2] = (char)('0' + v % 10u);
    buf[3] = '\0';
}

/* ===================================================================
 * RNG
 * =================================================================== */
static uint32_t g_seed = 0x5150C0DEUL;

static uint32_t rnd(uint32_t mod) {
    g_seed = g_seed * 1664525UL + 1013904223UL;
    return (g_seed >> 16) % mod;
}

/* ===================================================================
 * Scene
 * =================================================================== */
static void fill_circle(int16_t cx, int16_t cy, int16_t r, uint16_t col) {
    for(int16_t dy = -r; dy <= r; dy++) {
        int16_t y = (int16_t)(cy + dy);
        if(y < 0 || y >= LCD_HEIGHT) continue;

        int16_t half = 0;
        while((int16_t)((half + 1) * (half + 1) + dy * dy) <= (int16_t)(r * r)) half++;

        int16_t x0 = (int16_t)(cx - half);
        int16_t w  = (int16_t)(half * 2 + 1);
        if(x0 < 0) { w = (int16_t)(w + x0); x0 = 0; }
        if(x0 + w > LCD_WIDTH) w = (int16_t)(LCD_WIDTH - x0);
        if(w > 0) display_fill_rect((uint16_t)x0, (uint16_t)y, (uint16_t)w, 1, col);
    }
}

static void draw_cactus(uint16_t x, uint16_t base, uint16_t h) {
    display_fill_rect((uint16_t)(x + 5), (uint16_t)(base - h), 6, h, COL_CACTUS);
    /* left arm */
    display_fill_rect(x, (uint16_t)(base - h + 14), 3, 9, COL_CACTUS);
    display_fill_rect(x, (uint16_t)(base - h + 20), 6, 3, COL_CACTUS);
    /* right arm, set higher so the two do not mirror */
    display_fill_rect((uint16_t)(x + 13), (uint16_t)(base - h + 8), 3, 11, COL_CACTUS);
    display_fill_rect((uint16_t)(x + 10), (uint16_t)(base - h + 16), 6, 3, COL_CACTUS);
}

/* Revolver silhouette, GUN_W x GUN_H, built from rectangles because that is
 * all the SDK draws.  At this size the shape reads almost entirely from the
 * proportions of barrel, cylinder and grip, so those get the pixels and the
 * detail is left out.  The muzzle is at dx 0, so the default points LEFT and
 * flip turns it around. */
static void draw_revolver(uint16_t x, uint16_t y, uint8_t flip, uint16_t col) {
    static const uint8_t part[9][4] = {
        {  0,  6, 22,  5 },   /* barrel        */
        {  2,  4,  3,  2 },   /* front sight   */
        { 22,  3,  9, 11 },   /* cylinder      */
        { 31,  5,  5,  9 },   /* frame         */
        { 32,  1,  4,  4 },   /* hammer        */
        { 26, 14,  9,  2 },   /* trigger guard */
        { 30, 14,  6,  4 },   /* grip upper    */
        { 31, 18,  6,  4 },   /* grip mid      */
        { 32, 22,  5,  3 },   /* grip butt     */
    };

    for(uint8_t i = 0; i < 9; i++) {
        uint8_t dx = part[i][0], dy = part[i][1];
        uint8_t w  = part[i][2], h  = part[i][3];
        uint16_t px = flip ? (uint16_t)(x + (GUN_W - dx - w)) : (uint16_t)(x + dx);
        display_fill_rect(px, (uint16_t)(y + dy), w, h, col);
    }
}

/* Three tapering blocks off the muzzle, brightest where the barrel ends. */
static void muzzle_flash(uint16_t x, uint16_t y, uint8_t flip) {
    uint16_t my = (uint16_t)(y + 2);

    if(flip) {
        uint16_t m = (uint16_t)(x + GUN_W);
        display_fill_rect(m,                    my,      6, 13, COL_FLASH_A);
        display_fill_rect((uint16_t)(m + 6),    (uint16_t)(my + 2), 5,  9, COL_FLASH_B);
        display_fill_rect((uint16_t)(m + 11),   (uint16_t)(my + 4), 4,  5, COL_FLASH_C);
    } else {
        display_fill_rect((uint16_t)(x - 6),    my,      6, 13, COL_FLASH_A);
        display_fill_rect((uint16_t)(x - 11),   (uint16_t)(my + 2), 5,  9, COL_FLASH_B);
        display_fill_rect((uint16_t)(x - 15),   (uint16_t)(my + 4), 4,  5, COL_FLASH_C);
    }
}

static void draw_guns(void) {
    draw_revolver(GUN_L_X, GUN_Y, 1, COL_DARK);
    draw_revolver(GUN_R_X, GUN_Y, 0, COL_DARK);
}

/* Round outcomes: 0 pending, 1 won, 2 lost */
static uint8_t g_res[ROUNDS];

static void draw_pips(void) {
    for(uint8_t i = 0; i < ROUNDS; i++) {
        uint16_t c = (g_res[i] == 1u) ? COL_WIN
                   : (g_res[i] == 2u) ? COL_LOSE
                                      : COL_PEND;
        display_fill_rect((uint16_t)(12 + i * 22), 6, 16, 6, c);
    }
}

static void draw_scene(void) {
    display_fill_rect(0, 0, LCD_WIDTH, GROUND_Y, COL_SKY);
    fill_circle(SUN_CX, SUN_CY, SUN_R, COL_SUNRIM);
    fill_circle(SUN_CX, SUN_CY, SUN_R - 4, COL_SUN);

    display_fill_rect(0, GROUND_Y, LCD_WIDTH,
                      (uint16_t)(LCD_HEIGHT - GROUND_Y), COL_SAND);
    draw_cactus(2, 114, 16);
    draw_cactus(108, 112, 14);
    draw_guns();

    draw_pips();
}

/* The band is the only thing that ever changes during a standoff, and it
 * lives entirely inside the sky — so clearing it is one flat fill, fast
 * enough that a bluff can come and go without stalling the button poll. */
static void band(uint16_t bg, const char* word) {
    display_fill_rect(0, BAND_Y, LCD_WIDTH, BAND_H, bg);
    if(word) draw_text_mid(word, (uint16_t)(BAND_Y + 10), 5, COL_TEXT);
}

static void band_clear(void) {
    display_fill_rect(0, BAND_Y, LCD_WIDTH, BAND_H, COL_SKY);
}

/* ===================================================================
 * State
 * =================================================================== */
#define ST_TITLE   0
#define ST_PLAY    1
#define ST_RESULT  2
#define ST_MATCH   3

static uint8_t  g_state;
static uint8_t  g_round;
static uint8_t  g_wins;
static uint16_t g_ghost;        /* personal best draw, the time to beat  */
static uint16_t g_fastest;      /* fastest clean draw this match         */
static uint8_t  g_pressing;
static uint16_t g_bat_raw = BAT_FULL;

/* ===================================================================
 * Screens
 * =================================================================== */
static void draw_title(void) {
    display_fill_rect(0, 0, LCD_WIDTH, GROUND_Y, COL_SKY);
    fill_circle(SUN_CX, SUN_CY, SUN_R + 6, COL_SUNRIM);
    fill_circle(SUN_CX, SUN_CY, SUN_R + 2, COL_SUN);

    display_fill_rect(0, GROUND_Y, LCD_WIDTH,
                      (uint16_t)(LCD_HEIGHT - GROUND_Y), COL_SAND);
    draw_cactus(2, 114, 16);
    draw_cactus(108, 112, 14);

    draw_text_mid("QUICK", 14, 4, COL_DARK);
    draw_text_mid("DRAW", 40, 4, COL_DARK);

    char buf[4];
    ms_str(buf, g_ghost);
    draw_text_mid("BEAT", 68, 2, COL_DARK);
    draw_text(buf, 44, 80, 3, COL_GOLD);
    draw_text("MS", 82, 82, 2, COL_DARK);

    /* One revolver on the title, mid-shot, so the game announces itself. */
    draw_revolver(46, GUN_Y, 0, COL_DARK);
    muzzle_flash(46, GUN_Y, 0);

    draw_text_mid("TAP", 148, 2, COL_DARK);
}

static void draw_round_result(uint16_t rt, uint8_t fail, const char* why) {
    draw_scene();

    if(fail) {
        /* Fumbling the draw is still a loss, so the ghost is the one who
         * gets a shot off. */
        band(COL_LOSE, 0);
        draw_text_mid(why, (uint16_t)(BAND_Y + 12), 3, COL_TEXT);
        muzzle_flash(GUN_L_X, GUN_Y, 1);
    } else {
        char buf[4];
        ms_str(buf, rt);
        uint8_t won = (rt < g_ghost);

        band(won ? COL_WIN : COL_DARK, 0);
        draw_text(buf, 28, (uint16_t)(BAND_Y + 6), 5, COL_TEXT);
        draw_text("MS", 92, (uint16_t)(BAND_Y + 16), 3, COL_TEXT);

        /* Whoever was faster is the one who fires. */
        if(won) muzzle_flash(GUN_R_X, GUN_Y, 0);
        else    muzzle_flash(GUN_L_X, GUN_Y, 1);

        draw_text_mid(won ? "FASTER" : "TOO SLOW", 102, 2, COL_DARK);
    }
}

static void draw_match(void) {
    display_fill_rect(0, 0, LCD_WIDTH, GROUND_Y, COL_SKY);
    display_fill_rect(0, GROUND_Y, LCD_WIDTH,
                      (uint16_t)(LCD_HEIGHT - GROUND_Y), COL_SAND);
    draw_pips();

    uint8_t won_match = (g_wins >= 3u);
    draw_text_mid(won_match ? "YOU WIN" : "YOU LOSE", 24, 3,
                  won_match ? COL_WIN : COL_LOSE);

    char buf[4];
    buf[0] = (char)('0' + g_wins);
    buf[1] = '\0';
    draw_text_mid(buf, 48, 3, COL_DARK);
    draw_text_mid("OF 5", 70, 2, COL_DARK);

    if(g_fastest < 999u) {
        ms_str(buf, g_fastest);
        draw_text_mid("BEST DRAW", 90, 2, COL_DARK);
        draw_text(buf, 40, 104, 3, COL_DARK);
        draw_text("MS", 78, 106, 2, COL_DARK);

        if(g_fastest < g_ghost) {
            draw_text_mid("NEW RECORD", 128, 2, COL_GOLD);
        }
    }

    draw_text_mid("TAP", 146, 2, COL_DARK);
}

/* ===================================================================
 * One round — the timing-critical part, run to ~1 ms resolution
 * =================================================================== */
static const char* decoy_word(void) {
    switch(rnd(5)) {
    case 0:  return "BANG";
    case 1:  return "FIRE";
    case 2:  return "NOW";
    case 3:  return "HOLD";
    default: return "WAIT";
    }
}

static void run_round(void) {
    draw_scene();
    draw_text_mid("STEADY", 102, 2, COL_DARK);

    /* Demand a clean release first: holding the button into a round would
     * otherwise register instantly as jumping the gun. */
    while(button_raw()) delay_ms(1);
    delay_ms(150);

    uint16_t wait_ms = (uint16_t)(WAIT_MIN_MS + rnd(WAIT_SPAN_MS));

    /* Schedule bluffs, never inside the last 400 ms.  A decoy landing just
     * before the real signal is indistinguishable from it, which would make
     * the round a coin flip rather than a test of reading it. */
    uint16_t fake_at[2] = { 0, 0 };
    uint8_t  fakes = 0;
    if(g_round >= 1u && wait_ms > 1200u) {
        uint8_t n = (rnd(100) < 60u) ? (uint8_t)(1u + rnd(2)) : 0u;
        for(uint8_t i = 0; i < n; i++) {
            fake_at[i] = (uint16_t)(350u + rnd((uint32_t)(wait_ms - 750u)));
            fakes++;
        }
    }

    uint8_t  fired[2] = { 0, 0 };
    uint8_t  showing  = 0;
    uint16_t clear_at = 0;
    uint8_t  jumped   = 0;

    uint16_t t0 = ms_now();
    for(;;) {
        uint16_t el = (uint16_t)(ms_now() - t0);
        if(el >= wait_ms) break;

        if(button_raw()) { jumped = 1; break; }

        for(uint8_t i = 0; i < fakes; i++) {
            if(!fired[i] && el >= fake_at[i]) {
                fired[i] = 1;
                /* Half the bluffs are a wordless flinch, half are a word in
                 * the same loud band the real signal uses — so reacting to
                 * brightness alone is not enough, you have to read it. */
                if(rnd(2)) band(COL_DECOY, decoy_word());
                else       band(COL_TWITCH, 0);
                showing  = 1;
                clear_at = (uint16_t)(el + DECOY_MS);
            }
        }

        if(showing && el >= clear_at) {
            band_clear();
            showing = 0;
        }

        delay_ms(1);
    }

    if(showing) band_clear();

    if(jumped) {
        g_res[g_round] = 2u;
        draw_round_result(0, 1, "TOO SOON");
        /* Claim the press that ended the round, or the result screen is
         * dismissed by the very tap that caused it. */
        g_pressing = 1;
        g_state = ST_RESULT;
        return;
    }

    /* Start the clock as the signal begins painting, not after — the band is
     * a single ~14 ms fill, and timing from its start keeps every round
     * measured the same way. */
    uint16_t d0 = ms_now();
    band(COL_SIGNAL, "DRAW");

    uint16_t rt      = 0;
    uint8_t  timeout = 0;
    for(;;) {
        if(button_raw()) { rt = (uint16_t)(ms_now() - d0); break; }
        if((uint16_t)(ms_now() - d0) >= REACT_MAX_MS) { timeout = 1; break; }
        delay_ms(1);
    }

    if(timeout) {
        g_res[g_round] = 2u;
        draw_round_result(0, 1, "TOO SLOW");
    } else {
        uint8_t won = (rt < g_ghost);
        g_res[g_round] = won ? 1u : 2u;
        if(won) g_wins++;
        if(rt < g_fastest) g_fastest = rt;
        draw_round_result(rt, 0, 0);
    }

    g_pressing = 1;     /* see above: the drawing tap is not a dismissal */
    g_state = ST_RESULT;
}

/* ===================================================================
 * Match control
 * =================================================================== */
static void match_begin(void) {
    g_seed ^= ((uint32_t)ms_now() << 13) ^ 0x2BADD00DUL;

    for(uint8_t i = 0; i < ROUNDS; i++) g_res[i] = 0;
    g_round   = 0;
    g_wins    = 0;
    g_fastest = 999u;
    g_state   = ST_PLAY;
}

static void match_end(void) {
    if(g_fastest < g_ghost) {
        g_ghost = g_fastest;
        nv_write(NV_KEY_QD_BEST, g_ghost);
    }
    draw_match();
    g_state = ST_MATCH;
}

/* ===================================================================
 * Framework callbacks
 * =================================================================== */
static void on_hard_reset(void) {
    g_ghost = GHOST_DEFAULT;
    nv_write(NV_KEY_QD_BEST, GHOST_DEFAULT);
    g_state = ST_TITLE;
    draw_title();
}

void app_init(void) {
    display_recover();

    app_set_sleep_timeout(30000);
    app_set_hold_reset(10000, on_hard_reset);

    g_ghost = (uint16_t)nv_read(NV_KEY_QD_BEST, GHOST_DEFAULT);
    if(g_ghost == 0u || g_ghost > 999u) g_ghost = GHOST_DEFAULT;

    g_bat_raw = bat_read_raw();

    g_state = ST_TITLE;
    display_fill(COL_BLACK);
    draw_title();
}

void app_update(uint32_t frame) {
    (void)frame;

    uint8_t btn  = button_raw();
    uint8_t edge = (btn && !g_pressing) ? 1u : 0u;
    if(!btn) g_pressing = 0;
    if(btn)  g_pressing = 1;

    switch(g_state) {

    case ST_TITLE:
        if(edge) match_begin();
        break;

    case ST_PLAY:
        /* Blocks for the standoff and the draw, then leaves a result up. */
        run_round();
        break;

    case ST_RESULT:
        if(edge) {
            g_round++;
            if(g_round >= ROUNDS) match_end();
            else                  g_state = ST_PLAY;
        }
        break;

    case ST_MATCH:
        if(edge) {
            g_state = ST_TITLE;
            draw_title();
        }
        break;
    }

    if((frame % 150u) == 0u && g_state != ST_PLAY) {
        g_bat_raw = bat_read_raw();
    }
}

void app_wake(void) {
    /* A standoff cannot be resumed after the screen has been dark. */
    g_state    = ST_TITLE;
    g_pressing = 0;
    draw_title();
}
