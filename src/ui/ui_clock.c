/**
 * @file ui_clock.c
 * @brief Split-flap clock page with a secondary alarm / timer editor.
 *
 * Digits normally show local time. Pressing the centre key opens a small menu
 * (alarm / timer); left/right move the focused field, up/down nudge the value,
 * centre confirms, triangle backs out — same secondary-layer idea as calendar.
 */

#include "tal_api.h"
#include "tal_time_service.h"
#include "lvgl.h"
#include "lv_vendor.h"
#include "ai_ui_icon_font.h"
#include "ui_clock.h"
#include "ui_alarm.h"
#include "ui_page_mgr.h"
#include "ui_popup.h"
#include "ui_i18n.h"

#include <stdio.h>

#define CARD_W   78
#define CARD_H   136
#define CARD_Y   62
#define CARD_GAP 10
#define COLON_W  24
#define SEAM_H   3

#define ROW_W  (CARD_W * 4 + CARD_GAP * 2 + COLON_W)
#define ROW_X0 ((LV_HOR_RES - ROW_W) / 2)

#define SEG_T 10
#define SEG_W 46
#define SEG_H 78

#define DIGIT_X ((CARD_W - SEG_W) / 2)
#define DIGIT_Y ((CARD_H - SEG_H) / 2)

#define DIGIT_CNT 4
#define SEG_CNT   7
#define DIGIT_BLANK (-1)

#define BTN_UP    0
#define BTN_DOWN  1
#define BTN_LEFT  2
#define BTN_RIGHT 3
#define BTN_MID   4
#define BTN_TRI   7
#define BTN_GREEN 8

typedef enum {
    CLK_VIEW = 0,
    CLK_MENU,
    CLK_EDIT_ALARM,
    CLK_EDIT_TIMER,
} CLK_MODE_E;

typedef struct {
    uint8_t x, y, w, h;
} SEG_RECT_T;

static const SEG_RECT_T sg_seg_rect[SEG_CNT] = {
    {SEG_T, 0, SEG_W - 2 * SEG_T, SEG_T},
    {SEG_W - SEG_T, SEG_T, SEG_T, (SEG_H - SEG_T) / 2 - SEG_T},
    {SEG_W - SEG_T, (SEG_H + SEG_T) / 2, SEG_T, (SEG_H - SEG_T) / 2 - SEG_T},
    {SEG_T, SEG_H - SEG_T, SEG_W - 2 * SEG_T, SEG_T},
    {0, (SEG_H + SEG_T) / 2, SEG_T, (SEG_H - SEG_T) / 2 - SEG_T},
    {0, SEG_T, SEG_T, (SEG_H - SEG_T) / 2 - SEG_T},
    {SEG_T, (SEG_H - SEG_T) / 2, SEG_W - 2 * SEG_T, SEG_T},
};

static const uint8_t sg_digit_mask[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};


static lv_obj_t *sg_root = NULL;
static lv_obj_t *sg_seg[DIGIT_CNT][SEG_CNT] = {{NULL}};
static lv_obj_t *sg_colon[2] = {NULL};
static lv_obj_t *sg_week_label = NULL;
static lv_obj_t *sg_date_label = NULL;
static lv_obj_t *sg_status = NULL;
static lv_obj_t *sg_hint = NULL;
static lv_obj_t *sg_menu_alarm = NULL;
static lv_obj_t *sg_menu_timer = NULL;
static lv_timer_t *sg_timer = NULL;

static int sg_digit_val[DIGIT_CNT] = {DIGIT_BLANK, DIGIT_BLANK, DIGIT_BLANK, DIGIT_BLANK};
static int sg_shown_mday = -1;

static CLK_MODE_E sg_mode = CLK_VIEW;
static int sg_menu_pick = 0;   /* 0 alarm, 1 timer */
static int sg_field = 0;       /* 0 left pair (HH/MM), 1 right pair (MM/SS) */
static int sg_edit_a = 7;      /* hour or timer minutes */
static int sg_edit_b = 30;     /* minute or timer seconds */

static void __set_digit(int idx, int value)
{
    uint8_t mask;
    lv_coord_t dx;

    if (sg_digit_val[idx] == value || !sg_seg[idx][0]) {
        return;
    }
    sg_digit_val[idx] = value;

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

static void __show_hhmm(int a, int b)
{
    __set_colon(true);
    __set_digit(0, a / 10);
    __set_digit(1, a % 10);
    __set_digit(2, b / 10);
    __set_digit(3, b % 10);
}

static void __set_menu_visible(bool on)
{
    if (!sg_menu_alarm || !sg_menu_timer) {
        return;
    }
    if (on) {
        lv_obj_clear_flag(sg_menu_alarm, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(sg_menu_timer, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(sg_menu_alarm, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(sg_menu_timer, LV_OBJ_FLAG_HIDDEN);
    }
}

static void __refresh_menu_style(void)
{
    if (!sg_menu_alarm) {
        return;
    }
    if (sg_menu_pick == 0) {
        lv_label_set_text_fmt(sg_menu_alarm, "> %s", bmo_tr(BMO_STR_CLK_ALARM));
        lv_label_set_text_fmt(sg_menu_timer, "  %s", bmo_tr(BMO_STR_CLK_TIMER));
    } else {
        lv_label_set_text_fmt(sg_menu_alarm, "  %s", bmo_tr(BMO_STR_CLK_ALARM));
        lv_label_set_text_fmt(sg_menu_timer, "> %s", bmo_tr(BMO_STR_CLK_TIMER));
    }
}

static void __refresh_status_and_hint(void)
{
    ALARM_STATUS_T st;
    char line[64];

    if (!sg_status || !sg_hint) {
        return;
    }

    ui_alarm_get_status(&st);

    if (sg_mode == CLK_VIEW) {
        if (st.timer_on) {
            snprintf(line, sizeof(line), bmo_lang_get()==BMO_LANG_EN ? "Timer %02u:%02u" : "计时 %02u:%02u",
                     (unsigned)(st.timer_remain_s / 60), (unsigned)(st.timer_remain_s % 60));
        } else if (st.alarm_on) {
            snprintf(line, sizeof(line), bmo_lang_get()==BMO_LANG_EN ? "Alarm %02u:%02u" : "闹钟 %02u:%02u", st.alarm_hour, st.alarm_min);
        } else {
            line[0] = '\0';
        }
        lv_label_set_text(sg_status, line);
        lv_label_set_text(sg_hint, bmo_tr(BMO_STR_CLK_HINT));
        __set_menu_visible(false);
    } else if (sg_mode == CLK_MENU) {
        lv_label_set_text(sg_status, bmo_tr(BMO_STR_CLK_MENU_PICK));
        lv_label_set_text(sg_hint, bmo_tr(BMO_STR_CLK_HINT_MENU));
        __set_menu_visible(true);
        __refresh_menu_style();
    } else if (sg_mode == CLK_EDIT_ALARM) {
        snprintf(line, sizeof(line), bmo_lang_get()==BMO_LANG_EN ? "Set alarm  %s" : "设闹钟  %s",
                 sg_field == 0 ? (bmo_lang_get()==BMO_LANG_EN ? "[H] M" : "[时] 分")
                               : (bmo_lang_get()==BMO_LANG_EN ? "H [M]" : "时 [分]"));
        lv_label_set_text(sg_status, line);
        lv_label_set_text(sg_hint, bmo_tr(BMO_STR_CLK_HINT_ALARM));
        __set_menu_visible(false);
        __show_hhmm(sg_edit_a, sg_edit_b);
    } else if (sg_mode == CLK_EDIT_TIMER) {
        snprintf(line, sizeof(line), bmo_lang_get()==BMO_LANG_EN ? "Set timer  %s" : "设计时  %s",
                 sg_field == 0 ? (bmo_lang_get()==BMO_LANG_EN ? "[M] S" : "[分] 秒")
                               : (bmo_lang_get()==BMO_LANG_EN ? "M [S]" : "分 [秒]"));
        lv_label_set_text(sg_status, line);
        lv_label_set_text(sg_hint, bmo_tr(BMO_STR_CLK_HINT_TIMER));
        __set_menu_visible(false);
        __show_hhmm(sg_edit_a, sg_edit_b);
    }
}

static void __clock_timer_cb(lv_timer_t *timer)
{
    POSIX_TM_S tm;

    (void)timer;
    if (!sg_root) {
        return;
    }

    if (sg_mode != CLK_VIEW) {
        __refresh_status_and_hint();
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
            lv_label_set_text(sg_date_label, bmo_tr(BMO_STR_CLK_WAIT_SYNC));
        }
        __refresh_status_and_hint();
        return;
    }

    tal_time_get_local_time_custom(0, &tm);

    __show_hhmm(tm.tm_hour, tm.tm_min);

    if (sg_shown_mday != tm.tm_mday) {
        sg_shown_mday = tm.tm_mday;
        lv_label_set_text_fmt(sg_week_label, "%s%s", bmo_tr(BMO_STR_WEEK_PREFIX), bmo_tr_weekday(tm.tm_wday % 7));
        if (bmo_lang_get() == BMO_LANG_EN) {
            lv_label_set_text_fmt(sg_date_label, "%d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        } else {
            lv_label_set_text_fmt(sg_date_label, "%d年%d月%d日", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        }
    }
    __refresh_status_and_hint();
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

    seam = __block_create(card, lv_color_black(), 0);
    lv_obj_set_size(seam, CARD_W, SEAM_H);
    lv_obj_set_pos(seam, 0, (CARD_H - SEAM_H) / 2);

    seam = __block_create(card, lv_color_white(), 0);
    lv_obj_set_size(seam, CARD_W, 1);
    lv_obj_set_pos(seam, 0, (CARD_H - SEAM_H) / 2 + SEAM_H);
}

static lv_obj_t *__text_label(lv_obj_t *parent, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_label_set_text(label, "");
    return label;
}

static void __enter_menu(void)
{
    ALARM_STATUS_T st;

    sg_mode = CLK_MENU;
    sg_menu_pick = 0;
    ui_alarm_get_status(&st);
    /* Prefill editors from current values. */
    sg_edit_a = st.alarm_hour;
    sg_edit_b = st.alarm_min;
    __refresh_status_and_hint();
}

static void __enter_edit(CLK_MODE_E mode)
{
    ALARM_STATUS_T st;

    ui_alarm_get_status(&st);
    sg_mode = mode;
    sg_field = 0;
    if (mode == CLK_EDIT_ALARM) {
        sg_edit_a = st.alarm_hour;
        sg_edit_b = st.alarm_min;
    } else {
        /* Default 5 minutes for a fresh timer. */
        uint32_t rem = st.timer_on ? st.timer_remain_s : 5 * 60;
        sg_edit_a = (int)(rem / 60);
        sg_edit_b = (int)(rem % 60);
        if (sg_edit_a > 59) {
            sg_edit_a = 59;
        }
    }
    __refresh_status_and_hint();
}

static void __back_to_view(void)
{
    sg_mode = CLK_VIEW;
    sg_shown_mday = -1; /* force date refresh */
    __clock_timer_cb(NULL);
}

static void __nudge_edit(int delta)
{
    if (sg_mode == CLK_EDIT_ALARM) {
        if (sg_field == 0) {
            sg_edit_a = (sg_edit_a + delta + 24) % 24;
        } else {
            sg_edit_b = (sg_edit_b + delta + 60) % 60;
        }
    } else if (sg_mode == CLK_EDIT_TIMER) {
        if (sg_field == 0) {
            sg_edit_a = (sg_edit_a + delta + 60) % 60;
        } else {
            sg_edit_b = (sg_edit_b + delta + 60) % 60;
        }
    }
    __refresh_status_and_hint();
}

static void __confirm_edit(const char **toast_out)
{
    if (sg_mode == CLK_EDIT_ALARM) {
        if (OPRT_OK == ui_alarm_set((uint8_t)sg_edit_a, (uint8_t)sg_edit_b)) {
            if (toast_out) {
                *toast_out = bmo_tr(BMO_STR_TOAST_ALARM_SET);
            }
        } else if (toast_out) {
            *toast_out = "闹钟设置失败";
        }
        __back_to_view();
        return;
    }
    if (sg_mode == CLK_EDIT_TIMER) {
        uint32_t total = (uint32_t)sg_edit_a * 60u + (uint32_t)sg_edit_b;
        if (total == 0) {
            if (toast_out) {
                *toast_out = "时长不能为 0";
            }
            return;
        }
        if (OPRT_OK == ui_alarm_timer_start(total)) {
            if (toast_out) {
                *toast_out = bmo_tr(BMO_STR_TOAST_TIMER_START);
            }
        } else if (toast_out) {
            *toast_out = "计时启动失败";
        }
        __back_to_view();
    }
}

void clock_page_create(lv_obj_t *parent)
{
    lv_font_t *text_font = ai_ui_get_text_font();
    lv_coord_t colon_x = ROW_X0 + CARD_W * 2 + CARD_GAP;

    sg_mode = CLK_VIEW;
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
        lv_obj_set_pos(sg_colon[i], colon_x + (COLON_W - 10) / 2,
                       CARD_Y + (i ? CARD_H * 2 / 3 : CARD_H / 3) - 5);
        lv_obj_add_flag(sg_colon[i], LV_OBJ_FLAG_HIDDEN);
    }

    sg_week_label = __text_label(sg_root, text_font);
    lv_obj_align(sg_week_label, LV_ALIGN_TOP_MID, 0, 8);

    sg_date_label = __text_label(sg_root, text_font);
    lv_label_set_text(sg_date_label, bmo_tr(BMO_STR_CLK_WAIT_SYNC));
    lv_obj_align(sg_date_label, LV_ALIGN_TOP_MID, 0, CARD_Y + CARD_H + 8);

    sg_status = __text_label(sg_root, text_font);
    lv_obj_align(sg_status, LV_ALIGN_TOP_MID, 0, CARD_Y + CARD_H + 36);

    sg_menu_alarm = __text_label(sg_root, text_font);
    lv_obj_align(sg_menu_alarm, LV_ALIGN_BOTTOM_LEFT, 24, -50);
    lv_obj_add_flag(sg_menu_alarm, LV_OBJ_FLAG_HIDDEN);

    sg_menu_timer = __text_label(sg_root, text_font);
    lv_obj_align(sg_menu_timer, LV_ALIGN_BOTTOM_LEFT, 24, -28);
    lv_obj_add_flag(sg_menu_timer, LV_OBJ_FLAG_HIDDEN);

    sg_hint = __text_label(sg_root, text_font);
    lv_obj_set_style_text_align(sg_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(sg_hint, LV_HOR_RES - 16);
    lv_obj_align(sg_hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    sg_timer = lv_timer_create(__clock_timer_cb, 1000, NULL);
    __clock_timer_cb(NULL);
}

void clock_page_destroy(void)
{
    sg_mode = CLK_VIEW;
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
    sg_colon[0] = sg_colon[1] = NULL;
    sg_week_label = sg_date_label = sg_status = sg_hint = NULL;
    sg_menu_alarm = sg_menu_timer = NULL;
}

bool clock_is_editing(void)
{
    return sg_mode != CLK_VIEW;
}

static bool __handle_key(int btn_idx, const char **toast_out)
{
    if (toast_out) {
        *toast_out = NULL;
    }

    if (sg_mode == CLK_VIEW) {
        if (btn_idx == BTN_MID) {
            __enter_menu();
            return true;
        }
        return false;
    }

    if (btn_idx == BTN_TRI) {
        if (sg_mode == CLK_MENU) {
            __back_to_view();
            if (toast_out) {
                *toast_out = bmo_tr(BMO_STR_EXIT_BROWSE);
            }
        } else {
            sg_mode = CLK_MENU;
            __refresh_status_and_hint();
        }
        return true;
    }

    if (sg_mode == CLK_MENU) {
        if (btn_idx == BTN_UP || btn_idx == BTN_DOWN) {
            sg_menu_pick = (btn_idx == BTN_UP) ? 0 : 1;
            __refresh_menu_style();
            return true;
        }
        if (btn_idx == BTN_MID) {
            __enter_edit(sg_menu_pick == 0 ? CLK_EDIT_ALARM : CLK_EDIT_TIMER);
            return true;
        }
        if (btn_idx == BTN_GREEN) {
            if (sg_menu_pick == 0) {
                ui_alarm_clear();
                if (toast_out) {
                    *toast_out = bmo_tr(BMO_STR_TOAST_ALARM_OFF);
                }
            } else {
                ui_alarm_timer_cancel();
                if (toast_out) {
                    *toast_out = bmo_tr(BMO_STR_TOAST_TIMER_CANCEL);
                }
            }
            __back_to_view();
            return true;
        }
        /* Left/right keep paging while in the menu. */
        return false;
    }

    /* Editing alarm or timer. */
    if (btn_idx == BTN_LEFT || btn_idx == BTN_RIGHT) {
        sg_field = (btn_idx == BTN_RIGHT) ? 1 : 0;
        __refresh_status_and_hint();
        return true;
    }
    if (btn_idx == BTN_UP) {
        __nudge_edit(+1);
        return true;
    }
    if (btn_idx == BTN_DOWN) {
        __nudge_edit(-1);
        return true;
    }
    if (btn_idx == BTN_MID) {
        __confirm_edit(toast_out);
        return true;
    }
    return false;
}

bool clock_btn_event(int btn_idx, bool pressed)
{
    bool consumed;
    const char *toast = NULL;

    if (page_mgr_get_current() != PAGE_IDX_CLOCK) {
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
