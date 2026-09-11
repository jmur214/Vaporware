/* tetris.c — one-button Tetris for RAZ DC25000
 *            (N32G031K8Q7-1 + GC9107 128x160 LCD, Vaporware SDK)
 *
 * ── Making Tetris work with one button ──────────────────────────────────
 * Tetris normally wants four inputs: left, right, rotate, drop.  We have one.
 *
 * So the piece does the moving itself: it hovers above the stack and sweeps
 * left and right across the well, bouncing off the walls.  The player has two
 * verbs:
 *
 *   TAP  (press and release)      -> rotate 90 degrees
 *   HOLD (press for HOLD_MS)      -> hard-drop in the current column
 *
 * That maps the frequent action to the cheap gesture and the once-per-piece
 * action to the deliberate one, so a mistimed rotate can never dump a piece
 * in the wrong column.  There is no soft drop and no timer: the sweep is the
 * clock, and stalling gains nothing because the only way to score is to
 * commit.  Difficulty comes from the sweep accelerating as lines clear —
 * late on, hitting a one-column gap is genuinely hard.
 *
 * Well: 10 x 17 at 8 px per cell, which keeps blocks chunky enough to read on
 * a 128x160 panel while staying close to Tetris proportions.
 *
 * Rendering follows the house pattern: cells are painted individually, the
 * sweeping piece erases and redraws only the cells it occupies, and the full
 * well is repainted only when a lock or a line clear actually changes it.
 *
 * High score: NV_KEY_SPINS.  NV keys are per-flash — only one firmware is on
 * the device at a time — so reusing the slot machine's key is harmless here.
 */
#include "app.h"
#include "display.h"
#include "battery.h"
#include "system.h"

/* ===================================================================
 * Well geometry
 * =================================================================== */
#define COLS        10
#define ROWS        17
#define CELL         8
#define GAP          1          /* leaves a 7 px block, grid stays legible */
#define BOARD_X     24          /* (128 - 10*8) / 2                        */
#define BOARD_Y     16
#define BAR_H       14

/* ===================================================================
 * Palette — classic tetromino colours, indexed 1..7 (0 = empty)
 * =================================================================== */
#define COL_BG      COL_RGB( 16,  16,  22)
#define COL_GRID    COL_RGB( 34,  34,  44)
#define COL_BAR     COL_BLACK
#define COL_CHROME  COL_WHITE
#define COL_GHOST   COL_RGB( 70,  70,  86)
#define COL_GOLD    COL_RGB(255, 200,   0)
#define COL_DIM     COL_RGB(120, 120, 132)

static const uint16_t g_piece_col[8] = {
    COL_GRID,                   /* 0 — empty                              */
    COL_RGB(  0, 220, 230),     /* I — cyan                               */
    COL_RGB(245, 220,   0),     /* O — yellow                             */
    COL_RGB(180,  70, 245),     /* T — purple                             */
    COL_RGB( 50, 215,  80),     /* S — green                              */
    COL_RGB(240,  50,  50),     /* Z — red                                */
    COL_RGB( 50, 110, 255),     /* J — blue                               */
    COL_RGB(255, 150,  20),     /* L — orange                             */
};

/* ===================================================================
 * Tetrominoes
 *
 * Each rotation is a 4x4 bitmap packed into a uint16_t, one hex nibble per
 * row (high nibble = top row), and within a nibble bit 3 = leftmost column.
 * Written that way the hex literal is a picture of the piece: 0x0E40 is
 *     . . . .
 *     X X X .
 *     . X . .
 *     . . . .
 * =================================================================== */
static const uint16_t g_shape[7][4] = {
    /* I */ { 0x0F00, 0x2222, 0x0F00, 0x2222 },
    /* O */ { 0x6600, 0x6600, 0x6600, 0x6600 },
    /* T */ { 0x0E40, 0x4640, 0x04E0, 0x4C40 },
    /* S */ { 0x6C00, 0x8C40, 0x6C00, 0x8C40 },
    /* Z */ { 0xC600, 0x2640, 0xC600, 0x2640 },
    /* J */ { 0x8E00, 0x6440, 0x0E20, 0x44C0 },
    /* L */ { 0x2E00, 0x4460, 0x0E80, 0xC440 },
};

static uint8_t shape_cell(uint16_t s, uint8_t r, uint8_t c) {
    return (uint8_t)((s >> ((3u - r) * 4u + (3u - c))) & 1u);
}

/* Horizontal extent of a rotation, used to keep the sweep on the board. */
static void shape_cols(uint16_t s, int8_t* lo, int8_t* hi) {
    *lo = 3;
    *hi = 0;
    for(uint8_t r = 0; r < 4; r++) {
        for(uint8_t c = 0; c < 4; c++) {
            if(shape_cell(s, r, c)) {
                if((int8_t)c < *lo) *lo = (int8_t)c;
                if((int8_t)c > *hi) *hi = (int8_t)c;
            }
        }
    }
}

/* ===================================================================
 * Tuning
 * =================================================================== */
#define HOLD_MS        300u     /* press this long to drop                */
#define SWEEP_START    250u     /* ms per column at level 0               */
#define SWEEP_MIN       90u     /* fastest sweep                          */
#define SWEEP_STEP      14u     /* shaved per level                       */
#define LINES_PER_LEVEL 8u
#define LOCK_FLASH_MS  110u     /* white flash on cleared rows            */

#define NV_KEY_TETRIS_BEST  NV_KEY_SPINS

/* ===================================================================
 * 3x5 glyphs (same font as the other examples)
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

static void num_str(char buf[6], uint32_t v) {
    if(v > 99999u) v = 99999u;
    buf[0] = (char)('0' + (v / 10000u) % 10u);
    buf[1] = (char)('0' + (v / 1000u) % 10u);
    buf[2] = (char)('0' + (v / 100u) % 10u);
    buf[3] = (char)('0' + (v / 10u) % 10u);
    buf[4] = (char)('0' + v % 10u);
    buf[5] = '\0';
}

/* ===================================================================
 * RNG
 * =================================================================== */
static uint32_t g_seed = 0x7E7415UL;

static uint32_t rnd(uint32_t mod) {
    g_seed = g_seed * 1664525UL + 1013904223UL;
    return (g_seed >> 16) % mod;
}

/* ===================================================================
 * State
 * =================================================================== */
#define ST_TITLE  0
#define ST_PLAY   1
#define ST_OVER   2

static uint8_t  g_state;
static uint8_t  g_board[ROWS][COLS];

static uint8_t  g_piece;         /* current tetromino index 0..6         */
static uint8_t  g_rot;
static int8_t   g_x;             /* well column of the shape's column 0  */
static int8_t   g_dir;           /* sweep direction, +1 / -1             */
static uint8_t  g_next;

static uint32_t g_score;
static uint32_t g_lines;
static uint32_t g_best;
static uint8_t  g_new_best;
static uint8_t  g_level;

static uint16_t g_sweep_t0;
static uint16_t g_press_t0;
static uint8_t  g_pressing;
static uint8_t  g_rotated;       /* this press has already rotated       */

static uint16_t g_bat_raw = BAT_FULL;

/* ===================================================================
 * Cell painting
 * =================================================================== */
static void paint_cell(uint8_t r, uint8_t c, uint16_t col) {
    display_fill_rect((uint16_t)(BOARD_X + c * CELL),
                      (uint16_t)(BOARD_Y + r * CELL),
                      CELL - GAP, CELL - GAP, col);
}

static void paint_board_cell(uint8_t r, uint8_t c) {
    paint_cell(r, c, g_piece_col[g_board[r][c]]);
}

static void paint_board(void) {
    for(uint8_t r = 0; r < ROWS; r++) {
        for(uint8_t c = 0; c < COLS; c++) paint_board_cell(r, c);
    }
}

/* Draw or erase the hovering piece.  Erasing repaints whatever the well
 * holds underneath, so the piece can sweep over a high stack safely. */
static void paint_piece(uint8_t on) {
    uint16_t s = g_shape[g_piece][g_rot];
    for(uint8_t r = 0; r < 4; r++) {
        for(uint8_t c = 0; c < 4; c++) {
            if(!shape_cell(s, r, c)) continue;
            int8_t bc = (int8_t)(g_x + (int8_t)c);
            if(bc < 0 || bc >= COLS || r >= ROWS) continue;
            if(on) paint_cell(r, (uint8_t)bc, g_piece_col[g_piece + 1u]);
            else   paint_board_cell(r, (uint8_t)bc);
        }
    }
}

/* ===================================================================
 * Chrome
 * =================================================================== */
static void draw_next(void) {
    /* Preview lives in the left margin, at half cell size. */
    display_fill_rect(2, 28, 20, 20, COL_BG);
    uint16_t s = g_shape[g_next][0];
    for(uint8_t r = 0; r < 4; r++) {
        for(uint8_t c = 0; c < 4; c++) {
            if(shape_cell(s, r, c)) {
                display_fill_rect((uint16_t)(2 + c * 5), (uint16_t)(28 + r * 5),
                                  4, 4, g_piece_col[g_next + 1u]);
            }
        }
    }
}

static void draw_bar(void) {
    display_fill_rect(0, 0, LCD_WIDTH, BAR_H, COL_BAR);
    char buf[6];
    num_str(buf, g_score);
    draw_text(buf, 3, 3, 2, COL_CHROME);

    /* Level sits on the right so the eye can find it without hunting. */
    buf[0] = 'L';
    buf[1] = (char)('0' + (g_level / 10u) % 10u);
    buf[2] = (char)('0' + g_level % 10u);
    buf[3] = '\0';
    draw_text(buf, (uint16_t)(LCD_WIDTH - 26), 3, 2, COL_DIM);
}

/* Charge pip: fills while the button is held so hold-to-drop is legible
 * rather than a mystery delay. */
static void draw_charge(uint16_t held_ms) {
    uint16_t w = (held_ms >= HOLD_MS) ? 20u : (uint16_t)((held_ms * 20u) / HOLD_MS);
    display_fill_rect(2, 56, 20, 5, COL_GRID);
    if(w) display_fill_rect(2, 56, w, 5, COL_GOLD);
}

/* ===================================================================
 * Collision and locking
 * =================================================================== */
static uint8_t fits(uint16_t s, int8_t x, int8_t y) {
    for(uint8_t r = 0; r < 4; r++) {
        for(uint8_t c = 0; c < 4; c++) {
            if(!shape_cell(s, r, c)) continue;
            int8_t br = (int8_t)(y + (int8_t)r);
            int8_t bc = (int8_t)(x + (int8_t)c);
            if(br < 0 || br >= ROWS || bc < 0 || bc >= COLS) return 0;
            if(g_board[br][bc]) return 0;
        }
    }
    return 1;
}

/* Clear completed rows, shift everything above down, and return the count. */
static uint8_t clear_lines(void) {
    uint8_t cleared = 0;

    for(int8_t r = ROWS - 1; r >= 0; r--) {
        uint8_t full = 1;
        for(uint8_t c = 0; c < COLS; c++) {
            if(!g_board[r][c]) { full = 0; break; }
        }
        if(!full) continue;

        /* Flash the row before it goes, so a clear reads as an event. */
        for(uint8_t c = 0; c < COLS; c++) paint_cell((uint8_t)r, c, COL_WHITE);
        delay_ms(LOCK_FLASH_MS);

        for(int8_t rr = r; rr > 0; rr--) {
            for(uint8_t c = 0; c < COLS; c++) g_board[rr][c] = g_board[rr - 1][c];
        }
        for(uint8_t c = 0; c < COLS; c++) g_board[0][c] = 0;

        cleared++;
        r++;    /* re-test this row, it holds new contents now */
    }

    return cleared;
}

static void sweep_reset(void) {
    int8_t lo, hi;
    shape_cols(g_shape[g_piece][g_rot], &lo, &hi);
    g_x   = (int8_t)(-lo);
    g_dir = 1;
    g_sweep_t0 = ms_now();
}

static void spawn(void) {
    g_piece = g_next;
    g_next  = (uint8_t)rnd(7);
    g_rot   = 0;
    sweep_reset();
    draw_next();
}

static void game_over(void) {
    if(g_score > g_best) {
        g_best     = g_score;
        g_new_best = 1;
    }
    if(g_new_best) {
        nv_write(NV_KEY_TETRIS_BEST, g_best);
        g_new_best = 0;
    }

    g_state = ST_OVER;

    display_fill_rect(0, 50, LCD_WIDTH, 60, COL_BAR);
    draw_text_mid("GAME", 54, 3, COL_WHITE);
    draw_text_mid("OVER", 74, 3, COL_WHITE);

    char buf[6];
    num_str(buf, g_score);
    draw_text_mid(buf, 94, 2, COL_GOLD);
}

static void hard_drop(void) {
    uint16_t s = g_shape[g_piece][g_rot];

    /* Nowhere to put it at all — the stack has reached the ceiling. */
    if(!fits(s, g_x, 0)) {
        game_over();
        return;
    }

    int8_t y = 0;
    while(fits(s, g_x, (int8_t)(y + 1))) y++;

    for(uint8_t r = 0; r < 4; r++) {
        for(uint8_t c = 0; c < 4; c++) {
            if(!shape_cell(s, r, c)) continue;
            g_board[y + (int8_t)r][g_x + (int8_t)c] = (uint8_t)(g_piece + 1u);
        }
    }

    uint8_t cleared = clear_lines();
    if(cleared) {
        g_lines += cleared;
        /* Standard-ish scoring: clearing more at once is worth far more. */
        static const uint16_t award[5] = { 0, 100, 300, 500, 800 };
        g_score += award[cleared];
        g_level = (uint8_t)(g_lines / LINES_PER_LEVEL);
    } else {
        g_score += 10u;     /* placement is worth a little on its own */
    }

    paint_board();
    draw_bar();

    /* Topped out: the top four rows are the lane the next piece sweeps
     * through, so anything left resting up there means there is nowhere for
     * it to travel.  Checking only "does a piece fit at row 0" was not
     * enough — with a full well, pieces kept fitting in the top rows and the
     * game ran on forever after the player had plainly lost. */
    for(uint8_t r = 0; r < 4; r++) {
        for(uint8_t c = 0; c < COLS; c++) {
            if(g_board[r][c]) {
                game_over();
                return;
            }
        }
    }

    spawn();
    paint_piece(1);
}

/* ===================================================================
 * Sweep
 * =================================================================== */
static uint16_t sweep_period(void) {
    uint32_t p = SWEEP_START - (uint32_t)g_level * SWEEP_STEP;
    if(p < SWEEP_MIN) p = SWEEP_MIN;
    return (uint16_t)p;
}

static void sweep_step(void) {
    int8_t lo, hi;
    shape_cols(g_shape[g_piece][g_rot], &lo, &hi);

    int8_t min_x = (int8_t)(-lo);
    int8_t max_x = (int8_t)(COLS - 1 - hi);

    int8_t nx = (int8_t)(g_x + g_dir);
    if(nx < min_x) { nx = min_x; g_dir = 1; }
    else if(nx > max_x) { nx = max_x; g_dir = -1; }

    if(nx != g_x) {
        paint_piece(0);
        g_x = nx;
        paint_piece(1);
    }
}

static void rotate(void) {
    paint_piece(0);
    g_rot = (uint8_t)((g_rot + 1u) & 3u);

    /* Keep the new rotation inside the well — the shape's extent changes. */
    int8_t lo, hi;
    shape_cols(g_shape[g_piece][g_rot], &lo, &hi);
    if(g_x < (int8_t)(-lo)) g_x = (int8_t)(-lo);
    if(g_x > (int8_t)(COLS - 1 - hi)) g_x = (int8_t)(COLS - 1 - hi);

    paint_piece(1);
}

/* ===================================================================
 * Screens
 * =================================================================== */
static void draw_title(void) {
    display_fill(COL_BG);

    /* Title letters take the tetromino colours, one each. */
    static const char t[] = "TETRIS";
    uint16_t x = (uint16_t)((LCD_WIDTH - text_w(t, 4)) / 2);
    for(uint8_t i = 0; i < 6; i++) {
        const uint8_t* bm = glyph_for(t[i]);
        if(bm) draw_glyph(bm, (uint16_t)(x + i * CELL_W(4)), 30, 4,
                          g_piece_col[i + 1u]);
    }

    draw_text_mid("TAP DROP", 74, 2, COL_CHROME);
    draw_text_mid("HOLD ROTATE", 90, 2, COL_CHROME);

    if(g_best) {
        char buf[6];
        num_str(buf, g_best);
        draw_text_mid("BEST", 118, 2, COL_GOLD);
        draw_text_mid(buf, 134, 2, COL_GOLD);
    }
}

static void game_begin(void) {
    g_seed ^= ((uint32_t)ms_now() << 7) ^ 0x1234ABCDUL;

    for(uint8_t r = 0; r < ROWS; r++) {
        for(uint8_t c = 0; c < COLS; c++) g_board[r][c] = 0;
    }

    g_score    = 0;
    g_lines    = 0;
    g_level    = 0;
    g_new_best = 0;
    g_next     = (uint8_t)rnd(7);

    display_fill(COL_BG);
    paint_board();
    draw_bar();
    draw_charge(0);

    spawn();
    paint_piece(1);

    g_state = ST_PLAY;
}

/* ===================================================================
 * Framework callbacks
 * =================================================================== */
static void on_hard_reset(void) {
    g_best     = 0;
    g_new_best = 0;
    nv_write(NV_KEY_TETRIS_BEST, 0);
    g_state = ST_TITLE;
    draw_title();
}

void app_init(void) {
    display_recover();

    app_set_sleep_timeout(30000);
    app_set_hold_reset(10000, on_hard_reset);

    g_best    = nv_read(NV_KEY_TETRIS_BEST, 0);
    g_bat_raw = bat_read_raw();

    g_state = ST_TITLE;
    display_fill(COL_BLACK);
    draw_title();
}

void app_update(uint32_t frame) {
    (void)frame;

    uint8_t btn = button_raw();

    switch(g_state) {

    case ST_TITLE:
        if(btn && !g_pressing) {
            g_pressing = 1;
            game_begin();
        } else if(!btn) {
            g_pressing = 0;
        }
        break;

    case ST_PLAY: {
        /* Tap drops, hold rotates.
         *
         * The drop is the timing-critical action — it has to land while the
         * piece is over the column you want — so it gets the gesture with the
         * least latency.  A tap resolves on release, within about 60 ms of
         * intent; a hold cannot resolve before its threshold, so putting the
         * drop there made hitting a column almost impossible.
         *
         * Holding past the threshold rotates and re-arms, so keeping the
         * button down cycles through the rotations. */
        if(btn && !g_pressing) {
            g_pressing = 1;
            g_rotated  = 0;
            g_press_t0 = ms_now();
        } else if(btn && g_pressing) {
            uint16_t held = (uint16_t)(ms_now() - g_press_t0);
            draw_charge(held);
            if(held >= HOLD_MS) {
                rotate();
                g_rotated  = 1;
                g_press_t0 = ms_now();   /* re-arm: hold to keep rotating */
                draw_charge(0);
            }
        } else if(!btn && g_pressing) {
            g_pressing = 0;
            draw_charge(0);
            if(!g_rotated) hard_drop();
        }

        if(g_state != ST_PLAY) break;   /* hard_drop may have ended the game */

        if((uint16_t)(ms_now() - g_sweep_t0) >= sweep_period()) {
            g_sweep_t0 = ms_now();
            sweep_step();
        }
        break;
    }

    case ST_OVER:
        if(btn && !g_pressing) {
            g_pressing = 1;
            g_state    = ST_TITLE;
            draw_title();
        } else if(!btn) {
            g_pressing = 0;
        }
        break;
    }

    if((frame % 150u) == 0u && g_state != ST_PLAY) {
        g_bat_raw = bat_read_raw();
    }
}

void app_wake(void) {
    /* A half-played well cannot be resumed meaningfully after the screen has
     * been dark — send the player back to the title. */
    g_state    = ST_TITLE;
    g_pressing = 0;
    draw_title();
}
