/* dino.c — one-button Chrome-Dino-style runner for RAZ DC25000
 *          (N32G031K8Q7-1 + GC9107 128×160 LCD, Vaporware SDK)
 *
 * Controls:  PA7 button = JUMP (single press; no double-jump)
 * Display:   128×160 RGB565, MADCTL=0x98 → R/B swapped (use COL_RGB).
 *
 * Aesthetic: Chrome offline dino — white sky, dark grey dino and ground,
 * green cacti for a little color-screen flex.
 *
 * Rendering model (follows flappy.c):
 *   - Obstacles live in a fixed "band" of BAND_H rows above the ground line.
 *     Each scroll tick redraws the obstacle's visible columns as 1×BAND_H
 *     strips (single draw_image per column → no partial-column tearing).
 *   - The dino is an opaque 18×20 blit (white background baked in) so
 *     redrawing it in place needs no separate erase.
 *   - Physics at PHYS_MS=33 ms ticks with catch-up (max 4/frame) and a
 *     sticky jump latch so short presses are never lost.
 *
 * High score: NV_KEY_APP_0 (free app slot — flappy owns NV_KEY_HIGH_SCORE).
 */
#include "app.h"      /* framework: app_init/update/wake, button_*, nv_* */
#include "display.h"
#include "battery.h"
#include "system.h"

/* ===================================================================
 * Colors (BGR-swap corrected via COL_RGB)
 * =================================================================== */
#define COL_BG      COL_WHITE               /* sky / page background      */
#define COL_FG      COL_RGB(60, 60, 60)     /* dino, ground, dirt, digits */
#define COL_CACT    COL_RGB(30, 130, 55)    /* cactus green               */
#define COL_BAR     COL_BLACK               /* score bar backdrop         */
#define COL_SCORE   COL_WHITE               /* score digits on the bar    */
#define COL_GOLD    COL_RGB(255, 215, 0)    /* best-score digits          */
#define COL_DEAD    COL_RGB(255, 0, 0)      /* GAME OVER letters          */
#define COL_DIM     COL_RGB(70, 70, 70)     /* panel divider              */

/* ===================================================================
 * Layout  (portrait 128×160)
 * =================================================================== */
#define SCORE_BAR_H  14
#define SCORE_Y       3
#define GROUND_Y    132              /* dino/obstacle feet sit on this row */
#define GROUND_H      2              /* ground line thickness              */
#define PLAY_TOP    SCORE_BAR_H

#define DINO_X       14
#define DINO_W       18
#define DINO_H       20

#define BAND_H       24              /* tallest obstacle; strip height     */
#define BAND_TOP    (GROUND_Y - BAND_H)

/* ===================================================================
 * Physics (1/8-pixel fixed point, one tick per 33 ms frame)
 *   JUMP_FP = −9 px/tick launch → ~40 px apex, ~18 ticks (~0.6 s) air
 * =================================================================== */
#define FP_SHIFT     3
#define GRAVITY_FP   8               /* 1 px/tick²  */
#define JUMP_FP     (-72)            /* −9 px/tick  */
#define MAX_FALL_FP  80              /* 10 px/tick  */
#define PHYS_MS      33u

/* Speed ramp: px/tick scroll speed by score */
#define SPEED_BASE   3
#define SPEED_MAX    5

/* Score cap (display supports 4 digits) */
#define SCORE_CAP    9999u

/* ===================================================================
 * RNG (LCG, same constants as flappy)
 * =================================================================== */
static uint32_t g_seed = 0xD1905EEDUL;

static uint32_t rnd(uint32_t mod)
{
    g_seed = g_seed * 1664525UL + 1013904223UL;
    return (g_seed >> 16) % mod;
}

/* ===================================================================
 * Dino sprites — 18×20 opaque blits (white background baked in).
 * 3 frames: neutral/jump (legs down), run A (left leg up), run B.
 * =================================================================== */
#define _T COL_BG   /* white background            */
#define _D COL_FG   /* dark grey body              */
#define _W COL_BG   /* eye (white dot inside head) */

static const uint16_t spr_dino_jump[18*20] = {
/* r0 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_T,
/* r1 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_W,_D,_D,_D,_D,_T,
/* r2 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_T,
/* r3 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_T,_T,_T,
/* r4 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_T,
/* r5 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_T,_T,_T,_T,
/* r6 */ _D,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,
/* r7 */ _D,_D,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,
/* r8 */ _D,_D,_D,_T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,
/* r9 */ _T,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,
/*r10 */ _T,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,
/*r11 */ _T,_T,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,
/*r12 */ _T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,_T,
/*r13 */ _T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,_T,_T,
/*r14 */ _T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,_T,_T,_T,
/*r15 */ _T,_T,_T,_T,_T,_D,_D,_T,_D,_D,_T,_T,_T,_T,_T,_T,_T,_T,
/*r16 */ _T,_T,_T,_T,_T,_D,_D,_T,_D,_D,_T,_T,_T,_T,_T,_T,_T,_T,
/*r17 */ _T,_T,_T,_T,_T,_D,_T,_T,_T,_D,_T,_T,_T,_T,_T,_T,_T,_T,
/*r18 */ _T,_T,_T,_T,_T,_D,_T,_T,_T,_D,_T,_T,_T,_T,_T,_T,_T,_T,
/*r19 */ _T,_T,_T,_T,_T,_D,_D,_T,_T,_D,_D,_T,_T,_T,_T,_T,_T,_T,
};

static const uint16_t spr_dino_run_a[18*20] = {
/* r0 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_T,
/* r1 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_W,_D,_D,_D,_D,_T,
/* r2 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_T,
/* r3 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_T,_T,_T,
/* r4 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_T,
/* r5 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_T,_T,_T,_T,
/* r6 */ _D,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,
/* r7 */ _D,_D,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,
/* r8 */ _D,_D,_D,_T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,
/* r9 */ _T,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,
/*r10 */ _T,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,
/*r11 */ _T,_T,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,
/*r12 */ _T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,_T,
/*r13 */ _T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,_T,_T,
/*r14 */ _T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,_T,_T,_T,
/*r15 */ _T,_T,_T,_T,_T,_D,_D,_T,_D,_D,_T,_T,_T,_T,_T,_T,_T,_T,
/*r16 */ _T,_T,_T,_T,_T,_D,_D,_T,_D,_D,_T,_T,_T,_T,_T,_T,_T,_T,
/*r17 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_T,_T,_T,_T,_T,_T,_T,_T,
/*r18 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_T,_T,_T,_T,_T,_T,_T,_T,
/*r19 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_T,_T,_T,_T,_T,_T,_T,
};

static const uint16_t spr_dino_run_b[18*20] = {
/* r0 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_T,
/* r1 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_W,_D,_D,_D,_D,_T,
/* r2 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_T,
/* r3 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_T,_T,_T,
/* r4 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_T,
/* r5 */ _T,_T,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_T,_T,_T,_T,
/* r6 */ _D,_T,_T,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,
/* r7 */ _D,_D,_T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,
/* r8 */ _D,_D,_D,_T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,
/* r9 */ _T,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,
/*r10 */ _T,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,
/*r11 */ _T,_T,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,
/*r12 */ _T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,_T,
/*r13 */ _T,_T,_T,_T,_D,_D,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,_T,_T,
/*r14 */ _T,_T,_T,_T,_T,_D,_D,_D,_D,_D,_T,_T,_T,_T,_T,_T,_T,_T,
/*r15 */ _T,_T,_T,_T,_T,_D,_D,_T,_D,_D,_T,_T,_T,_T,_T,_T,_T,_T,
/*r16 */ _T,_T,_T,_T,_T,_D,_D,_T,_D,_D,_T,_T,_T,_T,_T,_T,_T,_T,
/*r17 */ _T,_T,_T,_T,_T,_D,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
/*r18 */ _T,_T,_T,_T,_T,_D,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
/*r19 */ _T,_T,_T,_T,_T,_D,_D,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,_T,
};

#undef _T
#undef _D
#undef _W

/* ===================================================================
 * Cactus sprites — transparent = COL_BG, rendered via column strips.
 * =================================================================== */
#define _T COL_BG
#define _C COL_CACT

/* Small cactus 8×16 */
static const uint16_t spr_cact_small[8*16] = {
/* r0 */ _T,_T,_T,_C,_C,_T,_T,_T,
/* r1 */ _T,_T,_T,_C,_C,_T,_T,_T,
/* r2 */ _T,_T,_T,_C,_C,_T,_C,_C,
/* r3 */ _C,_C,_T,_C,_C,_T,_C,_C,
/* r4 */ _C,_C,_T,_C,_C,_T,_C,_C,
/* r5 */ _C,_C,_T,_C,_C,_C,_C,_C,
/* r6 */ _C,_C,_C,_C,_C,_T,_T,_T,
/* r7 */ _T,_T,_T,_C,_C,_T,_T,_T,
/* r8 */ _T,_T,_T,_C,_C,_T,_T,_T,
/* r9 */ _T,_T,_T,_C,_C,_T,_T,_T,
/*r10 */ _T,_T,_T,_C,_C,_T,_T,_T,
/*r11 */ _T,_T,_T,_C,_C,_T,_T,_T,
/*r12 */ _T,_T,_T,_C,_C,_T,_T,_T,
/*r13 */ _T,_T,_T,_C,_C,_T,_T,_T,
/*r14 */ _T,_T,_T,_C,_C,_T,_T,_T,
/*r15 */ _T,_T,_T,_C,_C,_T,_T,_T,
};

/* Large cactus 12×24 */
static const uint16_t spr_cact_large[12*24] = {
/* r0 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/* r1 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/* r2 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/* r3 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_C,_C,_T,
/* r4 */ _C,_C,_T,_T,_T,_C,_C,_C,_T,_C,_C,_T,
/* r5 */ _C,_C,_T,_T,_T,_C,_C,_C,_T,_C,_C,_T,
/* r6 */ _C,_C,_T,_T,_T,_C,_C,_C,_T,_C,_C,_T,
/* r7 */ _C,_C,_T,_T,_T,_C,_C,_C,_C,_C,_C,_T,
/* r8 */ _C,_C,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/* r9 */ _C,_C,_C,_C,_C,_C,_C,_C,_T,_T,_T,_T,
/*r10 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/*r11 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/*r12 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/*r13 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/*r14 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/*r15 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/*r16 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/*r17 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/*r18 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/*r19 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/*r20 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/*r21 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/*r22 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
/*r23 */ _T,_T,_T,_T,_T,_C,_C,_C,_T,_T,_T,_T,
};

#undef _T
#undef _C

/* ===================================================================
 * Obstacle types — up to 2 sprite parts (for the double cluster)
 * =================================================================== */
typedef struct {
    const uint16_t *spr;
    uint8_t dx;      /* x offset of this part within the obstacle */
    uint8_t w, h;
} ObstPart;

typedef struct {
    ObstPart part[2];
    uint8_t  nparts;
    uint8_t  w;      /* total width  */
    uint8_t  h;      /* tallest part */
} ObstType;

static const ObstType g_obst_types[] = {
    { { { spr_cact_small, 0,  8, 16 }, { 0 } },                          1,  8, 16 },
    { { { spr_cact_large, 0, 12, 24 }, { 0 } },                          1, 12, 24 },
    { { { spr_cact_small, 0,  8, 16 }, { spr_cact_small, 10, 8, 16 } },  2, 18, 16 },
};
#define OBST_NTYPES  3
#define OBST_COUNT   2
#define OBST_MAX_W   18

typedef struct {
    int16_t x;          /* left edge on screen (can be ≥ LCD_WIDTH)   */
    uint8_t type;
} Obst;
static Obst g_obst[OBST_COUNT];

/* ===================================================================
 * Column-strip rendering for the obstacle band
 * =================================================================== */
static uint16_t g_strip_buf[BAND_H];

/* Fill g_strip_buf for screen column c of obstacle o (background white). */
static void strip_fill_obst(const Obst *o, int c)
{
    const ObstType *t = &g_obst_types[o->type];
    for (int i = 0; i < BAND_H; i++) g_strip_buf[i] = COL_BG;

    for (int p = 0; p < t->nparts; p++) {
        const ObstPart *pt = &t->part[p];
        int rel = c - (int)o->x - (int)pt->dx;
        if (rel < 0 || rel >= (int)pt->w) continue;
        int top = BAND_H - (int)pt->h;    /* part sits on the ground */
        for (int r = 0; r < (int)pt->h; r++) {
            uint16_t px = pt->spr[r * pt->w + rel];
            if (px != COL_BG) g_strip_buf[top + r] = px;
        }
    }
}

static void send_strip(int x)
{
    if (x < 0 || x >= LCD_WIDTH) return;
    display_draw_image(g_strip_buf, (uint16_t)x, (uint16_t)BAND_TOP,
                       1, (uint16_t)BAND_H);
}

/* Erase a horizontal run of band columns back to white. */
static void band_erase(int x, int w)
{
    if (x < 0) { w += x; x = 0; }
    if (w <= 0 || x >= LCD_WIDTH) return;
    if (x + w > LCD_WIDTH) w = LCD_WIDTH - x;
    display_fill_rect((uint16_t)x, (uint16_t)BAND_TOP,
                      (uint16_t)w, (uint16_t)BAND_H, COL_BG);
}

/* Redraw every visible column of an obstacle at its current position. */
static void obst_render(const Obst *o)
{
    const ObstType *t = &g_obst_types[o->type];
    for (int c = (int)o->x; c < (int)o->x + (int)t->w; c++) {
        if (c < 0 || c >= LCD_WIDTH) continue;
        strip_fill_obst(o, c);
        send_strip(c);
    }
}

/* ===================================================================
 * Obstacle spawn / scroll
 * =================================================================== */
static uint8_t g_speed = SPEED_BASE;

/* Place obstacle idx off-screen right of anchor_x with a jumpable gap. */
static void obst_reset(int idx, int anchor_x)
{
    int sep = 60 + (int)g_speed * 20 + (int)rnd(100);
    int x   = anchor_x + sep;
    if (x < LCD_WIDTH) x = LCD_WIDTH;
    g_obst[idx].x    = (int16_t)x;
    g_obst[idx].type = (uint8_t)rnd(OBST_NTYPES);
}

/* Scroll one obstacle left by g_speed. Returns 1 when fully off-screen. */
static int obst_scroll(Obst *o)
{
    const ObstType *t = &g_obst_types[o->type];
    int old_x = (int)o->x;
    int new_x = old_x - (int)g_speed;

    if (new_x + (int)t->w <= 0) {
        band_erase(0, old_x + (int)t->w);   /* wipe last visible columns */
        return 1;
    }

    /* Erase the strip the obstacle just vacated on its right edge. */
    band_erase(new_x + (int)t->w, (int)g_speed);

    o->x = (int16_t)new_x;
    obst_render(o);
    return 0;
}

/* ===================================================================
 * Dino rendering
 * =================================================================== */
static void dino_draw(int y, const uint16_t *frame)
{
    display_draw_image(frame, DINO_X, (uint16_t)y, DINO_W, DINO_H);
}

static void dino_erase(int y)
{
    display_fill_rect(DINO_X, (uint16_t)y, DINO_W, DINO_H, COL_BG);
}

/* ===================================================================
 * Font — 3×5 glyphs drawn at 2× (6×10 px), stride 8 px.
 * =================================================================== */
static const uint8_t g_digits[10][5] = {
    {0x7,0x5,0x5,0x5,0x7},{0x2,0x6,0x2,0x2,0x7},{0x7,0x1,0x7,0x4,0x7},
    {0x7,0x1,0x7,0x1,0x7},{0x5,0x5,0x7,0x1,0x1},{0x7,0x4,0x7,0x1,0x7},
    {0x7,0x4,0x7,0x5,0x7},{0x7,0x1,0x1,0x1,0x1},{0x7,0x5,0x7,0x5,0x7},
    {0x7,0x5,0x7,0x1,0x7},
};

enum { FG_, FA_, FM_, FE_, FO_, FV_, FR_, FS_, FB_, FT_, FJ_, FU_, FP_ };
static const uint8_t g_letters[13][5] = {
    {0x7,0x4,0x5,0x5,0x7}, /* G */
    {0x2,0x5,0x7,0x5,0x5}, /* A */
    {0x5,0x7,0x7,0x5,0x5}, /* M */
    {0x7,0x4,0x6,0x4,0x7}, /* E */
    {0x7,0x5,0x5,0x5,0x7}, /* O */
    {0x5,0x5,0x5,0x5,0x2}, /* V */
    {0x7,0x5,0x7,0x6,0x5}, /* R */
    {0x7,0x4,0x7,0x1,0x7}, /* S */
    {0x6,0x5,0x6,0x5,0x6}, /* B */
    {0x7,0x2,0x2,0x2,0x2}, /* T */
    {0x7,0x1,0x1,0x5,0x7}, /* J */
    {0x5,0x5,0x5,0x5,0x7}, /* U */
    {0x7,0x5,0x7,0x4,0x4}, /* P */
};

static void draw_glyph(const uint8_t bm[5], uint16_t px, uint16_t py,
                       uint16_t fg, uint16_t bg)
{
    for (int r = 0; r < 5; r++) {
        uint8_t b = bm[r];
        for (int c = 0; c < 3; c++)
            display_fill_rect(px + (uint16_t)(c * 2), py + (uint16_t)(r * 2),
                              2, 2, ((b >> (2 - c)) & 1) ? fg : bg);
    }
}

/* Draw a word from letter indices, stride 8 px; returns end x. */
static uint16_t draw_word(const uint8_t *idx, int n, uint16_t x, uint16_t y,
                          uint16_t fg, uint16_t bg)
{
    for (int i = 0; i < n; i++) {
        draw_glyph(g_letters[idx[i]], x, y, fg, bg);
        x += 8;
    }
    return x;
}

/* Score rendering — up to 4 digits, centered */
static uint16_t score_px_width(uint32_t sc)
{
    return (sc < 10U) ? 6U : (sc < 100U) ? 14U : (sc < 1000U) ? 22U : 30U;
}

static void draw_score_at(uint32_t sc, uint16_t y, uint16_t fg, uint16_t bg)
{
    /* Clear max-width area so shrinking/growing digit counts leave no ghosts */
    uint16_t clear_x = (uint16_t)((LCD_WIDTH - 30U) / 2);
    display_fill_rect(clear_x, y, 30, 10, bg);

    uint16_t x = (uint16_t)((LCD_WIDTH - score_px_width(sc)) / 2);
    if (sc >= 1000U) { draw_glyph(g_digits[(sc/1000)%10], x, y, fg, bg); x += 8; }
    if (sc >=  100U) { draw_glyph(g_digits[(sc/100) %10], x, y, fg, bg); x += 8; }
    if (sc >=   10U) { draw_glyph(g_digits[(sc/10)  %10], x, y, fg, bg); x += 8; }
                       draw_glyph(g_digits[ sc       %10], x, y, fg, bg);
}

/* ===================================================================
 * Battery icon on the score bar (same layout as flappy)
 * =================================================================== */
static uint16_t g_bat_raw = BAT_FULL;

static void draw_bat(void)
{
    uint16_t raw = g_bat_raw;
    uint16_t dim = COL_RGB(35, 35, 35);
    uint16_t c0, c1, c2;

    if      (raw >= BAT_FULL) { c0 = COL_GREEN;  c1 = COL_GREEN; c2 = COL_GREEN; }
    else if (raw >= BAT_WARN) { c0 = COL_YELLOW; c1 = COL_YELLOW; c2 = dim;      }
    else if (raw >= BAT_CRIT) { c0 = COL_DEAD;   c1 = dim;        c2 = dim;      }
    else                      { c0 = dim;        c1 = dim;        c2 = dim;      }

    display_fill_rect(108, 3, 17, 8, COL_BAR);
    display_fill_rect(108, 3, 17, 1, COL_SCORE);
    display_fill_rect(108,10, 17, 1, COL_SCORE);
    display_fill_rect(108, 3,  1, 8, COL_SCORE);
    display_fill_rect(124, 3,  1, 8, COL_SCORE);
    display_fill_rect(125, 5,  2, 4, COL_SCORE);
    display_fill_rect(109, 4, 4, 6, c0);
    display_fill_rect(114, 4, 4, 6, c1);
    display_fill_rect(119, 4, 4, 6, c2);
}

static void draw_score(uint32_t sc)
{
    draw_score_at(sc, SCORE_Y, COL_SCORE, COL_BAR);
    draw_bat();
}

/* ===================================================================
 * Scene / overlays
 * =================================================================== */
static void draw_ground(void)
{
    display_fill_rect(0, GROUND_Y, LCD_WIDTH, GROUND_H, COL_FG);
    /* Static dirt speckles below the line */
    display_fill_rect(0, GROUND_Y + GROUND_H, LCD_WIDTH,
                      (uint16_t)(LCD_HEIGHT - GROUND_Y - GROUND_H), COL_BG);
    for (int i = 0; i < 26; i++) {
        uint16_t dx = (uint16_t)((i * 37 + 11) % LCD_WIDTH);
        uint16_t dy = (uint16_t)(GROUND_Y + 5 + ((i * 53 + 7) % 20));
        display_fill_rect(dx, dy, 2, 1, COL_FG);
    }
}

static void scene_draw(int dino_y, const uint16_t *frame, uint32_t score)
{
    display_fill_rect(0, 0, LCD_WIDTH, SCORE_BAR_H, COL_BAR);
    display_fill_rect(0, PLAY_TOP, LCD_WIDTH,
                      (uint16_t)(GROUND_Y - PLAY_TOP), COL_BG);
    draw_ground();
    for (int i = 0; i < OBST_COUNT; i++)
        if (g_obst[i].x < LCD_WIDTH) obst_render(&g_obst[i]);
    dino_draw(dino_y, frame);
    draw_score(score);
}

static void draw_game_over(uint32_t score, uint32_t best)
{
    const uint16_t px = 14, py = 42, pw = 100, ph = 66;
    display_fill_rect(px, py, pw, ph, COL_BAR);
    display_fill_rect(px,      py,      pw, 1,  COL_SCORE);
    display_fill_rect(px,      py+ph-1, pw, 1,  COL_SCORE);
    display_fill_rect(px,      py,      1,  ph, COL_SCORE);
    display_fill_rect(px+pw-1, py,      1,  ph, COL_SCORE);

    /* "GAME OVER" — 9 cells at stride 8 = 70 px, centered */
    {
        static const uint8_t w1[] = { FG_, FA_, FM_, FE_ };
        static const uint8_t w2[] = { FO_, FV_, FE_, FR_ };
        uint16_t lx = (uint16_t)((LCD_WIDTH - 70) / 2);
        lx = draw_word(w1, 4, lx, 48, COL_DEAD, COL_BAR);
        draw_word(w2, 4, lx + 6, 48, COL_DEAD, COL_BAR);
    }

    draw_score_at(score, 61, COL_SCORE, COL_BAR);

    display_fill_rect(px+6, 74, pw-12, 1, COL_DIM);

    /* "BEST" — 4 cells = 30 px, centered */
    {
        static const uint8_t w[] = { FB_, FE_, FS_, FT_ };
        draw_word(w, 4, (uint16_t)((LCD_WIDTH - 30) / 2), 78, COL_GOLD, COL_BAR);
    }
    draw_score_at(best, 91, COL_GOLD, COL_BAR);
}

static void draw_waiting_hint(void)
{
    /* Black pill with "JUMP" — 4 cells = 30 px, centered */
    static const uint8_t w[] = { FJ_, FU_, FM_, FP_ };
    display_fill_rect(34, 66, 60, 18, COL_BAR);
    draw_word(w, 4, (uint16_t)((LCD_WIDTH - 30) / 2), 70, COL_SCORE, COL_BAR);
}

/* ===================================================================
 * Collision — dino AABB vs each obstacle part, 2 px inset all around
 * =================================================================== */
static int hit_obstacle(int dino_y, const Obst *o)
{
    const ObstType *t = &g_obst_types[o->type];
    int dx1 = DINO_X + 2, dx2 = DINO_X + DINO_W - 3;
    int dy2 = dino_y + DINO_H - 3;

    for (int p = 0; p < t->nparts; p++) {
        const ObstPart *pt = &t->part[p];
        int ox1 = (int)o->x + (int)pt->dx + 1;
        int ox2 = ox1 + (int)pt->w - 3;
        int oy1 = GROUND_Y - (int)pt->h + 2;
        if (dx2 < ox1 || dx1 > ox2) continue;
        if (dy2 >= oy1) return 1;
    }
    return 0;
}

/* ===================================================================
 * Game state
 * =================================================================== */
#define ST_WAITING 0
#define ST_PLAYING 1
#define ST_DEAD    2

#define NV_KEY_DINO_BEST  NV_KEY_APP_0

static uint32_t g_best;
static uint8_t  g_state;
static int32_t  g_dino_fp;       /* dino top y, fixed point */
static int32_t  g_vel_fp;
static int      g_dino_y;
static int      g_prev_y;
static uint8_t  g_airborne;
static uint32_t g_score;
static uint32_t g_prev_score;
static uint32_t g_dead_hold;
static uint32_t g_tick_ctr;      /* physics ticks this run (score + anim) */
static uint32_t g_frame_ctr;
static uint16_t g_phys_t;
static uint8_t  g_new_best;

#define DINO_GROUND_TOP  (GROUND_Y - DINO_H)

static const uint16_t *run_frame(void)
{
    return ((g_tick_ctr >> 2) & 1u) ? spr_dino_run_a : spr_dino_run_b;
}

static void update_speed(void)
{
    uint8_t s = (uint8_t)(SPEED_BASE + g_score / 150u);
    g_speed = (s > SPEED_MAX) ? SPEED_MAX : s;
}

static void game_init_state(void)
{
    g_seed ^= (g_frame_ctr * 2654435761UL) ^ 0xD1D0CAFEUL;

    g_state      = ST_WAITING;
    g_dino_fp    = (int32_t)DINO_GROUND_TOP << FP_SHIFT;
    g_vel_fp     = 0;
    g_dino_y     = DINO_GROUND_TOP;
    g_prev_y     = DINO_GROUND_TOP;
    g_airborne   = 0;
    g_score      = 0;
    g_prev_score = 0;
    g_dead_hold  = 0;
    g_tick_ctr   = 0;
    g_new_best   = 0;
    g_speed      = SPEED_BASE;

    obst_reset(0, LCD_WIDTH + 30);
    obst_reset(1, (int)g_obst[0].x);

    scene_draw(g_dino_y, spr_dino_jump, 0);
    draw_waiting_hint();
    g_phys_t = ms_now();   /* reset AFTER render so no catch-up burst */
}

static void on_hard_reset(void)
{
    if (g_new_best) {
        nv_write(NV_KEY_DINO_BEST, g_best);
        g_new_best = 0;
    }
    game_init_state();
}

/* ===================================================================
 * Framework callbacks
 * =================================================================== */
void app_init(void)
{
    /* Long-pulse GC9107 reset clears any stuck panel state after hot-flash */
    display_recover();

    app_set_sleep_timeout(30000);
    app_set_hold_reset(10000, on_hard_reset);

    g_best      = nv_read(NV_KEY_DINO_BEST, 0);
    g_bat_raw   = bat_read_raw();
    g_frame_ctr = 0;

    display_fill(COL_BLACK);
    game_init_state();
}

void app_update(uint32_t frame)
{
    (void)frame;

    /* Battery refresh every ~5 s */
    g_frame_ctr++;
    if (g_frame_ctr % 150u == 0u) {
        g_bat_raw = bat_read_raw();
        draw_bat();
    }

    /* Sticky jump latch (see flappy.c for rationale): the press edge is
     * held in s_jump until a physics tick consumes it, so presses during
     * render-heavy frames or right after a state transition are not lost. */
    static uint8_t s_btn_prev = 0u;
    static uint8_t s_jump     = 0u;
    {
        uint8_t btn = button_raw();
        if (btn && !s_btn_prev) s_jump = 1u;
        s_btn_prev = btn;
    }
    uint8_t jump = s_jump;

    uint8_t ticks = 0u;
    while (ticks < 4u && (uint16_t)(ms_now() - g_phys_t) >= PHYS_MS) {
        ticks++;
        g_phys_t += PHYS_MS;

        /* Re-sample the button each tick */
        {
            uint8_t btn = button_raw();
            if (btn && !s_btn_prev) s_jump = 1u;
            s_btn_prev = btn;
            jump = s_jump;
        }

        /* ---- Waiting ---- */
        if (g_state == ST_WAITING) {
            if (jump || button_pressed()) {
                g_state = ST_PLAYING;
                scene_draw(g_dino_y, spr_dino_jump, 0);  /* wipe hint */
                g_phys_t = ms_now();
                jump = 0; s_jump = 0u;
            }
            continue;
        }

        /* ---- Dead ---- */
        if (g_state == ST_DEAD) {
            if (++g_dead_hold > 15u && jump) {
                if (g_new_best) {
                    nv_write(NV_KEY_DINO_BEST, g_best);
                    g_new_best = 0;
                }
                s_jump = 0u;
                game_init_state();
                return;   /* g_phys_t reset; skip remaining ticks */
            }
            continue;
        }

        /* ---- Playing ---- */
        g_tick_ctr++;

        /* Jump input: only from the ground */
        if (jump) {
            if (!g_airborne) {
                g_airborne = 1;
                g_vel_fp   = JUMP_FP;
            }
            jump = 0; s_jump = 0u;
        }

        /* Vertical physics */
        if (g_airborne) {
            g_vel_fp += GRAVITY_FP;
            if (g_vel_fp > MAX_FALL_FP) g_vel_fp = MAX_FALL_FP;
            g_dino_fp += g_vel_fp;
            if (g_dino_fp >= ((int32_t)DINO_GROUND_TOP << FP_SHIFT)) {
                g_dino_fp  = (int32_t)DINO_GROUND_TOP << FP_SHIFT;
                g_vel_fp   = 0;
                g_airborne = 0;
            }
        }
        int ny = (int)(g_dino_fp >> FP_SHIFT);

        /* Scroll obstacles (draws them), respawn off-screen ones */
        for (int i = 0; i < OBST_COUNT; i++) {
            if (obst_scroll(&g_obst[i])) {
                int other = (int)g_obst[i ^ 1].x + g_obst_types[g_obst[i ^ 1].type].w;
                obst_reset(i, other > LCD_WIDTH ? other : LCD_WIDTH);
            }
        }

        /* Dino render: erase old rect if it moved, opaque blit at new y */
        if (g_prev_y != ny) dino_erase(g_prev_y);
        dino_draw(ny, g_airborne ? spr_dino_jump : run_frame());
        g_prev_y = ny;
        g_dino_y = ny;

        /* Score: +1 every 3rd tick (~10/s), speed ramps with score */
        if (g_tick_ctr % 3u == 0u && g_score < SCORE_CAP) {
            g_score++;
            update_speed();
        }
        if (g_score != g_prev_score && g_score % 5u == 0u) {
            draw_score(g_score);
            g_prev_score = g_score;
        }

        /* Collision */
        for (int i = 0; i < OBST_COUNT; i++) {
            if (hit_obstacle(ny, &g_obst[i])) { g_state = ST_DEAD; break; }
        }

        if (g_state == ST_DEAD) {
            if (g_score > g_best) {
                g_best     = g_score;
                g_new_best = 1;
            }
            IWDG_FEED();
            draw_score(g_score);
            draw_game_over(g_score, g_best);
            g_phys_t = ms_now();
            return;   /* g_phys_t reset; skip remaining ticks this frame */
        }
    }
}

void app_wake(void)
{
    scene_draw(g_dino_y, spr_dino_jump, g_score);
    if (g_state == ST_DEAD)
        draw_game_over(g_score, g_best);
    else if (g_state == ST_WAITING)
        draw_waiting_hint();
    g_phys_t = ms_now();
}
