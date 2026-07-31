/**
 * @file ui_calendar.c
 * @brief Month grid with today highlighted, plus today's Feishu agenda.
 *
 * Layout on the 400x300 landscape mono panel: the month sits on the left, the
 * agenda for today on the right. Days that have a synced event are outlined,
 * today is drawn inverted.
 */

#include <stdio.h>
#include "tal_api.h"
#include "tal_time_service.h"
#include "lvgl.h"
#include "ai_ui_icon_font.h"
#include "ui_calendar.h"
#include "ui_bg_task.h"

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

/* Re-render often enough that the highlight follows midnight. */
#define CAL_TICK_MS 60000

static const char *WEEK_CN[] = {"日", "一", "二", "三", "四", "五", "六"};

static lv_obj_t *sg_root = NULL;
static lv_obj_t *sg_title = NULL;
static lv_obj_t *sg_today = NULL;
static lv_obj_t *sg_wday[CAL_COLS] = {NULL};
static lv_obj_t *sg_cell[CAL_CELLS] = {NULL};
static lv_obj_t *sg_task_hdr = NULL;
static lv_obj_t *sg_task[TASK_MAX_LINES] = {NULL};
static lv_obj_t *sg_task_note = NULL;
static lv_timer_t *sg_tick = NULL;

static FEISHU_CAL_DATA_T sg_data;

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

static void __style_day(lv_obj_t *cell, bool is_today, bool has_event)
{
    if (is_today) {
        lv_obj_set_style_bg_color(cell, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(cell, lv_color_white(), 0);
        lv_obj_set_style_border_width(cell, 0, 0);
    } else {
        lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(cell, lv_color_black(), 0);
        lv_obj_set_style_border_width(cell, has_event ? 2 : 0, 0);
    }
}

static void __render_month(void)
{
    POSIX_TM_S tm;
    int year, mon, today, days, first_wday;

    if (!sg_root) {
        return;
    }

    if (OPRT_OK != tal_time_check_time_sync()) {
        lv_label_set_text(sg_title, "等待时间同步...");
        lv_label_set_text(sg_today, "");
        for (int i = 0; i < CAL_CELLS; i++) {
            lv_obj_add_flag(sg_cell[i], LV_OBJ_FLAG_HIDDEN);
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

    lv_label_set_text_fmt(sg_title, "%d年%d月", year, mon);
    lv_label_set_text_fmt(sg_today, "%d月%d日 周%s", mon, today, WEEK_CN[tm.tm_wday % 7]);

    for (int i = 0; i < CAL_CELLS; i++) {
        int day = i - first_wday + 1;

        if (day < 1 || day > days) {
            lv_obj_add_flag(sg_cell[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(sg_cell[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(sg_cell[i], "%d", day);
        __style_day(sg_cell[i], day == today, __day_has_event(mon, day));
    }
}

static void __render_tasks(void)
{
    POSIX_TM_S tm;
    int mon = 0, today = 0, shown = 0;

    if (!sg_root) {
        return;
    }

    for (int i = 0; i < TASK_MAX_LINES; i++) {
        lv_obj_add_flag(sg_task[i], LV_OBJ_FLAG_HIDDEN);
    }

    if (!sg_data.valid) {
        lv_obj_clear_flag(sg_task_note, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(sg_task_note, "暂无数据");
        return;
    }

    if (OPRT_OK == tal_time_check_time_sync()) {
        tal_time_get_local_time_custom(0, &tm);
        mon   = tm.tm_mon + 1;
        today = tm.tm_mday;
    }

    for (int i = 0; i < sg_data.count && i < FEISHU_CAL_MAX_EVENTS && shown < TASK_MAX_LINES; i++) {
        const FEISHU_CAL_EVENT_T *ev = &sg_data.events[i];

        /* mday == 0 means the date was not parseable, so keep the event visible. */
        if (today && ev->mday && (ev->mday != today || ev->mon != mon)) {
            continue;
        }
        lv_label_set_text_fmt(sg_task[shown], "%s %s", ev->time_str, ev->title);
        lv_obj_clear_flag(sg_task[shown], LV_OBJ_FLAG_HIDDEN);
        shown++;
    }

    if (shown == 0) {
        lv_obj_clear_flag(sg_task_note, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(sg_task_note, "今天没有安排 :)");
    } else {
        lv_obj_add_flag(sg_task_note, LV_OBJ_FLAG_HIDDEN);
    }
}

static void __tick_cb(lv_timer_t *timer)
{
    (void)timer;
    __render_month();
    __render_tasks();
}

static lv_obj_t *__label(lv_obj_t *parent, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_label_set_text(label, "");

    return label;
}

void calendar_page_create(lv_obj_t *parent)
{
    lv_font_t *text_font = ai_ui_get_text_font();

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
        lv_label_set_text(sg_wday[c], WEEK_CN[c]);
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
    lv_label_set_text(sg_task_hdr, "今日日程");
    lv_obj_set_pos(sg_task_hdr, TASK_X0, TASK_HDR_Y);

    sg_task_note = __label(sg_root, text_font);
    lv_label_set_text(sg_task_note, "加载中...");
    lv_obj_set_pos(sg_task_note, TASK_X0, TASK_Y0);

    for (int i = 0; i < TASK_MAX_LINES; i++) {
        sg_task[i] = __label(sg_root, text_font);
        lv_obj_set_width(sg_task[i], LV_HOR_RES - TASK_X0 - 8);
        lv_label_set_long_mode(sg_task[i], LV_LABEL_LONG_DOT);
        lv_obj_add_flag(sg_task[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(sg_task[i], TASK_X0, TASK_Y0 + i * TASK_LINE_H);
    }

    __render_month();

    sg_tick = lv_timer_create(__tick_cb, CAL_TICK_MS, NULL);
}

void calendar_page_destroy(void)
{
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
    feishu_cal_request_refresh();
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
