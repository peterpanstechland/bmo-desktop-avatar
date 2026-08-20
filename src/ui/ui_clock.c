/**
 * @file ui_clock.c
 * @brief Split-flap clock page using cloud-synced local time.
 *
 * Each digit is a black card with a white seven-segment numeral and a black
 * seam across the middle, so the numeral looks split the way a real flip clock
 * reads. The digits are drawn from plain rectangles rather than a font: the
 * largest CJK face available here is 30 px, far too small to carry a 400x300
 * panel, and scaling a bitmap font up would fray the edges once the driver
 * thresholds it to pure black and white.
 */

#include "tal_api.h"
#include "tal_time_service.h"
#include "lvgl.h"
#include "ai_ui_icon_font.h"
#include "ui_clock.h"

#define CARD_W   78
#define CARD_H   136
#define CARD_Y   62
#define CARD_GAP 10
#define COLON_W  24
#define SEAM_H   3

/* Left edge of the first card, chosen so the whole row is centred:
 * 4 cards + 2 in-group gaps + the colon column. */
#define ROW_W  (CARD_W * 4 + CARD_GAP * 2 + COLON_W)
#define ROW_X0 ((LV_HOR_RES - ROW_W) / 2)

#define SEG_T 10 /* stroke thickness */
#define SEG_W 46 /* numeral bounding box */
#define SEG_H 78

#define DIGIT_X ((CARD_W - SEG_W) / 2)
#define DIGIT_Y ((CARD_H - SEG_H) / 2)

#define DIGIT_CNT 4
#define SEG_CNT   7

#define DIGIT_BLANK (-1)

typedef struct {
    uint8_t x, y, w, h;
} SEG_RECT_T;

/* Segment order is the usual a..g. */
static const SEG_RECT_T sg_seg_rect[SEG_CNT] = {
    {SEG_T, 0, SEG_W - 2 * SEG_T, SEG_T},                          /* a  top      */
    {SEG_W - SEG_T, SEG_T, SEG_T, (SEG_H - SEG_T) / 2 - SEG_T},    /* b  upper r  */
    {SEG_W - SEG_T, (SEG_H + SEG_T) / 2, SEG_T,                    /* c  lower r  */
     (SEG_H - SEG_T) / 2 - SEG_T},
    {SEG_T, SEG_H - SEG_T, SEG_W - 2 * SEG_T, SEG_T},              /* d  bottom   */
    {0, (SEG_H + SEG_T) / 2, SEG_T, (SEG_H - SEG_T) / 2 - SEG_T},  /* e  lower l  */
    {0, SEG_T, SEG_T, (SEG_H - SEG_T) / 2 - SEG_T},                /* f  upper l  */
    {SEG_T, (SEG_H - SEG_T) / 2, SEG_W - 2 * SEG_T, SEG_T},        /* g  middle   */
};

static const uint8_t sg_digit_mask[10] = {
    0x3F, /* 0 */
    0x06, /* 1 */
    0x5B, /* 2 */
    0x4F, /* 3 */
    0x66, /* 4 */
    0x6D, /* 5 */
    0x7D, /* 6 */
    0x07, /* 7 */
    0x7F, /* 8 */
    0x6F, /* 9 */
};

static const char *WEEK_CN[] = {"日", "一", "二", "三", "四", "五", "六"};

static lv_obj_t *sg_root                       = NULL;
static lv_obj_t *sg_seg[DIGIT_CNT][SEG_CNT]    = {{NULL}};
static lv_obj_t *sg_colon[2]                   = {NULL};
static lv_obj_t *sg_week_label                 = NULL;
static lv_obj_t *sg_date_label                 = NULL;
static lv_timer_t *sg_timer                    = NULL;

static int sg_digit_val[DIGIT_CNT] = {DIGIT_BLANK, DIGIT_BLANK, DIGIT_BLANK, DIGIT_BLANK};
static int sg_shown_mday           = -1;

static void __set_digit(int idx, int value)
{
    uint8_t mask;
    lv_coord_t dx;

    if (sg_digit_val[idx] == value || !sg_seg[idx][0]) {
        return;
    }
    sg_digit_val[idx] = value;

    /* A seven-segment 1 is drawn on the two right-hand strokes, which leaves it
     * hanging against the edge of the card. Slide it to the middle instead. */
    dx = (value == 1) ? -(SEG_W - SEG_T) / 2 : 0;

    mask = (value >= 0 && value <= 9) ? sg_digit_mask[value] : 0;
    for (int s = 0; s < SEG_CNT; s++) {
        lv_obj_set_pos(sg_seg[idx][s], DIGIT_X + sg_seg_rect[s].x + dx, DIGIT_Y + sg_seg_rect[s].y);
        if (mask & (1u << s)) {
            lv_obj_clear_flag(sg_seg[idx][s], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(sg_seg[idx][s], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void __set_colon(bool on)
{
    for (int i = 0; i < 2; i++) {
        if (!sg_colon[i]) {
            continue;
        }
        if (on) {
            lv_obj_clear_flag(sg_colon[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(sg_colon[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/*
 * Runs every second but only touches objects when a value really changed. The
 * panel is refreshed as a whole frame, so an unconditional redraw here would
 * cost a full flush every second for a display that only changes once a minute.
 */
static void __clock_timer_cb(lv_timer_t *timer)
{
    POSIX_TM_S tm;

    (void)timer;
    if (!sg_root) {
        return;
    }

    if (OPRT_OK != tal_time_check_time_sync()) {
        for (int i = 0; i < DIGIT_CNT; i++) {
            __set_digit(i, DIGIT_BLANK);
        }
        __set_colon(false);
        if (sg_shown_mday != -1) {
            sg_shown_mday = -1;
            lv_label_set_text(sg_week_label, "");
            lv_label_set_text(sg_date_label, "等待时间同步...");
        }
        return;
    }

    tal_time_get_local_time_custom(0, &tm);

    __set_colon(true);
    __set_digit(0, tm.tm_hour / 10);
    __set_digit(1, tm.tm_hour % 10);
    __set_digit(2, tm.tm_min / 10);
    __set_digit(3, tm.tm_min % 10);

    if (sg_shown_mday != tm.tm_mday) {
        sg_shown_mday = tm.tm_mday;
        lv_label_set_text_fmt(sg_week_label, "星期%s", WEEK_CN[tm.tm_wday % 7]);
        lv_label_set_text_fmt(sg_date_label, "%d年%d月%d日", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    }
}

static lv_obj_t *__block_create(lv_obj_t *parent, lv_color_t color, lv_coord_t radius)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    return obj;
}

static void __card_create(int idx, lv_coord_t x)
{
    lv_obj_t *card = __block_create(sg_root, lv_color_black(), 8);
    lv_obj_t *seam;

    lv_obj_set_pos(card, x, CARD_Y);
    lv_obj_set_size(card, CARD_W, CARD_H);

    for (int s = 0; s < SEG_CNT; s++) {
        lv_obj_t *seg = __block_create(card, lv_color_white(), 3);

        lv_obj_set_pos(seg, DIGIT_X + sg_seg_rect[s].x, DIGIT_Y + sg_seg_rect[s].y);
        lv_obj_set_size(seg, sg_seg_rect[s].w, sg_seg_rect[s].h);
        lv_obj_add_flag(seg, LV_OBJ_FLAG_HIDDEN);
        sg_seg[idx][s] = seg;
    }

    /*
     * Two lines, created last so they sit above the segments. The dark one is
     * the gap itself and only shows where it crosses a white stroke; the light
     * one below it is the lit top edge of the lower flap and only shows against
     * the black card. Together they read as one continuous split at every point
     * across the card, which a single line of either colour cannot do on a
     * panel with no greys.
     */
    seam = __block_create(card, lv_color_black(), 0);
    lv_obj_set_size(seam, CARD_W, SEAM_H);
    lv_obj_set_pos(seam, 0, (CARD_H - SEAM_H) / 2);

    seam = __block_create(card, lv_color_white(), 0);
    lv_obj_set_size(seam, CARD_W, 1);
    lv_obj_set_pos(seam, 0, (CARD_H - SEAM_H) / 2 + SEAM_H);
}

void clock_page_create(lv_obj_t *parent)
{
    lv_font_t *text_font = ai_ui_get_text_font();
    lv_coord_t colon_x   = ROW_X0 + CARD_W * 2 + CARD_GAP;

    sg_root = lv_obj_create(parent);
    lv_obj_set_size(sg_root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(sg_root, lv_color_white(), 0);
    lv_obj_set_style_border_width(sg_root, 0, 0);
    lv_obj_set_style_pad_all(sg_root, 0, 0);
    lv_obj_clear_flag(sg_root, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < DIGIT_CNT; i++) {
        sg_digit_val[i] = DIGIT_BLANK;
    }
    sg_shown_mday = -1;

    __card_create(0, ROW_X0);
    __card_create(1, ROW_X0 + CARD_W + CARD_GAP);
    __card_create(2, colon_x + COLON_W);
    __card_create(3, colon_x + COLON_W + CARD_W + CARD_GAP);

    for (int i = 0; i < 2; i++) {
        sg_colon[i] = __block_create(sg_root, lv_color_black(), 3);
        lv_obj_set_size(sg_colon[i], 10, 10);
        lv_obj_set_pos(sg_colon[i], colon_x + (COLON_W - 10) / 2, CARD_Y + (i ? CARD_H * 2 / 3 : CARD_H / 3) - 5);
        lv_obj_add_flag(sg_colon[i], LV_OBJ_FLAG_HIDDEN);
    }

    sg_week_label = lv_label_create(sg_root);
    lv_obj_set_style_text_font(sg_week_label, text_font, 0);
    lv_obj_set_style_text_color(sg_week_label, lv_color_black(), 0);
    lv_label_set_text(sg_week_label, "");
    lv_obj_align(sg_week_label, LV_ALIGN_TOP_MID, 0, 16);

    sg_date_label = lv_label_create(sg_root);
    lv_obj_set_style_text_font(sg_date_label, text_font, 0);
    lv_obj_set_style_text_color(sg_date_label, lv_color_black(), 0);
    lv_label_set_text(sg_date_label, "等待时间同步...");
    lv_obj_align(sg_date_label, LV_ALIGN_TOP_MID, 0, CARD_Y + CARD_H + 16);

    sg_timer = lv_timer_create(__clock_timer_cb, 1000, NULL);
    __clock_timer_cb(NULL);
}

void clock_page_destroy(void)
{
    if (sg_timer) {
        lv_timer_del(sg_timer);
        sg_timer = NULL;
    }
    if (sg_root) {
        lv_obj_del(sg_root);
        sg_root = NULL;
    }
    for (int i = 0; i < DIGIT_CNT; i++) {
        for (int s = 0; s < SEG_CNT; s++) {
            sg_seg[i][s] = NULL;
        }
    }
    sg_colon[0] = NULL;
    sg_colon[1] = NULL;
    sg_week_label = NULL;
    sg_date_label = NULL;
}
