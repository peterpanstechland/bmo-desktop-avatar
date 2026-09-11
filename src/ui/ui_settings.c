/**
 * @file ui_settings.c
 * @brief Settings page: language, firmware version, OTA check / update.
 *
 * Opaque fixed-row layout (same mono-LCD ghosting rules as RSS).
 */

#include "ui_settings.h"
#include "ui_i18n.h"
#include "ui_page_mgr.h"
#include "ui_popup.h"
#include "bmo_ota.h"
#include "tal_api.h"
#include "lvgl.h"
#include "lv_vendor.h"
#include "ai_ui_icon_font.h"

#include <stdio.h>
#include <string.h>

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.2"
#endif

#define SET_ROWS   4
#define SET_PAD_X  8
#define SET_HDR_Y  6
#define SET_RULE_Y 32
#define SET_LIST_Y 40
#define SET_HINT_H 28
#define SET_ROW_H  36

#define BTN_UP    0
#define BTN_DOWN  1
#define BTN_LEFT  2
#define BTN_RIGHT 3
#define BTN_MID   4
#define BTN_TRI   7
#define BTN_GREEN 8

enum {
    ROW_LANG = 0,
    ROW_VER = 1,
    ROW_CHECK = 2,
    ROW_UPDATE = 3,
};

static lv_obj_t *sg_root = NULL;
static lv_obj_t *sg_title = NULL;
static lv_obj_t *sg_rule = NULL;
static lv_obj_t *sg_row[SET_ROWS] = {NULL};
static lv_obj_t *sg_hint = NULL;

static bool sg_browse = false;
static int  sg_sel = 0;

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

static void __render(void)
{
    char line[96];
    const char *lang_val;
    int content_w = LV_HOR_RES - SET_PAD_X * 2;

    if (!sg_root) {
        return;
    }

    lv_label_set_text(sg_title, bmo_tr(BMO_STR_SETTINGS_TITLE));
    lang_val = (bmo_lang_get() == BMO_LANG_EN) ? bmo_tr(BMO_STR_LANG_EN) : bmo_tr(BMO_STR_LANG_ZH);

    for (int i = 0; i < SET_ROWS; i++) {
        const char *prefix = (sg_browse && sg_sel == i) ? ">" : " ";
        bool sel = sg_browse && (sg_sel == i);

        switch (i) {
        case ROW_LANG:
            snprintf(line, sizeof(line), "%s %s: %s", prefix, bmo_tr(BMO_STR_LANGUAGE), lang_val);
            break;
        case ROW_VER:
            snprintf(line, sizeof(line), "%s %s: %s", prefix, bmo_tr(BMO_STR_FIRMWARE), PROJECT_VERSION);
            break;
        case ROW_CHECK:
            snprintf(line, sizeof(line), "%s %s", prefix, bmo_tr(BMO_STR_CHECK_UPDATE));
            break;
        case ROW_UPDATE:
            snprintf(line, sizeof(line), "%s %s", prefix, bmo_tr(BMO_STR_UPDATE));
            break;
        default:
            line[0] = '\0';
            break;
        }
        lv_label_set_text(sg_row[i], line);
        lv_obj_set_size(sg_row[i], content_w, SET_ROW_H - 4);
        lv_obj_set_style_bg_color(sg_row[i], sel ? lv_color_black() : lv_color_white(), 0);
        lv_obj_set_style_text_color(sg_row[i], sel ? lv_color_white() : lv_color_black(), 0);
        lv_obj_invalidate(sg_row[i]);
    }

    lv_label_set_text(sg_hint, sg_browse ? bmo_tr(BMO_STR_HINT_BROWSE) : bmo_tr(BMO_STR_HINT_ENTER));
    lv_obj_invalidate(sg_root);
}

void settings_page_create(lv_obj_t *parent)
{
    lv_font_t *text_font = ai_ui_get_text_font();
    int content_w = LV_HOR_RES - SET_PAD_X * 2;

    sg_browse = false;
    sg_sel = 0;

    sg_root = lv_obj_create(parent);
    lv_obj_set_size(sg_root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(sg_root, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(sg_root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sg_root, 0, 0);
    lv_obj_set_style_pad_all(sg_root, 0, 0);
    lv_obj_clear_flag(sg_root, LV_OBJ_FLAG_SCROLLABLE);

    sg_title = lv_label_create(sg_root);
    __style_row(sg_title, text_font);
    lv_obj_set_width(sg_title, content_w);
    lv_label_set_long_mode(sg_title, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(sg_title, SET_PAD_X, SET_HDR_Y);

    sg_rule = lv_obj_create(sg_root);
    lv_obj_set_size(sg_rule, content_w, 2);
    lv_obj_set_pos(sg_rule, SET_PAD_X, SET_RULE_Y);
    lv_obj_set_style_bg_color(sg_rule, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(sg_rule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sg_rule, 0, 0);
    lv_obj_set_style_pad_all(sg_rule, 0, 0);
    lv_obj_clear_flag(sg_rule, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < SET_ROWS; i++) {
        sg_row[i] = lv_label_create(sg_root);
        __style_row(sg_row[i], text_font);
        lv_obj_set_size(sg_row[i], content_w, SET_ROW_H - 4);
        lv_label_set_long_mode(sg_row[i], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(sg_row[i], SET_PAD_X, SET_LIST_Y + i * SET_ROW_H);
    }

    sg_hint = lv_label_create(sg_root);
    __style_row(sg_hint, text_font);
    lv_obj_set_style_text_align(sg_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_size(sg_hint, content_w, SET_HINT_H - 4);
    lv_label_set_long_mode(sg_hint, LV_LABEL_LONG_DOT);
    lv_obj_align(sg_hint, LV_ALIGN_BOTTOM_MID, 0, -4);

    __render();
}

void settings_page_destroy(void)
{
    sg_browse = false;
    sg_sel = 0;
    if (sg_root) {
        lv_obj_del(sg_root);
        sg_root = NULL;
    }
    sg_title = sg_rule = sg_hint = NULL;
    for (int i = 0; i < SET_ROWS; i++) {
        sg_row[i] = NULL;
    }
}

void settings_page_on_press(void)
{
    /* Green is handled in browse mode via settings_btn_event. */
}

bool settings_is_browsing(void)
{
    return sg_browse;
}

static void __run_row(void)
{
    switch (sg_sel) {
    case ROW_LANG:
        bmo_lang_toggle();
        __render();
        break;
    case ROW_VER:
        break;
    case ROW_CHECK:
        bmo_ota_check();
        break;
    case ROW_UPDATE:
        if (bmo_ota_is_busy()) {
            ui_popup_toast(bmo_tr(BMO_STR_OTA_BUSY));
        } else {
            bmo_ota_check();
        }
        break;
    default:
        break;
    }
}

static bool __handle_key(int btn_idx, const char **toast_out)
{
    if (toast_out) {
        *toast_out = NULL;
    }

    if (!sg_browse) {
        if (btn_idx == BTN_MID) {
            sg_browse = true;
            sg_sel = 0;
            __render();
            return true;
        }
        return false;
    }

    if (btn_idx == BTN_TRI) {
        sg_browse = false;
        __render();
        if (toast_out) {
            *toast_out = bmo_tr(BMO_STR_EXIT_BROWSE);
        }
        return true;
    }

    if (btn_idx == BTN_UP) {
        sg_sel = (sg_sel + SET_ROWS - 1) % SET_ROWS;
        __render();
        return true;
    }
    if (btn_idx == BTN_DOWN) {
        sg_sel = (sg_sel + 1) % SET_ROWS;
        __render();
        return true;
    }

    if (btn_idx == BTN_LEFT || btn_idx == BTN_RIGHT) {
        if (sg_sel == ROW_LANG) {
            bmo_lang_toggle();
            __render();
        }
        return true;
    }

    if (btn_idx == BTN_MID || btn_idx == BTN_GREEN) {
        __run_row();
        return true;
    }

    return false;
}

bool settings_btn_event(int btn_idx, bool pressed)
{
    bool consumed;
    const char *toast = NULL;

    if (page_mgr_get_current() != PAGE_IDX_SETTINGS) {
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
