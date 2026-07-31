/**
 * @file ui_avatar.c
 * @brief BMO-style monochrome face page for the desktop companion.
 *
 * Rendering rules for the 1-bit reflective LCD:
 * - No transform rotations (they produce gray AA artifacts and mis-positioning).
 * - Shapes only: rounded rects, circles, arcs (lv_arc), lines (lv_line).
 * - BMO reads as small dot eyes plus a big, chunky mouth: the mouth carries the
 *   expression, the eyes only modulate it.
 *
 * The signature open mouth is a "D" on its side (flat top, semicircular bottom).
 * It is drawn as an lv_arc whose arc width equals its radius, which the software
 * renderer clamps into a filled pie, plus a small bar for the straight sides.
 */

#include "tal_api.h"
#include <string.h>
#include <ctype.h>
#include "lvgl.h"
#include "lv_vendor.h"
#include "ai_ui_icon_font.h"
#include "lang_config.h"
#include "ui_avatar.h"
#include "motion_engine.h"

/* Set to 1 to cycle all expressions on boot (no network needed). */
#define EXPR_SELF_TEST 0

#define STATUS_BAR_H 28
#define CAPTION_H    30

#define PLATE_X 6
#define PLATE_Y 30
#define PLATE_W 388
#define PLATE_H 236

/* Feature coordinates are relative to the face container, which is anchored at
 * PLATE_Y so that bobbing only ever invalidates the plate area. */
#define EYE_CY   84
#define EYE_L_CX 132
#define EYE_R_CX 268
#define MOUTH_CX 200
#define MOUTH_CY 156

#define EYE_DOT_D 30
#define HEART_W   52
#define HEART_H   46

#define BLUSH_STROKES 3
#define BLUSH_L_X     (EYE_L_CX - 74)
#define BLUSH_R_X     (EYE_R_CX + 42)
#define BLUSH_Y       (EYE_CY + 40)

/* Idle behaviour: blink and drift for a while, then nod off. */
#define TICK_MS         150
#define IDLE_SLEEP_MS   60000
#define BLINK_HOLD_MS   120
#define BLINK_MIN_TICKS 20 /* 3.0 s */
#define BLINK_VAR_TICKS 27 /* up to +4.0 s */
#define DART_MIN_TICKS  47 /* 7.0 s */
#define DART_VAR_TICKS  33
#define DART_HOLD_TICKS 5
#define DART_SHIFT      9
#define BOB_STEPS       24

#define ZZZ_COUNT      3
#define ZZZ_TICK_MS    50
#define ZZZ_PERIOD_MS  1800
#define ZZZ_STAGGER_MS 600
#define ZZZ_SIZE_MIN   8
#define ZZZ_SIZE_MAX   22
#define ZZZ_START_X    306
#define ZZZ_START_Y    76
#define ZZZ_END_X      352
#define ZZZ_END_Y      20

/* ------------------------------------------------------------------ */
/* Expression spec                                                      */
/* ------------------------------------------------------------------ */

typedef enum {
    EYE_DOT = 0,   /* BMO round dot */
    EYE_BAR,       /* flat closed line */
    EYE_ARC_UP,    /* happy closed, curves up */
    EYE_ARC_DOWN,  /* drowsy or sad closed, curves down */
    EYE_SQUINT,    /* scrunched >< */
    EYE_HEART,
} EYE_SHAPE_E;

typedef enum {
    MOUTH_LINE = 0, /* thin bar */
    MOUTH_SMILE,    /* thin arc, curves up */
    MOUTH_FROWN,    /* thin arc, curves down */
    MOUTH_O,        /* stadium or circle */
    MOUTH_D,        /* BMO open mouth: flat top, round bottom */
    MOUTH_ZIG,      /* gritted grimace */
} MOUTH_SHAPE_E;

typedef enum {
    BROW_NONE = 0,
    BROW_ANGRY, /* \  /  inner ends down */
    BROW_SAD,   /* /  \  inner ends up   */
} BROW_E;

typedef struct {
    uint8_t shape; /* EYE_SHAPE_E */
    int16_t w, h;
    int16_t dx, dy;
} EYE_SPEC_T;

typedef struct {
    uint8_t shape; /* MOUTH_SHAPE_E */
    int16_t w;     /* overall width; arcs use it as circle diameter */
    int16_t h;     /* MOUTH_D: straight side height. others: stroke or height */
    int16_t dx, dy;
} MOUTH_SPEC_T;

typedef struct {
    const char  *names[6];
    EYE_SPEC_T   eye_l;
    EYE_SPEC_T   eye_r;
    MOUTH_SPEC_T mouth;
    uint8_t      brow; /* BROW_E */
    bool         blush;
    bool         zzz;
    uint8_t      motion_id;
} EXPR_DEF_T;

static const EXPR_DEF_T sg_expr_table[] = {
    { /* BMO resting face: two dots and a thick flat mouth */
        .names = {"NEUTRAL", "RELAXED", "COOL", NULL},
        .eye_l = {EYE_DOT, EYE_DOT_D, EYE_DOT_D, 0, 0},
        .eye_r = {EYE_DOT, EYE_DOT_D, EYE_DOT_D, 0, 0},
        .mouth = {MOUTH_LINE, 100, 14, 0, 0},
        .motion_id = MOTION_NEUTRAL,
    },
    {
        .names = {"HAPPY", "FUNNY", "SILLY", "CONFIDENT", NULL},
        .eye_l = {EYE_ARC_UP, 52, 9, 0, 0},
        .eye_r = {EYE_ARC_UP, 52, 9, 0, 0},
        .mouth = {MOUTH_D, 112, 10, 0, 0},
        .blush = true,
        .motion_id = MOTION_CHEER_BOTH,
    },
    {
        .names = {"LAUGHING", "DELICIOUS", NULL},
        .eye_l = {EYE_ARC_UP, 56, 9, 0, -4},
        .eye_r = {EYE_ARC_UP, 56, 9, 0, -4},
        .mouth = {MOUTH_D, 126, 18, 0, 6},
        .blush = true,
        .motion_id = MOTION_CHEER_BOTH,
    },
    {
        .names = {"SAD", "DISAPPOINTED", NULL},
        .eye_l = {EYE_ARC_DOWN, 50, 9, 0, 8},
        .eye_r = {EYE_ARC_DOWN, 50, 9, 0, 8},
        .mouth = {MOUTH_FROWN, 76, 9, 0, 14},
        .brow = BROW_SAD,
        .motion_id = MOTION_DROOP_SAD,
    },
    {
        .names = {"ANGRY", "ANNOYED", NULL},
        .eye_l = {EYE_SQUINT, 34, 30, 0, 0},
        .eye_r = {EYE_SQUINT, 34, 30, 0, 0},
        .mouth = {MOUTH_ZIG, 104, 18, 0, 8},
        .motion_id = MOTION_NEUTRAL,
    },
    {
        .names = {"SURPRISE", "SURPRISED", "SHOCKED", "FEARFUL", "WAKEUP", NULL},
        .eye_l = {EYE_DOT, 42, 42, 0, -6},
        .eye_r = {EYE_DOT, 42, 42, 0, -6},
        .mouth = {MOUTH_O, 46, 56, 0, 10},
        .motion_id = MOTION_WAVE_RIGHT,
    },
    {
        .names = {"LOVING", "KISSY", "TOUCH", NULL},
        .eye_l = {EYE_HEART, HEART_W, HEART_H, 0, 0},
        .eye_r = {EYE_HEART, HEART_W, HEART_H, 0, 0},
        .mouth = {MOUTH_D, 88, 8, 0, 4},
        .blush = true,
        .motion_id = MOTION_CHEER_BOTH,
    },
    {
        .names = {"EMBARRASSED", NULL},
        .eye_l = {EYE_ARC_UP, 46, 8, 0, 0},
        .eye_r = {EYE_ARC_UP, 46, 8, 0, 0},
        .mouth = {MOUTH_ZIG, 56, 12, 0, 8},
        .blush = true,
        .motion_id = MOTION_NEUTRAL,
    },
    {
        .names = {"THINKING", "CONFUSED", NULL},
        .eye_l = {EYE_DOT, EYE_DOT_D, EYE_DOT_D, -8, 6},
        .eye_r = {EYE_DOT, EYE_DOT_D, EYE_DOT_D, 8, -10},
        .mouth = {MOUTH_LINE, 60, 12, 16, 0},
        .motion_id = MOTION_THINK_POSE,
    },
    {
        .names = {"WINK", NULL},
        .eye_l = {EYE_DOT, EYE_DOT_D, EYE_DOT_D, 0, 0},
        .eye_r = {EYE_ARC_UP, 50, 9, 0, 0},
        .mouth = {MOUTH_D, 94, 8, 0, 0},
        .blush = true,
        .motion_id = MOTION_WAVE_RIGHT,
    },
    {
        .names = {"SLEEP", "SLEEPY", NULL},
        .eye_l = {EYE_ARC_DOWN, 52, 9, 0, 6},
        .eye_r = {EYE_ARC_DOWN, 52, 9, 0, 6},
        .mouth = {MOUTH_O, 34, 28, 0, 12},
        .zzz = true,
        .motion_id = MOTION_NEUTRAL,
    },
    {
        .names = {"LEFT", "LOOK_LEFT", NULL},
        .eye_l = {EYE_DOT, EYE_DOT_D, EYE_DOT_D, -20, 0},
        .eye_r = {EYE_DOT, EYE_DOT_D, EYE_DOT_D, -20, 0},
        .mouth = {MOUTH_LINE, 72, 12, -16, 0},
        .motion_id = MOTION_WAVE_LEFT,
    },
    {
        .names = {"RIGHT", "LOOK_RIGHT", NULL},
        .eye_l = {EYE_DOT, EYE_DOT_D, EYE_DOT_D, 20, 0},
        .eye_r = {EYE_DOT, EYE_DOT_D, EYE_DOT_D, 20, 0},
        .mouth = {MOUTH_LINE, 72, 12, 16, 0},
        .motion_id = MOTION_WAVE_RIGHT,
    },
};

static const int sg_expr_cnt = (int)(sizeof(sg_expr_table) / sizeof(sg_expr_table[0]));

/* Talking cycles through open shapes so the mouth looks like it forms syllables. */
static const MOUTH_SPEC_T sg_speak_mouth[] = {
    {MOUTH_D, 96, 10, 0, 0},
    {MOUTH_O, 40, 36, 0, 6},
    {MOUTH_D, 118, 18, 0, 4},
    {MOUTH_LINE, 72, 13, 0, 0},
};
static const int sg_speak_cnt = (int)(sizeof(sg_speak_mouth) / sizeof(sg_speak_mouth[0]));

/* Vertical bob, one entry per tick: 3 px peak over 24 steps (3.6 s). */
static const int8_t sg_bob[BOB_STEPS] = {0,  1,  2,  2,  3,  3,  3,  3,  3,  2,  2,  1,
                                         0,  -1, -2, -2, -3, -3, -3, -3, -3, -2, -2, -1};

/* ------------------------------------------------------------------ */
/* UI state                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    lv_obj_t          *line[3]; /* top / diagonal / bottom of letter Z */
    lv_point_precise_t pts[3][2];
} ZZZ_GLYPH_T;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *plate;
    lv_obj_t *face; /* holds every facial feature so it can bob as one */
    lv_obj_t *status_label;
    lv_obj_t *network_label;
    lv_obj_t *caption_label;

    lv_obj_t *eye_dot_l, *eye_dot_r;
    lv_obj_t *eye_arc_l, *eye_arc_r;
    lv_obj_t *eye_heart_l, *eye_heart_r;
    lv_obj_t *eye_sq_l[2], *eye_sq_r[2];
    lv_obj_t *brow_l, *brow_r;
    lv_obj_t *blush_l[BLUSH_STROKES], *blush_r[BLUSH_STROKES];

    lv_obj_t *mouth_rect; /* flat bar / stadium / O */
    lv_obj_t *mouth_arc;  /* thin smile or frown */
    lv_obj_t *mouth_pie;  /* filled lower half of the D mouth */
    lv_obj_t *mouth_bar;  /* straight sides above the pie */
    lv_obj_t *mouth_zig;

    lv_obj_t   *think_dots[3];
    ZZZ_GLYPH_T zzz[ZZZ_COUNT];
    bool        zzz_active;
    uint16_t    zzz_tick;

    lv_timer_t *tick_timer;
    lv_timer_t *speak_timer;
    lv_timer_t *think_timer;
    lv_timer_t *zzz_timer;
#if EXPR_SELF_TEST
    lv_timer_t *self_test_timer;
#endif
    AVATAR_STATE_E state;
    char           emotion[16];
    uint8_t        speak_idx;
    uint8_t        think_idx;

    bool     blinking;
    bool     asleep;
    int16_t  dart_dx;
    uint16_t blink_at;
    uint16_t dart_at;
    uint16_t dart_end;
    uint16_t tick;
    uint8_t  bob_idx;
} AVATAR_UI_T;

static AVATAR_UI_T sg_avatar;
static lv_font_t  *sg_text_font = NULL;
static lv_font_t  *sg_icon_font = NULL;

static char           sg_cached_emotion[16] = "NEUTRAL";
static AVATAR_STATE_E sg_cached_state       = AVATAR_IDLE;
static char           sg_cached_caption[128];
static char           sg_cached_status[32];
static uint32_t       sg_hold_until_ms = 0;
static uint32_t       sg_idle_since_ms = 0;

static uint32_t sg_rand_state = 0x1234abcdu;

static uint32_t __rnd(uint32_t range)
{
    sg_rand_state = sg_rand_state * 1103515245u + 12345u;
    return range ? ((sg_rand_state >> 16) % range) : 0;
}

/* Line point buffers stay resident: lv_line keeps the pointer, not a copy. */
static lv_point_precise_t sg_brow_angry_l[2] = {{0, 0}, {48, 20}};
static lv_point_precise_t sg_brow_angry_r[2] = {{0, 20}, {48, 0}};
static lv_point_precise_t sg_brow_sad_l[2]   = {{0, 20}, {48, 0}};
static lv_point_precise_t sg_brow_sad_r[2]   = {{0, 0}, {48, 20}};
/* One shared slanted stroke, repeated three times per cheek as blush hatching. */
static lv_point_precise_t sg_blush_pts[2] = {{0, 15}, {11, 0}};
static lv_point_precise_t sg_sq_l[2][2];
static lv_point_precise_t sg_sq_r[2][2];
static lv_point_precise_t sg_zig_pts[7];

/* Heart eye bitmap: computed once from the classic heart curve. */
static uint8_t        sg_heart_buf[8 + ((HEART_W + 7) / 8) * HEART_H];
static lv_image_dsc_t sg_heart_dsc;
static bool           sg_heart_ready = false;

static void __build_heart_image(void)
{
    int      stride  = (HEART_W + 7) / 8;
    uint8_t *palette = sg_heart_buf;
    uint8_t *px      = sg_heart_buf + 8;

    if (sg_heart_ready) {
        return;
    }

    /* palette entry = B,G,R,A; index0 = white, index1 = black */
    palette[0] = 0xFF; palette[1] = 0xFF; palette[2] = 0xFF; palette[3] = 0xFF;
    palette[4] = 0x00; palette[5] = 0x00; palette[6] = 0x00; palette[7] = 0xFF;

    memset(px, 0, (size_t)stride * HEART_H);
    for (int j = 0; j < HEART_H; j++) {
        float y = 1.25f - 2.60f * ((float)j / (float)(HEART_H - 1));
        for (int i = 0; i < HEART_W; i++) {
            float x = -1.55f + 3.10f * ((float)i / (float)(HEART_W - 1));
            float a = x * x + y * y - 1.0f;
            /* (x^2 + y^2 - 1)^3 - x^2*y^3 <= 0 --> inside heart */
            if (a * a * a - x * x * y * y * y <= 0.0f) {
                px[j * stride + (i >> 3)] |= (uint8_t)(0x80u >> (i & 7));
            }
        }
    }

    memset(&sg_heart_dsc, 0, sizeof(sg_heart_dsc));
    sg_heart_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    sg_heart_dsc.header.cf     = LV_COLOR_FORMAT_I1;
    sg_heart_dsc.header.w      = HEART_W;
    sg_heart_dsc.header.h      = HEART_H;
    sg_heart_dsc.header.stride = (uint16_t)stride;
    sg_heart_dsc.data          = sg_heart_buf;
    sg_heart_dsc.data_size     = sizeof(sg_heart_buf);
    sg_heart_ready             = true;
}

/* ------------------------------------------------------------------ */
/* Expression lookup                                                    */
/* ------------------------------------------------------------------ */

static int __strcasecmp_local(const char *a, const char *b)
{
    if (!a || !b) {
        return (a == b) ? 0 : 1;
    }
    while (*a && *b) {
        int ca = (int)tolower((unsigned char)*a);
        int cb = (int)tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    return (int)tolower((unsigned char)*a) - (int)tolower((unsigned char)*b);
}

static const EXPR_DEF_T *__match_expr(const char *name)
{
    if (!name || !name[0]) {
        return NULL;
    }
    for (int i = 0; i < sg_expr_cnt; i++) {
        for (int j = 0; j < 6 && sg_expr_table[i].names[j]; j++) {
            if (0 == __strcasecmp_local(name, sg_expr_table[i].names[j])) {
                return &sg_expr_table[i];
            }
        }
    }
    return NULL;
}

static const EXPR_DEF_T *__find_expr(const char *name)
{
    const EXPR_DEF_T *expr = __match_expr(name);
    return expr ? expr : &sg_expr_table[0];
}

/* ------------------------------------------------------------------ */
/* Shape helpers                                                        */
/* ------------------------------------------------------------------ */

/* Toggling the hidden flag invalidates the object, so re-applying an unchanged
 * layout would repaint the whole face for nothing. Only act on real changes. */
static void __hide(lv_obj_t *obj)
{
    if (obj && !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void __show(lv_obj_t *obj)
{
    if (obj && lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void __apply_eye(const EYE_SPEC_T *spec, int base_cx, bool left)
{
    lv_obj_t  *dot   = left ? sg_avatar.eye_dot_l : sg_avatar.eye_dot_r;
    lv_obj_t  *arc   = left ? sg_avatar.eye_arc_l : sg_avatar.eye_arc_r;
    lv_obj_t  *heart = left ? sg_avatar.eye_heart_l : sg_avatar.eye_heart_r;
    lv_obj_t **sq    = left ? sg_avatar.eye_sq_l : sg_avatar.eye_sq_r;
    lv_point_precise_t (*pts)[2] = left ? sg_sq_l : sg_sq_r;
    int cx = base_cx + spec->dx;
    int cy = EYE_CY + spec->dy;

    __hide(dot);
    __hide(arc);
    __hide(heart);
    __hide(sq[0]);
    __hide(sq[1]);

    /* A blink collapses any open eye into a short lid stroke. */
    if (sg_avatar.blinking && (spec->shape == EYE_DOT || spec->shape == EYE_HEART)) {
        int w = spec->w + 6;
        __show(dot);
        lv_obj_set_size(dot, w, 7);
        lv_obj_set_style_radius(dot, 2, 0);
        lv_obj_set_pos(dot, cx - w / 2, cy - 3);
        return;
    }

    switch (spec->shape) {
    case EYE_ARC_UP:
    case EYE_ARC_DOWN:
        __show(arc);
        lv_obj_set_size(arc, spec->w, spec->w);
        lv_obj_set_style_arc_width(arc, spec->h, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
        if (spec->shape == EYE_ARC_UP) {
            lv_arc_set_bg_angles(arc, 195, 345);
            lv_obj_set_pos(arc, cx - spec->w / 2, cy - spec->w / 2 + 8);
        } else {
            lv_arc_set_bg_angles(arc, 15, 165);
            lv_obj_set_pos(arc, cx - spec->w / 2, cy - spec->w / 2 - 8);
        }
        break;

    case EYE_HEART:
        __show(heart);
        lv_obj_set_pos(heart, cx - HEART_W / 2, cy - HEART_H / 2);
        break;

    case EYE_SQUINT: {
        int w = spec->w, h = spec->h;
        /* Left eye points right, right eye points left, so they scrunch inward. */
        int tip = left ? w : 0;
        int out = left ? 0 : w;
        pts[0][0].x = out;      pts[0][0].y = 0;
        pts[0][1].x = tip;      pts[0][1].y = h / 2;
        pts[1][0].x = tip;      pts[1][0].y = h / 2;
        pts[1][1].x = out;      pts[1][1].y = h;
        for (int i = 0; i < 2; i++) {
            lv_line_set_points(sq[i], pts[i], 2);
            __show(sq[i]);
            lv_obj_set_pos(sq[i], cx - w / 2, cy - h / 2);
        }
        break;
    }

    case EYE_BAR:
        __show(dot);
        lv_obj_set_size(dot, spec->w, spec->h);
        lv_obj_set_style_radius(dot, spec->h / 2, 0);
        lv_obj_set_pos(dot, cx - spec->w / 2, cy - spec->h / 2);
        break;

    case EYE_DOT:
    default:
        __show(dot);
        lv_obj_set_size(dot, spec->w, spec->h);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(dot, cx - spec->w / 2, cy - spec->h / 2);
        break;
    }
}

static void __apply_mouth(const MOUTH_SPEC_T *spec)
{
    int cx = MOUTH_CX + spec->dx;
    int cy = MOUTH_CY + spec->dy;

    __hide(sg_avatar.mouth_rect);
    __hide(sg_avatar.mouth_arc);
    __hide(sg_avatar.mouth_pie);
    __hide(sg_avatar.mouth_bar);
    __hide(sg_avatar.mouth_zig);

    switch (spec->shape) {
    case MOUTH_D: {
        int w   = spec->w;
        int str = spec->h;             /* height of the straight sides */
        int top = cy - (str + w / 2) / 2;

        /* Arc width == radius makes the renderer fill the pie solid. */
        __show(sg_avatar.mouth_pie);
        lv_obj_set_size(sg_avatar.mouth_pie, w, w);
        lv_obj_set_style_arc_width(sg_avatar.mouth_pie, w / 2, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(sg_avatar.mouth_pie, false, LV_PART_MAIN);
        lv_arc_set_bg_angles(sg_avatar.mouth_pie, 0, 180);
        lv_obj_set_pos(sg_avatar.mouth_pie, cx - w / 2, top + str - w / 2);

        if (str > 0) {
            __show(sg_avatar.mouth_bar);
            lv_obj_set_size(sg_avatar.mouth_bar, w, str + 2);
            lv_obj_set_style_radius(sg_avatar.mouth_bar, 2, 0);
            lv_obj_set_pos(sg_avatar.mouth_bar, cx - w / 2, top);
        }
        break;
    }

    case MOUTH_ZIG: {
        int w = spec->w, a = spec->h;
        for (int i = 0; i < 7; i++) {
            sg_zig_pts[i].x = w * i / 6;
            sg_zig_pts[i].y = (i % 2) ? 0 : a;
        }
        __show(sg_avatar.mouth_zig);
        lv_line_set_points(sg_avatar.mouth_zig, sg_zig_pts, 7);
        lv_obj_set_pos(sg_avatar.mouth_zig, cx - w / 2, cy - a / 2);
        break;
    }

    case MOUTH_SMILE:
        __show(sg_avatar.mouth_arc);
        lv_obj_set_size(sg_avatar.mouth_arc, spec->w, spec->w);
        lv_obj_set_style_arc_width(sg_avatar.mouth_arc, spec->h, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(sg_avatar.mouth_arc, true, LV_PART_MAIN);
        lv_arc_set_bg_angles(sg_avatar.mouth_arc, 25, 155);
        lv_obj_set_pos(sg_avatar.mouth_arc, cx - spec->w / 2, cy - spec->w + 20);
        break;

    case MOUTH_FROWN:
        __show(sg_avatar.mouth_arc);
        lv_obj_set_size(sg_avatar.mouth_arc, spec->w, spec->w);
        lv_obj_set_style_arc_width(sg_avatar.mouth_arc, spec->h, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(sg_avatar.mouth_arc, true, LV_PART_MAIN);
        lv_arc_set_bg_angles(sg_avatar.mouth_arc, 205, 335);
        lv_obj_set_pos(sg_avatar.mouth_arc, cx - spec->w / 2, cy - 20);
        break;

    case MOUTH_O:
        __show(sg_avatar.mouth_rect);
        lv_obj_set_size(sg_avatar.mouth_rect, spec->w, spec->h);
        lv_obj_set_style_radius(sg_avatar.mouth_rect, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(sg_avatar.mouth_rect, cx - spec->w / 2, cy - spec->h / 2);
        break;

    case MOUTH_LINE:
    default:
        __show(sg_avatar.mouth_rect);
        lv_obj_set_size(sg_avatar.mouth_rect, spec->w, spec->h);
        lv_obj_set_style_radius(sg_avatar.mouth_rect, spec->h / 2, 0);
        lv_obj_set_pos(sg_avatar.mouth_rect, cx - spec->w / 2, cy - spec->h / 2);
        break;
    }
}

static void __apply_brows(const EXPR_DEF_T *expr)
{
    int y_l = EYE_CY + expr->eye_l.dy - expr->eye_l.h / 2 - 26;
    int y_r = EYE_CY + expr->eye_r.dy - expr->eye_r.h / 2 - 26;

    __hide(sg_avatar.brow_l);
    __hide(sg_avatar.brow_r);

    if (expr->brow == BROW_ANGRY) {
        lv_line_set_points(sg_avatar.brow_l, sg_brow_angry_l, 2);
        lv_line_set_points(sg_avatar.brow_r, sg_brow_angry_r, 2);
    } else if (expr->brow == BROW_SAD) {
        lv_line_set_points(sg_avatar.brow_l, sg_brow_sad_l, 2);
        lv_line_set_points(sg_avatar.brow_r, sg_brow_sad_r, 2);
    } else {
        return;
    }

    __show(sg_avatar.brow_l);
    __show(sg_avatar.brow_r);
    lv_obj_set_pos(sg_avatar.brow_l, EYE_L_CX + expr->eye_l.dx - 24, y_l);
    lv_obj_set_pos(sg_avatar.brow_r, EYE_R_CX + expr->eye_r.dx - 24, y_r);
}

static void __apply_blush(bool on)
{
    for (int i = 0; i < BLUSH_STROKES; i++) {
        if (!on) {
            __hide(sg_avatar.blush_l[i]);
            __hide(sg_avatar.blush_r[i]);
            continue;
        }
        __show(sg_avatar.blush_l[i]);
        __show(sg_avatar.blush_r[i]);
        lv_obj_set_pos(sg_avatar.blush_l[i], BLUSH_L_X + i * 11, BLUSH_Y);
        lv_obj_set_pos(sg_avatar.blush_r[i], BLUSH_R_X + i * 11, BLUSH_Y);
    }
}

/* ------------------------------------------------------------------ */
/* Face layout                                                          */
/* ------------------------------------------------------------------ */

static void __zzz_set_active(bool on);

static bool __idle_dozed_off(void)
{
    return (sg_avatar.state == AVATAR_IDLE && sg_idle_since_ms != 0 &&
            (tal_system_get_millisecond() - sg_idle_since_ms) > IDLE_SLEEP_MS);
}

static void __apply_face_layout(void)
{
    EXPR_DEF_T eff;

    if (!sg_avatar.root) {
        return;
    }

    eff = *__find_expr(sg_avatar.emotion);

    switch (sg_avatar.state) {
    case AVATAR_IDLE:
        /* Keep the current mood on screen; only drop to sleep after a while. */
        if (sg_avatar.asleep) {
            eff = *__find_expr("SLEEP");
        }
        break;
    case AVATAR_LISTEN: {
        EYE_SPEC_T wide = {EYE_DOT, 40, 40, 0, -4};
        eff.eye_l = wide;
        eff.eye_r = wide;
        eff.mouth = (MOUTH_SPEC_T){MOUTH_O, 36, 34, 0, 6};
        eff.brow  = BROW_NONE;
        eff.zzz   = false;
        break;
    }
    case AVATAR_THINK:
        eff.mouth = (MOUTH_SPEC_T){MOUTH_LINE, 60, 12, 16, 0};
        eff.zzz   = false;
        break;
    case AVATAR_SPEAK:
        eff.mouth = sg_speak_mouth[sg_avatar.speak_idx % sg_speak_cnt];
        eff.zzz   = false;
        break;
    default:
        break;
    }

    eff.eye_l.dx += sg_avatar.dart_dx;
    eff.eye_r.dx += sg_avatar.dart_dx;

    __apply_eye(&eff.eye_l, EYE_L_CX, true);
    __apply_eye(&eff.eye_r, EYE_R_CX, false);
    __apply_mouth(&eff.mouth);
    __apply_brows(&eff);
    __apply_blush(eff.blush);
    __zzz_set_active(eff.zzz);

    for (int i = 0; i < 3; i++) {
        if (sg_avatar.state == AVATAR_THINK) {
            __show(sg_avatar.think_dots[i]);
        } else {
            __hide(sg_avatar.think_dots[i]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Animations                                                           */
/* ------------------------------------------------------------------ */

static void __schedule_blink(void)
{
    sg_avatar.blink_at = (uint16_t)(sg_avatar.tick + BLINK_MIN_TICKS + __rnd(BLINK_VAR_TICKS));
}

static void __schedule_dart(void)
{
    sg_avatar.dart_at = (uint16_t)(sg_avatar.tick + DART_MIN_TICKS + __rnd(DART_VAR_TICKS));
}

/* Only faces that actually show open eyes have something to blink. */
static bool __eyes_are_open(void)
{
    const EXPR_DEF_T *expr;

    if (sg_avatar.asleep) {
        return false;
    }
    if (sg_avatar.state == AVATAR_LISTEN) {
        return true;
    }
    expr = __find_expr(sg_avatar.emotion);
    return (expr->eye_l.shape == EYE_DOT || expr->eye_l.shape == EYE_HEART ||
            expr->eye_r.shape == EYE_DOT || expr->eye_r.shape == EYE_HEART);
}

static void __blink_end_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!sg_avatar.root) {
        return;
    }
    sg_avatar.blinking = false;
    __apply_face_layout();
}

static void __tick_cb(lv_timer_t *timer)
{
    bool asleep;

    (void)timer;
    if (!sg_avatar.root) {
        return;
    }
    sg_avatar.tick++;

    asleep = __idle_dozed_off();
    if (asleep != sg_avatar.asleep) {
        sg_avatar.asleep = asleep;
        __apply_face_layout();
    }

    if (sg_avatar.tick == sg_avatar.blink_at) {
        /* A talking face is busy enough already. Always reschedule, otherwise a
         * skipped blink would stall blinking until the tick counter wraps. */
        if (sg_avatar.state != AVATAR_SPEAK && __eyes_are_open()) {
            lv_timer_t *end = lv_timer_create(__blink_end_cb, BLINK_HOLD_MS, NULL);
            lv_timer_set_repeat_count(end, 1);
            sg_avatar.blinking = true;
            __apply_face_layout();
        }
        __schedule_blink();
    }

    /* Glancing around only reads as idle curiosity, so it is skipped in every
     * other state. Rescheduling is unconditional for the same reason as blinks. */
    if (sg_avatar.tick == sg_avatar.dart_at) {
        if (sg_avatar.state == AVATAR_IDLE && !sg_avatar.asleep) {
            sg_avatar.dart_dx  = __rnd(2) ? DART_SHIFT : -DART_SHIFT;
            sg_avatar.dart_end = (uint16_t)(sg_avatar.tick + DART_HOLD_TICKS);
            __apply_face_layout();
        }
        __schedule_dart();
    } else if (sg_avatar.dart_dx != 0 &&
               (sg_avatar.tick == sg_avatar.dart_end || sg_avatar.state != AVATAR_IDLE)) {
        sg_avatar.dart_dx = 0;
        __apply_face_layout();
    }

    /* Breathing bob. Held still while asleep so the panel can settle, and only
     * moved on a real change so the reflective panel is not redrawn for nothing. */
    if (!sg_avatar.asleep && sg_avatar.face) {
        uint8_t next = (uint8_t)((sg_avatar.bob_idx + 1) % BOB_STEPS);
        if (sg_bob[next] != sg_bob[sg_avatar.bob_idx]) {
            lv_obj_set_y(sg_avatar.face, PLATE_Y + sg_bob[next]);
        }
        sg_avatar.bob_idx = next;
    }
}

static void __speak_cb(lv_timer_t *timer)
{
    (void)timer;
    if (sg_avatar.state != AVATAR_SPEAK || !sg_avatar.root) {
        return;
    }
    sg_avatar.speak_idx = (uint8_t)((sg_avatar.speak_idx + 1) % sg_speak_cnt);
    __apply_face_layout();
}

static void __think_cb(lv_timer_t *timer)
{
    (void)timer;
    if (sg_avatar.state != AVATAR_THINK || !sg_avatar.root) {
        return;
    }
    for (int i = 0; i < 3; i++) {
        if (i == (int)sg_avatar.think_idx) {
            __show(sg_avatar.think_dots[i]);
        } else {
            __hide(sg_avatar.think_dots[i]);
        }
    }
    sg_avatar.think_idx = (uint8_t)((sg_avatar.think_idx + 1) % 3);
}

#if EXPR_SELF_TEST
static void __self_test_cb(lv_timer_t *timer)
{
    static int idx = 0;
    (void)timer;
    const EXPR_DEF_T *expr = &sg_expr_table[idx % sg_expr_cnt];
    if (expr->names[0]) {
        avatar_express(expr->names[0]);
        avatar_set_caption(expr->names[0]);
        PR_NOTICE("[expr self-test] %s", expr->names[0]);
    }
    idx++;
}
#endif

/* ------------------------------------------------------------------ */
/* Widget factories                                                     */
/* ------------------------------------------------------------------ */

static lv_obj_t *__make_solid(lv_obj_t *parent, lv_color_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *__make_arc(lv_obj_t *parent)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc, 0, 0);
    lv_obj_set_style_pad_all(arc, 0, 0);
    lv_obj_set_style_arc_color(arc, lv_color_black(), LV_PART_MAIN);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
    return arc;
}

static lv_obj_t *__make_stroke(lv_obj_t *parent, int width)
{
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_color(line, lv_color_black(), 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_add_flag(line, LV_OBJ_FLAG_HIDDEN);
    return line;
}

static void __zzz_set_glyph(ZZZ_GLYPH_T *g, int size, int line_w)
{
    g->pts[0][0].x = 0;    g->pts[0][0].y = 0;
    g->pts[0][1].x = size; g->pts[0][1].y = 0;

    g->pts[1][0].x = size; g->pts[1][0].y = 0;
    g->pts[1][1].x = 0;    g->pts[1][1].y = size;

    g->pts[2][0].x = 0;    g->pts[2][0].y = size;
    g->pts[2][1].x = size; g->pts[2][1].y = size;

    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_line_width(g->line[i], line_w, 0);
        lv_line_set_points(g->line[i], g->pts[i], 2);
    }
}

static void __zzz_hide_all(void)
{
    for (int z = 0; z < ZZZ_COUNT; z++) {
        for (int i = 0; i < 3; i++) {
            __hide(sg_avatar.zzz[z].line[i]);
        }
    }
    sg_avatar.zzz_active = false;
}

static void __zzz_place_glyph(ZZZ_GLYPH_T *g, int x, int y, int size)
{
    int line_w = (size < 16) ? 3 : ((size < 22) ? 4 : 5);

    __zzz_set_glyph(g, size, line_w);
    for (int i = 0; i < 3; i++) {
        __show(g->line[i]);
        lv_obj_set_pos(g->line[i], x, y);
    }
}

static void __zzz_anim_cb(lv_timer_t *timer)
{
    int loop;

    (void)timer;
    if (!sg_avatar.root || !sg_avatar.zzz_active) {
        return;
    }

    loop = ZZZ_PERIOD_MS + ZZZ_STAGGER_MS * (ZZZ_COUNT - 1);
    sg_avatar.zzz_tick = (uint16_t)((sg_avatar.zzz_tick + ZZZ_TICK_MS) % loop);

    for (int z = 0; z < ZZZ_COUNT; z++) {
        int age = (int)sg_avatar.zzz_tick - z * ZZZ_STAGGER_MS;
        if (age < 0) {
            age += loop;
        }
        if (age >= ZZZ_PERIOD_MS) {
            for (int i = 0; i < 3; i++) {
                __hide(sg_avatar.zzz[z].line[i]);
            }
            continue;
        }

        /* Ease-out so growth feels softer near the top */
        float t    = (float)age / (float)ZZZ_PERIOD_MS;
        float te   = t * (2.0f - t);
        int   x    = ZZZ_START_X + (int)((ZZZ_END_X - ZZZ_START_X) * te);
        int   y    = ZZZ_START_Y + (int)((ZZZ_END_Y - ZZZ_START_Y) * te);
        int   size = ZZZ_SIZE_MIN + (int)((ZZZ_SIZE_MAX - ZZZ_SIZE_MIN) * te);
        __zzz_place_glyph(&sg_avatar.zzz[z], x, y, size);
    }
}

static void __zzz_set_active(bool on)
{
    if (on) {
        if (!sg_avatar.zzz_active) {
            sg_avatar.zzz_tick   = 0;
            sg_avatar.zzz_active = true;
        }
    } else {
        __zzz_hide_all();
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

bool avatar_emotion_hold_active(void)
{
    return (tal_system_get_millisecond() < sg_hold_until_ms);
}

bool avatar_express(const char *name)
{
    const EXPR_DEF_T *expr = __match_expr(name);

    if (!expr) {
        return false;
    }

    strncpy(sg_cached_emotion, expr->names[0], sizeof(sg_cached_emotion) - 1);
    sg_cached_emotion[sizeof(sg_cached_emotion) - 1] = '\0';

    if (sg_avatar.root) {
        strncpy(sg_avatar.emotion, sg_cached_emotion, sizeof(sg_avatar.emotion) - 1);
        sg_avatar.emotion[sizeof(sg_avatar.emotion) - 1] = '\0';
        /* An explicit expression means something happened, so wake up. */
        sg_avatar.asleep = false;
        sg_idle_since_ms = tal_system_get_millisecond();
        __apply_face_layout();
    }

    motion_on_expression(expr->motion_id);
    return true;
}

void avatar_express_hold(const char *name, uint32_t hold_ms)
{
    if (avatar_express(name)) {
        sg_hold_until_ms = tal_system_get_millisecond() + hold_ms;
    }
}

void avatar_page_create(lv_obj_t *parent)
{
    memset(&sg_avatar, 0, sizeof(sg_avatar));
    strncpy(sg_avatar.emotion, sg_cached_emotion, sizeof(sg_avatar.emotion) - 1);
    sg_avatar.state = sg_cached_state;

    sg_text_font  = ai_ui_get_text_font();
    sg_icon_font  = ai_ui_get_icon_font();
    sg_rand_state = tal_system_get_millisecond() | 1u;

    __build_heart_image();

    sg_avatar.root = lv_obj_create(parent);
    lv_obj_set_size(sg_avatar.root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(sg_avatar.root, lv_color_white(), 0);
    lv_obj_set_style_border_width(sg_avatar.root, 0, 0);
    lv_obj_set_style_pad_all(sg_avatar.root, 0, 0);
    lv_obj_set_style_radius(sg_avatar.root, 0, 0);
    lv_obj_clear_flag(sg_avatar.root, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *status_bar = lv_obj_create(sg_avatar.root);
    lv_obj_set_size(status_bar, LV_HOR_RES, STATUS_BAR_H);
    lv_obj_set_pos(status_bar, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_white(), 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 4, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    sg_avatar.network_label = lv_label_create(status_bar);
    lv_obj_set_style_text_font(sg_avatar.network_label, sg_icon_font, 0);
    lv_label_set_text(sg_avatar.network_label, LV_SYMBOL_WIFI);
    lv_obj_align(sg_avatar.network_label, LV_ALIGN_LEFT_MID, 4, 0);

    sg_avatar.status_label = lv_label_create(status_bar);
    lv_obj_set_style_text_font(sg_avatar.status_label, sg_text_font, 0);
    lv_obj_set_style_text_color(sg_avatar.status_label, lv_color_black(), 0);
    lv_label_set_text(sg_avatar.status_label, sg_cached_status[0] ? sg_cached_status : STANDBY);
    lv_obj_align(sg_avatar.status_label, LV_ALIGN_RIGHT_MID, -4, 0);

    /* BMO's face sits behind a bezel; the plate stays put while the face bobs. */
    sg_avatar.plate = lv_obj_create(sg_avatar.root);
    lv_obj_set_size(sg_avatar.plate, PLATE_W, PLATE_H);
    lv_obj_set_pos(sg_avatar.plate, PLATE_X, PLATE_Y);
    lv_obj_set_style_bg_opa(sg_avatar.plate, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(sg_avatar.plate, lv_color_black(), 0);
    lv_obj_set_style_border_width(sg_avatar.plate, 3, 0);
    lv_obj_set_style_radius(sg_avatar.plate, 22, 0);
    lv_obj_set_style_pad_all(sg_avatar.plate, 0, 0);
    lv_obj_clear_flag(sg_avatar.plate, LV_OBJ_FLAG_SCROLLABLE);

    sg_avatar.face = lv_obj_create(sg_avatar.root);
    lv_obj_set_size(sg_avatar.face, LV_HOR_RES, PLATE_H);
    lv_obj_set_pos(sg_avatar.face, 0, PLATE_Y);
    lv_obj_set_style_bg_opa(sg_avatar.face, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sg_avatar.face, 0, 0);
    lv_obj_set_style_pad_all(sg_avatar.face, 0, 0);
    lv_obj_clear_flag(sg_avatar.face, LV_OBJ_FLAG_SCROLLABLE);

    for (int z = 0; z < ZZZ_COUNT; z++) {
        for (int i = 0; i < 3; i++) {
            sg_avatar.zzz[z].line[i] = __make_stroke(sg_avatar.face, 4);
        }
    }

    sg_avatar.eye_dot_l = __make_solid(sg_avatar.face, lv_color_black());
    sg_avatar.eye_dot_r = __make_solid(sg_avatar.face, lv_color_black());
    sg_avatar.eye_arc_l = __make_arc(sg_avatar.face);
    sg_avatar.eye_arc_r = __make_arc(sg_avatar.face);

    sg_avatar.eye_heart_l = lv_image_create(sg_avatar.face);
    sg_avatar.eye_heart_r = lv_image_create(sg_avatar.face);
    lv_image_set_src(sg_avatar.eye_heart_l, &sg_heart_dsc);
    lv_image_set_src(sg_avatar.eye_heart_r, &sg_heart_dsc);
    __hide(sg_avatar.eye_heart_l);
    __hide(sg_avatar.eye_heart_r);

    for (int i = 0; i < 2; i++) {
        sg_avatar.eye_sq_l[i] = __make_stroke(sg_avatar.face, 6);
        sg_avatar.eye_sq_r[i] = __make_stroke(sg_avatar.face, 6);
    }

    sg_avatar.brow_l = __make_stroke(sg_avatar.face, 7);
    sg_avatar.brow_r = __make_stroke(sg_avatar.face, 7);

    for (int i = 0; i < BLUSH_STROKES; i++) {
        sg_avatar.blush_l[i] = __make_stroke(sg_avatar.face, 3);
        sg_avatar.blush_r[i] = __make_stroke(sg_avatar.face, 3);
        lv_line_set_points(sg_avatar.blush_l[i], sg_blush_pts, 2);
        lv_line_set_points(sg_avatar.blush_r[i], sg_blush_pts, 2);
    }

    sg_avatar.mouth_rect = __make_solid(sg_avatar.face, lv_color_black());
    sg_avatar.mouth_bar  = __make_solid(sg_avatar.face, lv_color_black());
    sg_avatar.mouth_arc  = __make_arc(sg_avatar.face);
    sg_avatar.mouth_pie  = __make_arc(sg_avatar.face);
    sg_avatar.mouth_zig  = __make_stroke(sg_avatar.face, 8);
    __hide(sg_avatar.mouth_bar);

    for (int i = 0; i < 3; i++) {
        sg_avatar.think_dots[i] = __make_solid(sg_avatar.face, lv_color_black());
        lv_obj_set_size(sg_avatar.think_dots[i], 10, 10);
        lv_obj_set_style_radius(sg_avatar.think_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_pos(sg_avatar.think_dots[i], 170 + i * 18, 18);
        __hide(sg_avatar.think_dots[i]);
    }

    sg_avatar.caption_label = lv_label_create(sg_avatar.root);
    lv_obj_set_size(sg_avatar.caption_label, LV_HOR_RES - 8, CAPTION_H);
    lv_obj_align(sg_avatar.caption_label, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_text_font(sg_avatar.caption_label, sg_text_font, 0);
    lv_obj_set_style_text_color(sg_avatar.caption_label, lv_color_black(), 0);
    lv_label_set_long_mode(sg_avatar.caption_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(sg_avatar.caption_label, sg_cached_caption);

    if (sg_avatar.state == AVATAR_IDLE && sg_idle_since_ms == 0) {
        sg_idle_since_ms = tal_system_get_millisecond();
    }
    __schedule_blink();
    __schedule_dart();
    __apply_face_layout();

    sg_avatar.tick_timer  = lv_timer_create(__tick_cb, TICK_MS, NULL);
    sg_avatar.speak_timer = lv_timer_create(__speak_cb, 200, NULL);
    sg_avatar.think_timer = lv_timer_create(__think_cb, 400, NULL);
    sg_avatar.zzz_timer   = lv_timer_create(__zzz_anim_cb, ZZZ_TICK_MS, NULL);

#if EXPR_SELF_TEST
    sg_avatar.self_test_timer = lv_timer_create(__self_test_cb, 3000, NULL);
#endif
}

void avatar_page_destroy(void)
{
    if (sg_avatar.tick_timer) {
        lv_timer_del(sg_avatar.tick_timer);
    }
    if (sg_avatar.speak_timer) {
        lv_timer_del(sg_avatar.speak_timer);
    }
    if (sg_avatar.think_timer) {
        lv_timer_del(sg_avatar.think_timer);
    }
    if (sg_avatar.zzz_timer) {
        lv_timer_del(sg_avatar.zzz_timer);
    }
#if EXPR_SELF_TEST
    if (sg_avatar.self_test_timer) {
        lv_timer_del(sg_avatar.self_test_timer);
    }
#endif
    if (sg_avatar.root) {
        lv_obj_del(sg_avatar.root);
    }
    memset(&sg_avatar, 0, sizeof(sg_avatar));
}

void avatar_set_state(AVATAR_STATE_E st)
{
    /* Callers may re-assert the current state periodically. Restarting the
     * doze countdown on every such call would keep the avatar awake forever. */
    bool changed = (sg_cached_state != st);

    sg_cached_state = st;
    motion_on_avatar_state((int)st);

    if (st != AVATAR_IDLE) {
        sg_idle_since_ms = 0;
    } else if (changed || sg_idle_since_ms == 0) {
        sg_idle_since_ms = tal_system_get_millisecond();
    }

    if (!sg_avatar.root) {
        return;
    }
    sg_avatar.state = st;
    if (changed) {
        sg_avatar.asleep    = false;
        sg_avatar.speak_idx = 0;
    }
    __apply_face_layout();
}

void avatar_set_emotion(const char *emo)
{
    if (avatar_emotion_hold_active()) {
        return;
    }
    if (emo) {
        strncpy(sg_cached_emotion, emo, sizeof(sg_cached_emotion) - 1);
        sg_cached_emotion[sizeof(sg_cached_emotion) - 1] = '\0';
    }
    if (!sg_avatar.root) {
        return;
    }
    strncpy(sg_avatar.emotion, sg_cached_emotion, sizeof(sg_avatar.emotion) - 1);
    sg_avatar.emotion[sizeof(sg_avatar.emotion) - 1] = '\0';
    __apply_face_layout();
}

void avatar_set_caption(const char *txt)
{
    if (txt) {
        strncpy(sg_cached_caption, txt, sizeof(sg_cached_caption) - 1);
        sg_cached_caption[sizeof(sg_cached_caption) - 1] = '\0';
    }
    if (sg_avatar.caption_label && txt) {
        lv_label_set_text(sg_avatar.caption_label, txt);
    }
}

void avatar_set_status_text(const char *txt)
{
    if (txt) {
        strncpy(sg_cached_status, txt, sizeof(sg_cached_status) - 1);
        sg_cached_status[sizeof(sg_cached_status) - 1] = '\0';
    }
    if (sg_avatar.status_label && txt) {
        lv_label_set_text(sg_avatar.status_label, txt);
    }
}

void avatar_set_wifi(AI_UI_WIFI_STATUS_E status)
{
    if (sg_avatar.network_label) {
        lv_label_set_text(sg_avatar.network_label, ai_ui_get_wifi_icon(status));
    }
}
