/**
 * @file ui_calendar.c
 * @brief Month grid with today highlighted, plus a Feishu agenda for one day.
 *
 * Layout on the 400x300 landscape mono panel: the month sits on the left, the
 * agenda for the focused day on the right. Days that have a synced event are
 * outlined; the focused day is drawn inverted.
 *
 * The D-pad normally pages/volumes. Pressing the centre key on this page enters
 * a browse mode that steals UP/DOWN/LEFT/RIGHT to move the focused day within
 * the synced window (today .. today+3). Triangle exits browse and restores the
 * global bindings — same pattern as the games page.
 */

#include <stdio.h>
#include <string.h>
#include "tal_api.h"
#include "tal_time_service.h"
#include "lvgl.h"
#include "ai_ui_icon_font.h"
#include "ui_calendar.h"
#include "ui_bg_task.h"
#include "ui_page_mgr.h"
#include "ui_popup.h"
#include "ui_i18n.h"
#include "lv_vendor.h"

#define CAL_COLS   7
#define CAL_ROWS   6
#define CAL_CELLS  (CAL_COLS * CAL_ROWS)
#define CAL_X0     8
#define CAL_CELL_W 30
#define CAL_CELL_H 29
#define CAL_HDR_Y  36
#define CAL_GRID_Y 60
#define CAL_DAY_W  26
#define CAL_DAY_H  20

#define TASK_X0        228
#define TASK_HDR_Y     36
#define TASK_Y0        62
#define TASK_LINE_H    26
#define TASK_MAX_LINES 8

/* Matches feishu_cal_fetch()'s window: today and the next three days. */
#define CAL_BROWSE_DAYS 4

/* Re-render often enough that the highlight follows midnight. */
#define CAL_TICK_MS 60000

#define BTN_UP    0
#define BTN_DOWN  1
#define BTN_LEFT  2
#define BTN_RIGHT 3
#define BTN_MID   4
#define BTN_TRI   7
#define BTN_GREEN 8


static lv_obj_t *sg_root = NULL;
static lv_obj_t *sg_title = NULL;
static lv_obj_t *sg_today = NULL;
static lv_obj_t *sg_wday[CAL_COLS] = {NULL};
static lv_obj_t *sg_cell[CAL_CELLS] = {NULL};
static lv_obj_t *sg_task_hdr = NULL;
static lv_obj_t *sg_task[TASK_MAX_LINES] = {NULL};
static lv_obj_t *sg_task_note = NULL;
static lv_obj_t *sg_hint = NULL;
static lv_timer_t *sg_tick = NULL;

static FEISHU_CAL_DATA_T sg_data;
static bool sg_browse = false;
static int  sg_day_offset = 0; /* 0 = today … CAL_BROWSE_DAYS-1 */

static int __days_in_month(int year, int mon)
{
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (mon == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
        return 29;
    }
    return days[mon - 1];
}

static bool __day_has_event(int mon, int mday)
{
    if (!sg_data.valid) {
        return false;
    }
    for (int i = 0; i < sg_data.count && i < FEISHU_CAL_MAX_EVENTS; i++) {
        if (sg_data.events[i].mday == mday && sg_data.events[i].mon == mon) {
            return true;
        }
    }
    return false;
}

/** Resolve the focused civil date from local midnight + sg_day_offset. */
static void __focus_ymd(int *year, int *mon, int *mday, int *wday)
{
    POSIX_TM_S tm;
    TIME_T local;
    int tz_sec = 0;

    tal_time_get_time_zone_seconds(&tz_sec);
    local = tal_time_get_posix() + (TIME_T)tz_sec;
    local = local - (local % 86400) + (TIME_T)sg_day_offset * 86400;
    tal_time_get_local_time_custom(local - (TIME_T)tz_sec, &tm);

    if (year) {
        *year = tm.tm_year + 1900;
    }
    if (mon) {
        *mon = tm.tm_mon + 1;
    }
    if (mday) {
        *mday = tm.tm_mday;
    }
    if (wday) {
        *wday = tm.tm_wday;
    }
}

static void __style_day(lv_obj_t *cell, bool is_focus, bool is_today, bool has_event)
{
    /* Always paint an opaque fill. Transparent cells leave previous glyphs on
     * the ST7305 mono panel (looked like garbled "n"/junk on days 1–5). */
    if (is_focus) {
        lv_obj_set_style_bg_color(cell, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(cell, lv_color_white(), 0);
        lv_obj_set_style_border_width(cell, 0, 0);
    } else {
        lv_obj_set_style_bg_color(cell, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(cell, lv_color_black(), 0);
        /* Today keeps an outline when focus has moved; event days too. */
        lv_obj_set_style_border_width(cell, (is_today || has_event) ? 2 : 0, 0);
    }
}

static void __render_month(void)
{
    POSIX_TM_S tm;
    int year, mon, today, days, first_wday;
    int focus_mon = 0, focus_mday = 0;

    if (!sg_root) {
        return;
    }

    if (OPRT_OK != tal_time_check_time_sync()) {
        lv_label_set_text(sg_title, bmo_tr(BMO_STR_CAL_WAIT_SYNC));
        lv_label_set_text(sg_today, "");
        for (int i = 0; i < CAL_CELLS; i++) {
            lv_label_set_text(sg_cell[i], "");
            __style_day(sg_cell[i], false, false, false);
            lv_obj_clear_flag(sg_cell[i], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    tal_time_get_local_time_custom(0, &tm);
    year  = tm.tm_year + 1900;
    mon   = tm.tm_mon + 1;
    today = tm.tm_mday;
    days  = __days_in_month(year, mon);
    /* Weekday of the 1st, derived from today's weekday. */
    first_wday = (((tm.tm_wday - (today - 1)) % 7) + 7) % 7;

    __focus_ymd(NULL, &focus_mon, &focus_mday, NULL);

    if (bmo_lang_get() == BMO_LANG_EN) {
        lv_label_set_text_fmt(sg_title, "%d/%02d", year, mon);
    } else {
        lv_label_set_text_fmt(sg_title, "%d年%d月", year, mon);
    }
    if (sg_browse) {
        int fw = 0;
        __focus_ymd(NULL, NULL, NULL, &fw);
        lv_label_set_text_fmt(sg_today, bmo_tr(BMO_STR_CAL_PICK_DAY), focus_mon, focus_mday, bmo_tr_weekday(fw % 7));
    } else {
        lv_label_set_text_fmt(sg_today, bmo_tr(BMO_STR_CAL_TODAY_LINE), mon, today, bmo_tr_weekday(tm.tm_wday % 7));
    }

    for (int i = 0; i < CAL_CELLS; i++) {
        int day = i - first_wday + 1;

        if (day < 1 || day > days) {
            /* Keep the cell visible but blank + opaque so mono pixels clear. */
            lv_label_set_text(sg_cell[i], "");
            __style_day(sg_cell[i], false, false, false);
            lv_obj_clear_flag(sg_cell[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_label_set_text_fmt(sg_cell[i], "%d", day);
        __style_day(sg_cell[i], (mon == focus_mon && day == focus_mday), day == today,
                    __day_has_event(mon, day));
        lv_obj_clear_flag(sg_cell[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void __render_tasks(void)
{
    int focus_mon = 0, focus_mday = 0, shown = 0;

    if (!sg_root) {
        return;
    }

    for (int i = 0; i < TASK_MAX_LINES; i++) {
        lv_obj_add_flag(sg_task[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (!sg_data.valid) {
        lv_obj_clear_flag(sg_task_note, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(sg_task_note, bmo_tr(BMO_STR_CAL_NO_DATA));
        lv_label_set_text(sg_task_hdr, bmo_tr(BMO_STR_CAL_EVENTS));
        return;
    }

    if (OPRT_OK == tal_time_check_time_sync()) {
        __focus_ymd(NULL, &focus_mon, &focus_mday, NULL);
    }

    if (sg_day_offset == 0) {
        lv_label_set_text(sg_task_hdr, bmo_tr(BMO_STR_CAL_TODAY_EVENTS));
    } else if (bmo_lang_get() == BMO_LANG_EN) {
        lv_label_set_text_fmt(sg_task_hdr, "%d/%d", focus_mon, focus_mday);
    } else {
        lv_label_set_text_fmt(sg_task_hdr, "%d月%d日", focus_mon, focus_mday);
    }

    for (int i = 0; i < sg_data.count && i < FEISHU_CAL_MAX_EVENTS && shown < TASK_MAX_LINES; i++) {
        const FEISHU_CAL_EVENT_T *ev = &sg_data.events[i];

        /* mday == 0 means the date was not parseable, so keep the event visible
         * only while looking at today — otherwise it would spam every day. */
        if (ev->mday == 0) {
            if (sg_day_offset != 0) {
                continue;
            }
        } else if (focus_mday && (ev->mday != focus_mday || ev->mon != focus_mon)) {
            continue;
        }
        lv_label_set_text_fmt(sg_task[shown], "%s %s", ev->time_str, ev->title);
        lv_obj_clear_flag(sg_task[shown], LV_OBJ_FLAG_HIDDEN);
        shown++;
    }

    if (shown == 0) {
        lv_obj_clear_flag(sg_task_note, LV_OBJ_FLAG_HIDDEN);
        if (sg_day_offset == 0) {
            lv_label_set_text(sg_task_note, bmo_tr(BMO_STR_CAL_NONE_TODAY));
        } else {
            lv_label_set_text(sg_task_note, bmo_tr(BMO_STR_CAL_NONE_DAY));
        }
    } else {
        lv_obj_add_flag(sg_task_note, LV_OBJ_FLAG_HIDDEN);
    }
}

static void __tick_cb(lv_timer_t *timer)
{
    (void)timer;
    /* Midnight rolled past while we were looking ahead: clamp and redraw. */
    if (sg_day_offset < 0) {
        sg_day_offset = 0;
    }
    if (sg_day_offset >= CAL_BROWSE_DAYS) {
        sg_day_offset = CAL_BROWSE_DAYS - 1;
    }
    __render_month();
    __render_tasks();
}

static lv_obj_t *__label(lv_obj_t *parent, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_set_style_bg_color(label, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_style_border_width(label, 0, 0);
    lv_label_set_text(label, "");

    return label;
}

static void __enter_browse(void)
{
    sg_browse = true;
    sg_day_offset = 0;
    if (sg_hint) {
        lv_label_set_text(sg_hint, bmo_tr(BMO_STR_CAL_HINT_BROWSE));
    }
    __render_month();
    __render_tasks();
    PR_NOTICE("[cal] browse enter");
}

static void __exit_browse(void)
{
    if (!sg_browse) {
        return;
    }
    sg_browse = false;
    sg_day_offset = 0;
    if (sg_hint) {
        lv_label_set_text(sg_hint, bmo_tr(BMO_STR_CAL_HINT));
    }
    __render_month();
    __render_tasks();
    PR_NOTICE("[cal] browse exit");
}

static void __move_focus(int delta)
{
    int next = sg_day_offset + delta;

    if (next < 0) {
        next = 0;
    }
    if (next >= CAL_BROWSE_DAYS) {
        next = CAL_BROWSE_DAYS - 1;
    }
    if (next == sg_day_offset) {
        return;
    }
    sg_day_offset = next;
    __render_month();
    __render_tasks();
}

void calendar_page_create(lv_obj_t *parent)
{
    lv_font_t *text_font = ai_ui_get_text_font();

    sg_browse = false;
    sg_day_offset = 0;
    memset(&sg_data, 0, sizeof(sg_data));

    sg_root = lv_obj_create(parent);
    lv_obj_set_size(sg_root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(sg_root, lv_color_white(), 0);
    lv_obj_set_style_border_width(sg_root, 0, 0);
    lv_obj_set_style_pad_all(sg_root, 0, 0);
    lv_obj_clear_flag(sg_root, LV_OBJ_FLAG_SCROLLABLE);

    sg_title = __label(sg_root, text_font);
    lv_obj_set_pos(sg_title, CAL_X0, 6);

    sg_today = __label(sg_root, text_font);
    lv_obj_align(sg_today, LV_ALIGN_TOP_RIGHT, -8, 6);

    for (int c = 0; c < CAL_COLS; c++) {
        sg_wday[c] = __label(sg_root, text_font);
        lv_obj_set_size(sg_wday[c], CAL_DAY_W, LV_SIZE_CONTENT);
        lv_obj_set_style_text_align(sg_wday[c], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_text(sg_wday[c], bmo_tr_weekday(c));
        lv_obj_set_pos(sg_wday[c], CAL_X0 + c * CAL_CELL_W + 2, CAL_HDR_Y);
    }

    /* Montserrat renders the digits tighter than the CJK face, which matters in
     * a 26 px cell. */
    for (int i = 0; i < CAL_CELLS; i++) {
        int row = i / CAL_COLS;
        int col = i % CAL_COLS;

        sg_cell[i] = __label(sg_root, &lv_font_montserrat_14);
        lv_obj_set_size(sg_cell[i], CAL_DAY_W, CAL_DAY_H);
        lv_obj_set_style_text_align(sg_cell[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(sg_cell[i], 3, 0);
        lv_obj_set_style_radius(sg_cell[i], 4, 0);
        lv_obj_set_style_border_color(sg_cell[i], lv_color_black(), 0);
        lv_obj_add_flag(sg_cell[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(sg_cell[i], CAL_X0 + col * CAL_CELL_W + 2, CAL_GRID_Y + row * CAL_CELL_H);
    }

    sg_task_hdr = __label(sg_root, text_font);
    lv_label_set_text(sg_task_hdr, bmo_tr(BMO_STR_CAL_TODAY_EVENTS));
    lv_obj_set_pos(sg_task_hdr, TASK_X0, TASK_HDR_Y);

    sg_task_note = __label(sg_root, text_font);
    lv_label_set_text(sg_task_note, bmo_tr(BMO_STR_CAL_LOADING));
    lv_obj_set_pos(sg_task_note, TASK_X0, TASK_Y0);

    for (int i = 0; i < TASK_MAX_LINES; i++) {
        sg_task[i] = __label(sg_root, text_font);
        lv_obj_set_width(sg_task[i], LV_HOR_RES - TASK_X0 - 8);
        lv_label_set_long_mode(sg_task[i], LV_LABEL_LONG_DOT);
        lv_obj_add_flag(sg_task[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(sg_task[i], TASK_X0, TASK_Y0 + i * TASK_LINE_H);
    }

    /* Bottom hint is always visible on this page: how to enter browse, then
     * how to move days / exit once inside. Toast still confirms exit. */
    sg_hint = __label(sg_root, text_font);
    lv_label_set_text(sg_hint, bmo_tr(BMO_STR_CAL_HINT));
    lv_obj_set_style_text_align(sg_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(sg_hint, LV_HOR_RES - 16);
    lv_obj_align(sg_hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    __render_month();

    sg_tick = lv_timer_create(__tick_cb, CAL_TICK_MS, NULL);
}

void calendar_page_destroy(void)
{
    sg_browse = false;
    sg_day_offset = 0;
    if (sg_tick) {
        lv_timer_del(sg_tick);
        sg_tick = NULL;
    }
    if (sg_root) {
        lv_obj_del(sg_root);
        sg_root = NULL;
    }
    sg_title = NULL;
    sg_today = NULL;
    sg_task_hdr = NULL;
    sg_task_note = NULL;
    sg_hint = NULL;
    for (int i = 0; i < CAL_COLS; i++) {
        sg_wday[i] = NULL;
    }
    for (int i = 0; i < CAL_CELLS; i++) {
        sg_cell[i] = NULL;
    }
    for (int i = 0; i < TASK_MAX_LINES; i++) {
        sg_task[i] = NULL;
    }
}

void calendar_page_on_press(void)
{
    ui_bg_task_request_refresh();
}

void calendar_page_update(const FEISHU_CAL_DATA_T *data)
{
    if (!data) {
        return;
    }
    sg_data = *data;

    __render_month();
    __render_tasks();
}

bool calendar_is_browsing(void)
{
    return sg_browse;
}

/** Runs with the display lock held.
 *  @param toast_out optional; set to a static string to show after unlock.
 *  @return true if the calendar owns the key. */
static bool __handle_key(int btn_idx, const char **toast_out)
{
    if (toast_out) {
        *toast_out = NULL;
    }

    if (!sg_browse) {
        /* Centre key opens the secondary browse layer; green still refreshes. */
        if (btn_idx == BTN_MID) {
            /* Hint text already explains browse mode; skip toast so it does not
             * overlap the bottom hint (looked like mixed CN/EN garbage). */
            __enter_browse();
            return true;
        }
        return false;
    }

    /* Triangle backs out of browse without leaving the calendar page. */
    if (btn_idx == BTN_TRI) {
        __exit_browse();
        if (toast_out) {
            *toast_out = bmo_tr(BMO_STR_EXIT_BROWSE);
        }
        return true;
    }

    if (btn_idx == BTN_MID || btn_idx == BTN_GREEN) {
        calendar_page_on_press();
        if (toast_out) {
            *toast_out = bmo_tr(BMO_STR_REFRESHING);
        }
        return true;
    }

    if (btn_idx == BTN_LEFT) {
        __move_focus(-1);
        return true;
    }
    if (btn_idx == BTN_RIGHT) {
        __move_focus(+1);
        return true;
    }
    /* UP/DOWN stay volume while browsing — only left/right move the day. */
    return false;
}

bool calendar_btn_event(int btn_idx, bool pressed)
{
    bool consumed;
    const char *toast = NULL;

    if (page_mgr_get_current() != PAGE_IDX_CALENDAR) {
        return false;
    }
    if (!pressed) {
        return false;
    }

    /* Toast locks the display itself — never call it while we hold the lock. */
    lv_vendor_disp_lock();
    consumed = __handle_key(btn_idx, &toast);
    lv_vendor_disp_unlock();

    if (toast) {
        ui_popup_toast(toast);
    }
    return consumed;
}
