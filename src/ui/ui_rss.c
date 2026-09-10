/**
 * @file ui_rss.c
 * @brief Maker RSS headlines page with per-item summary detail.
 *
 * Idle → Mid browse. Browse: L/R source, U/D select, Mid open summary, Tri exit.
 * Detail: U/D scroll body, L/R prev/next item, Tri back to browse.
 *
 * Layout is absolute and each row is a fixed-size opaque label. On the ST7305
 * mono panel, labels without a solid background leave previous glyphs behind
 * when text length changes.
 */

#include "ui_rss.h"
#include "ui_bg_task.h"
#include "ui_page_mgr.h"
#include "ui_popup.h"
#include "ui_i18n.h"
#include "tal_api.h"
#include "lvgl.h"
#include "lv_vendor.h"
#include "ai_ui_icon_font.h"

#include <stdio.h>
#include <string.h>

#define RSS_LINES      6
#define RSS_PAD_X      8
#define RSS_HDR_Y      6
#define RSS_RULE_Y     32
#define RSS_LIST_Y     40
#define RSS_HINT_H     28
#define RSS_ALL        (-1)

#define BTN_UP    0
#define BTN_DOWN  1
#define BTN_LEFT  2
#define BTN_RIGHT 3
#define BTN_MID   4
#define BTN_TRI   7
#define BTN_GREEN 8

typedef enum {
    RSS_VIEW_IDLE = 0,
    RSS_VIEW_BROWSE,
    RSS_VIEW_DETAIL,
} RSS_VIEW_E;

static lv_obj_t *sg_root = NULL;
static lv_obj_t *sg_title = NULL;
static lv_obj_t *sg_meta = NULL;
static lv_obj_t *sg_rule = NULL;
static lv_obj_t *sg_line[RSS_LINES] = {NULL};
static lv_obj_t *sg_body = NULL; /* detail summary; hidden in list modes */
static lv_obj_t *sg_hint = NULL;
static lv_obj_t *sg_note = NULL;

static RSS_FEED_DATA_T *sg_data; /* PSRAM copy of headlines + summaries */
static RSS_VIEW_E sg_view = RSS_VIEW_IDLE;
static int  sg_source = RSS_ALL; /* RSS_ALL or 0..RSS_SOURCE_CNT-1 */
static int  sg_scroll = 0;
static int  sg_sel = 0;          /* index into filtered list */
static int  sg_line_h = 26;
static int  sg_body_h = 200;

static int __filtered_count(void)
{
    int n = 0;

    if (!sg_data || !sg_data->valid) {
        return 0;
    }
    for (int i = 0; i < sg_data->count; i++) {
        if (sg_source == RSS_ALL || sg_data->items[i].source == (uint8_t)sg_source) {
            n++;
        }
    }
    return n;
}

static const RSS_ITEM_T *__filtered_at(int visible_idx)
{
    int n = 0;

    if (!sg_data) {
        return NULL;
    }
    for (int i = 0; i < sg_data->count; i++) {
        if (sg_source != RSS_ALL && sg_data->items[i].source != (uint8_t)sg_source) {
            continue;
        }
        if (n == visible_idx) {
            return &sg_data->items[i];
        }
        n++;
    }
    return NULL;
}

static void __clamp_sel(void)
{
    int total = __filtered_count();

    if (total <= 0) {
        sg_sel = 0;
        sg_scroll = 0;
        return;
    }
    if (sg_sel < 0) {
        sg_sel = 0;
    }
    if (sg_sel >= total) {
        sg_sel = total - 1;
    }
    if (sg_sel < sg_scroll) {
        sg_scroll = sg_sel;
    }
    if (sg_sel >= sg_scroll + RSS_LINES) {
        sg_scroll = sg_sel - RSS_LINES + 1;
    }
}

static void __style_row(lv_obj_t *obj, const lv_font_t *font)
{
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_black(), 0);
    lv_obj_set_style_bg_color(obj, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void __set_row_selected(lv_obj_t *obj, bool sel)
{
    lv_obj_set_style_bg_color(obj, sel ? lv_color_black() : lv_color_white(), 0);
    lv_obj_set_style_text_color(obj, sel ? lv_color_white() : lv_color_black(), 0);
}

static void __hide_list_rows(void)
{
    for (int i = 0; i < RSS_LINES; i++) {
        lv_label_set_text(sg_line[i], "");
        __set_row_selected(sg_line[i], false);
        lv_obj_add_flag(sg_line[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void __render_detail(void)
{
    const RSS_ITEM_T *it;
    int content_w = LV_HOR_RES - RSS_PAD_X * 2;
    int total = __filtered_count();

    __clamp_sel();
    it = __filtered_at(sg_sel);
    __hide_list_rows();
    lv_obj_add_flag(sg_note, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(sg_note, "");

    if (!it) {
        lv_label_set_text(sg_title, bmo_tr(BMO_STR_RSS_TITLE));
        lv_label_set_text(sg_meta, "");
        lv_label_set_text(sg_body, bmo_tr(BMO_STR_RSS_NO_SUMMARY));
        lv_obj_clear_flag(sg_body, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(sg_hint, bmo_tr(BMO_STR_RSS_HINT_DETAIL));
        return;
    }

    lv_label_set_text_fmt(sg_title, "%s", it->title);
    lv_label_set_text_fmt(sg_meta, "%d/%d", sg_sel + 1, total);
    lv_obj_align(sg_meta, LV_ALIGN_TOP_RIGHT, -RSS_PAD_X, RSS_HDR_Y);

    if (it->summary[0]) {
        lv_label_set_text_fmt(sg_body, "%s\n\n%s", rss_source_name(it->source), it->summary);
    } else {
        lv_label_set_text_fmt(sg_body, "%s\n\n%s", rss_source_name(it->source),
                              bmo_tr(BMO_STR_RSS_NO_SUMMARY));
    }
    lv_obj_set_size(sg_body, content_w, sg_body_h);
    lv_obj_set_pos(sg_body, RSS_PAD_X, RSS_LIST_Y);
    lv_obj_scroll_to_y(sg_body, 0, LV_ANIM_OFF);
    lv_obj_clear_flag(sg_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(sg_body);

    lv_label_set_text(sg_hint, bmo_tr(BMO_STR_RSS_HINT_DETAIL));
}

static void __render_list(void)
{
    int total;
    int content_w = LV_HOR_RES - RSS_PAD_X * 2;

    lv_obj_add_flag(sg_body, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(sg_body, "");

    __clamp_sel();
    total = __filtered_count();

    if (sg_source == RSS_ALL) {
        lv_label_set_text(sg_title, bmo_tr(BMO_STR_RSS_TITLE));
    } else {
        lv_label_set_text_fmt(sg_title, "%s", rss_source_name(sg_source));
    }

    if (!sg_data || !sg_data->valid) {
        lv_label_set_text(sg_meta, bmo_tr(BMO_STR_RSS_EMPTY));
        lv_label_set_text(sg_note, bmo_tr(BMO_STR_RSS_WAIT));
        lv_obj_clear_flag(sg_note, LV_OBJ_FLAG_HIDDEN);
        __hide_list_rows();
    } else if (total == 0) {
        lv_label_set_text(sg_meta, "0");
        lv_label_set_text(sg_note, bmo_tr(BMO_STR_RSS_NO_TITLES));
        lv_obj_clear_flag(sg_note, LV_OBJ_FLAG_HIDDEN);
        __hide_list_rows();
    } else {
        lv_label_set_text_fmt(sg_meta, "%d/%d", sg_sel + 1, total);
        lv_label_set_text(sg_note, "");
        lv_obj_add_flag(sg_note, LV_OBJ_FLAG_HIDDEN);
        for (int i = 0; i < RSS_LINES; i++) {
            int idx = sg_scroll + i;
            const RSS_ITEM_T *it = __filtered_at(idx);
            bool sel = (sg_view == RSS_VIEW_BROWSE) && (idx == sg_sel);

            if (!it) {
                lv_label_set_text(sg_line[i], "");
                __set_row_selected(sg_line[i], false);
                lv_obj_add_flag(sg_line[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            if (sg_source == RSS_ALL) {
                lv_label_set_text_fmt(sg_line[i], "%s · %s", rss_source_name(it->source), it->title);
            } else {
                lv_label_set_text(sg_line[i], it->title);
            }
            lv_obj_set_size(sg_line[i], content_w, sg_line_h);
            __set_row_selected(sg_line[i], sel);
            lv_obj_clear_flag(sg_line[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_invalidate(sg_line[i]);
        }
    }

    lv_obj_align(sg_meta, LV_ALIGN_TOP_RIGHT, -RSS_PAD_X, RSS_HDR_Y);

    if (sg_view == RSS_VIEW_BROWSE) {
        lv_label_set_text(sg_hint, bmo_tr(BMO_STR_RSS_HINT_BROWSE));
    } else {
        lv_label_set_text(sg_hint, bmo_tr(BMO_STR_RSS_HINT));
    }
}

static void __render(void)
{
    if (!sg_root) {
        return;
    }
    if (sg_view == RSS_VIEW_DETAIL) {
        __render_detail();
    } else {
        __render_list();
    }
    lv_obj_invalidate(sg_root);
}

void rss_page_create(lv_obj_t *parent)
{
    lv_font_t *text_font = ai_ui_get_text_font();
    int content_w = LV_HOR_RES - RSS_PAD_X * 2;
    int list_bottom;
    int avail;

    sg_view = RSS_VIEW_IDLE;
    sg_source = RSS_ALL;
    sg_scroll = 0;
    sg_sel = 0;
    if (!sg_data) {
        sg_data = (RSS_FEED_DATA_T *)tal_psram_malloc(sizeof(*sg_data));
    }
    if (sg_data) {
        memset(sg_data, 0, sizeof(*sg_data));
    }

    sg_line_h = text_font && text_font->line_height > 0 ? text_font->line_height + 4 : 26;
    if (sg_line_h < 22) {
        sg_line_h = 22;
    }

    sg_root = lv_obj_create(parent);
    lv_obj_set_size(sg_root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(sg_root, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(sg_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sg_root, 0, 0);
    lv_obj_set_style_pad_all(sg_root, 0, 0);
    lv_obj_clear_flag(sg_root, LV_OBJ_FLAG_SCROLLABLE);

    sg_title = lv_label_create(sg_root);
    __style_row(sg_title, text_font);
    lv_obj_set_width(sg_title, LV_HOR_RES - 96);
    lv_label_set_long_mode(sg_title, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(sg_title, RSS_PAD_X, RSS_HDR_Y);

    sg_meta = lv_label_create(sg_root);
    __style_row(sg_meta, text_font);
    lv_obj_set_style_text_align(sg_meta, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(sg_meta, LV_ALIGN_TOP_RIGHT, -RSS_PAD_X, RSS_HDR_Y);

    sg_rule = lv_obj_create(sg_root);
    lv_obj_set_size(sg_rule, content_w, 2);
    lv_obj_set_pos(sg_rule, RSS_PAD_X, RSS_RULE_Y);
    lv_obj_set_style_bg_color(sg_rule, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(sg_rule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sg_rule, 0, 0);
    lv_obj_set_style_pad_all(sg_rule, 0, 0);
    lv_obj_clear_flag(sg_rule, LV_OBJ_FLAG_SCROLLABLE);

    list_bottom = LV_VER_RES - RSS_HINT_H - 4;
    avail = list_bottom - RSS_LIST_Y;
    if (avail / RSS_LINES < sg_line_h) {
        sg_line_h = avail / RSS_LINES;
    }
    sg_body_h = avail;

    sg_note = lv_label_create(sg_root);
    __style_row(sg_note, text_font);
    lv_obj_set_size(sg_note, content_w, sg_line_h);
    lv_label_set_long_mode(sg_note, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(sg_note, RSS_PAD_X, RSS_LIST_Y);

    for (int i = 0; i < RSS_LINES; i++) {
        sg_line[i] = lv_label_create(sg_root);
        __style_row(sg_line[i], text_font);
        lv_obj_set_size(sg_line[i], content_w, sg_line_h);
        lv_label_set_long_mode(sg_line[i], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(sg_line[i], RSS_PAD_X, RSS_LIST_Y + i * sg_line_h);
        lv_obj_add_flag(sg_line[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Scrollable wrapped summary for detail view. */
    sg_body = lv_label_create(sg_root);
    __style_row(sg_body, text_font);
    lv_obj_set_size(sg_body, content_w, sg_body_h);
    lv_obj_set_pos(sg_body, RSS_PAD_X, RSS_LIST_Y);
    lv_label_set_long_mode(sg_body, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(sg_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(sg_body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(sg_body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(sg_body, LV_OBJ_FLAG_HIDDEN);

    sg_hint = lv_label_create(sg_root);
    __style_row(sg_hint, text_font);
    lv_obj_set_style_text_align(sg_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(sg_hint, content_w, RSS_HINT_H - 4);
    lv_label_set_long_mode(sg_hint, LV_LABEL_LONG_DOT);
    lv_obj_align(sg_hint, LV_ALIGN_BOTTOM_MID, 0, -4);

    __render();
}

void rss_page_destroy(void)
{
    sg_view = RSS_VIEW_IDLE;
    if (sg_root) {
        lv_obj_del(sg_root);
        sg_root = NULL;
    }
    sg_title = sg_meta = sg_hint = sg_note = sg_rule = sg_body = NULL;
    for (int i = 0; i < RSS_LINES; i++) {
        sg_line[i] = NULL;
    }
    /* Keep sg_data in PSRAM across page recreations — cleared on next create. */
}

void rss_page_on_press(void)
{
    ui_bg_task_request_refresh();
}

void rss_page_update(const RSS_FEED_DATA_T *data)
{
    if (!data) {
        return;
    }
    if (!sg_data) {
        sg_data = (RSS_FEED_DATA_T *)tal_psram_malloc(sizeof(*sg_data));
        if (!sg_data) {
            return;
        }
    }
    memcpy(sg_data, data, sizeof(*sg_data));
    /* Keep selection if possible; leave detail so text matches fresh cache. */
    if (sg_view == RSS_VIEW_DETAIL) {
        sg_view = RSS_VIEW_BROWSE;
    }
    __clamp_sel();
    __render();
}

bool rss_is_browsing(void)
{
    return sg_view != RSS_VIEW_IDLE;
}

static bool __handle_key(int btn_idx, const char **toast_out)
{
    if (toast_out) {
        *toast_out = NULL;
    }

    if (sg_view == RSS_VIEW_IDLE) {
        if (btn_idx == BTN_MID) {
            sg_view = RSS_VIEW_BROWSE;
            __clamp_sel();
            __render();
            return true;
        }
        if (btn_idx == BTN_GREEN) {
            rss_page_on_press();
            if (toast_out) {
                *toast_out = bmo_tr(BMO_STR_REFRESHING);
            }
            return true;
        }
        return false;
    }

    if (sg_view == RSS_VIEW_DETAIL) {
        if (btn_idx == BTN_TRI) {
            sg_view = RSS_VIEW_BROWSE;
            __render();
            return true;
        }
        if (btn_idx == BTN_UP) {
            lv_obj_scroll_by(sg_body, 0, sg_line_h, LV_ANIM_OFF);
            return true;
        }
        if (btn_idx == BTN_DOWN) {
            lv_obj_scroll_by(sg_body, 0, -sg_line_h, LV_ANIM_OFF);
            return true;
        }
        if (btn_idx == BTN_LEFT) {
            if (sg_sel > 0) {
                sg_sel--;
                __render();
            }
            return true;
        }
        if (btn_idx == BTN_RIGHT) {
            if (sg_sel + 1 < __filtered_count()) {
                sg_sel++;
                __render();
            }
            return true;
        }
        if (btn_idx == BTN_GREEN) {
            rss_page_on_press();
            if (toast_out) {
                *toast_out = bmo_tr(BMO_STR_REFRESHING);
            }
            return true;
        }
        /* Mid ignored in detail — already reading. */
        if (btn_idx == BTN_MID) {
            return true;
        }
        return false;
    }

    /* Browse */
    if (btn_idx == BTN_TRI) {
        sg_view = RSS_VIEW_IDLE;
        __render();
        if (toast_out) {
            *toast_out = bmo_tr(BMO_STR_EXIT_BROWSE);
        }
        return true;
    }

    if (btn_idx == BTN_MID) {
        if (__filtered_count() > 0) {
            sg_view = RSS_VIEW_DETAIL;
            __render();
        }
        return true;
    }

    if (btn_idx == BTN_GREEN) {
        rss_page_on_press();
        if (toast_out) {
            *toast_out = bmo_tr(BMO_STR_REFRESHING);
        }
        return true;
    }

    if (btn_idx == BTN_LEFT) {
        if (sg_source == RSS_ALL) {
            sg_source = RSS_SOURCE_CNT - 1;
        } else if (sg_source == 0) {
            sg_source = RSS_ALL;
        } else {
            sg_source--;
        }
        sg_sel = 0;
        sg_scroll = 0;
        __render();
        return true;
    }
    if (btn_idx == BTN_RIGHT) {
        if (sg_source == RSS_ALL) {
            sg_source = 0;
        } else if (sg_source >= RSS_SOURCE_CNT - 1) {
            sg_source = RSS_ALL;
        } else {
            sg_source++;
        }
        sg_sel = 0;
        sg_scroll = 0;
        __render();
        return true;
    }
    if (btn_idx == BTN_UP) {
        if (sg_sel > 0) {
            sg_sel--;
            __render();
        }
        return true;
    }
    if (btn_idx == BTN_DOWN) {
        if (sg_sel + 1 < __filtered_count()) {
            sg_sel++;
            __render();
        }
        return true;
    }
    return false;
}

bool rss_btn_event(int btn_idx, bool pressed)
{
    bool consumed;
    const char *toast = NULL;

    if (page_mgr_get_current() != PAGE_IDX_RSS) {
        return false;
    }
    if (!pressed) {
        return false;
    }

    lv_vendor_disp_lock();
    consumed = __handle_key(btn_idx, &toast);
    lv_vendor_disp_unlock();
    if (toast) {
        ui_popup_toast(toast);
    }
    return consumed;
}
