/**
 * @file ui_chat_overlay.c
 * @brief Top-layer chat status bar when not on avatar page.
 */

#include "tal_api.h"
#include <string.h>
#include "lvgl.h"
#include "lv_vendor.h"
#include "ai_ui_icon_font.h"
#include "lang_config.h"
#include "ui_chat_overlay.h"
#include "ui_page_mgr.h"

#define OVERLAY_H  36

typedef struct {
    lv_obj_t *bar;
    lv_obj_t *state_label;
    lv_obj_t *caption_label;
    bool ready;
} CHAT_OVERLAY_T;

static CHAT_OVERLAY_T sg_overlay;

void chat_overlay_init(void)
{
    lv_font_t *font = ai_ui_get_text_font();

    memset(&sg_overlay, 0, sizeof(sg_overlay));

    sg_overlay.bar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(sg_overlay.bar, LV_HOR_RES, OVERLAY_H);
    lv_obj_set_pos(sg_overlay.bar, 0, 0);
    lv_obj_set_style_bg_color(sg_overlay.bar, lv_color_white(), 0);
    lv_obj_set_style_border_width(sg_overlay.bar, 1, 0);
    lv_obj_set_style_border_color(sg_overlay.bar, lv_color_black(), 0);
    lv_obj_set_style_border_side(sg_overlay.bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_pad_all(sg_overlay.bar, 4, 0);
    lv_obj_set_style_radius(sg_overlay.bar, 0, 0);
    lv_obj_clear_flag(sg_overlay.bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sg_overlay.bar, LV_OBJ_FLAG_HIDDEN);

    sg_overlay.state_label = lv_label_create(sg_overlay.bar);
    lv_obj_set_style_text_font(sg_overlay.state_label, font, 0);
    lv_obj_set_style_text_color(sg_overlay.state_label, lv_color_black(), 0);
    lv_label_set_text(sg_overlay.state_label, "");
    lv_obj_align(sg_overlay.state_label, LV_ALIGN_LEFT_MID, 0, 0);

    sg_overlay.caption_label = lv_label_create(sg_overlay.bar);
    lv_obj_set_width(sg_overlay.caption_label, LV_HOR_RES - 90);
    lv_obj_set_style_text_font(sg_overlay.caption_label, font, 0);
    lv_obj_set_style_text_color(sg_overlay.caption_label, lv_color_black(), 0);
    lv_label_set_long_mode(sg_overlay.caption_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(sg_overlay.caption_label, "");
    lv_obj_align(sg_overlay.caption_label, LV_ALIGN_RIGHT_MID, 0, 0);

    sg_overlay.ready = true;
}

void chat_overlay_update(const char *state_text, const char *caption)
{
    bool on_avatar;
    bool active;
    const char *st = state_text ? state_text : "";

    if (!sg_overlay.ready || !sg_overlay.bar) {
        return;
    }

    on_avatar = (page_mgr_get_current() == 0);
    /* Hide overlay on avatar page, or when idle/standby (CJK or ASCII). */
    active = !on_avatar && st[0] &&
             (0 != strcmp(st, STANDBY)) &&
             (0 != strcmp(st, "Say NiHaoTuYa / Click")) &&
             (0 != strcmp(st, "Standby"));

    if (!active) {
        lv_obj_add_flag(sg_overlay.bar, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(sg_overlay.bar, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(sg_overlay.state_label, st);
    lv_label_set_text(sg_overlay.caption_label, caption ? caption : "");
}
