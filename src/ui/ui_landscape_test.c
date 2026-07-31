/**
 * @file ui_landscape_test.c
 * @brief Corner digit overlay for landscape rotation verification.
 *
 * Expected on panel (logical 400x300):
 *   1 top-left    2 top-right
 *   3 bottom-left 4 bottom-right
 */

#include "ui_landscape_test.h"
#include "ai_ui_icon_font.h"
#include "tal_api.h"

#if defined(UI_LANDSCAPE_TEST) && (UI_LANDSCAPE_TEST == 1)

typedef struct {
    lv_obj_t *root;
    lv_timer_t *timer;
} LANDSCAPE_TEST_T;

static LANDSCAPE_TEST_T sg_test;

static void __test_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (sg_test.root) {
        lv_obj_del(sg_test.root);
        sg_test.root = NULL;
    }
    if (sg_test.timer) {
        lv_timer_del(sg_test.timer);
        sg_test.timer = NULL;
    }
    PR_NOTICE("[landscape] corner test done");
}

static lv_obj_t *__corner_label(lv_obj_t *parent, const char *text, lv_align_t align,
                                 int x_ofs, int y_ofs)
{
    lv_font_t *font = ai_ui_get_text_font();
    lv_obj_t *lbl = lv_label_create(parent);

    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    lv_label_set_text(lbl, text);
    lv_obj_align(lbl, align, x_ofs, y_ofs);
    return lbl;
}

void ui_landscape_test_show(lv_obj_t *parent, uint32_t hold_ms)
{
    if (!parent) {
        return;
    }

    sg_test.root = lv_obj_create(parent);
    lv_obj_set_size(sg_test.root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(sg_test.root, lv_color_white(), 0);
    lv_obj_set_style_border_width(sg_test.root, 0, 0);
    lv_obj_set_style_pad_all(sg_test.root, 0, 0);
    lv_obj_clear_flag(sg_test.root, LV_OBJ_FLAG_SCROLLABLE);

    __corner_label(sg_test.root, "1", LV_ALIGN_TOP_LEFT, 8, 8);
    __corner_label(sg_test.root, "2", LV_ALIGN_TOP_RIGHT, -8, 8);
    __corner_label(sg_test.root, "3", LV_ALIGN_BOTTOM_LEFT, 8, -8);
    __corner_label(sg_test.root, "4", LV_ALIGN_BOTTOM_RIGHT, -8, -8);

    lv_obj_t *mid = lv_label_create(sg_test.root);
    lv_obj_set_style_text_font(mid, ai_ui_get_text_font(), 0);
    lv_obj_set_style_text_color(mid, lv_color_black(), 0);
    lv_label_set_text(mid, "400x300");
    lv_obj_align(mid, LV_ALIGN_CENTER, 0, 0);

    sg_test.timer = lv_timer_create(__test_timer_cb, hold_ms, NULL);
    lv_timer_set_repeat_count(sg_test.timer, 1);
    PR_NOTICE("[landscape] corner test %u ms", (unsigned)hold_ms);
}

#else

void ui_landscape_test_show(lv_obj_t *parent, uint32_t hold_ms)
{
    (void)parent;
    (void)hold_ms;
}

#endif
