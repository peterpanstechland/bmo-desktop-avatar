/**
 * @file ui_clock.c
 * @brief Clock page using cloud-synced local time.
 */

#include "tal_api.h"
#include "tal_time_service.h"
#include "lvgl.h"
#include "ai_ui_icon_font.h"
#include "ui_clock.h"

static lv_obj_t *sg_root       = NULL;
static lv_obj_t *sg_time_label = NULL;
static lv_obj_t *sg_date_label = NULL;
static lv_timer_t *sg_timer    = NULL;

static const char *WEEK_CN[] = {"日", "一", "二", "三", "四", "五", "六"};

static void __clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!sg_time_label) {
        return;
    }

    if (OPRT_OK != tal_time_check_time_sync()) {
        lv_label_set_text(sg_time_label, "--:--");
        lv_label_set_text(sg_date_label, "等待时间同步...");
        return;
    }

    POSIX_TM_S tm;
    tal_time_get_local_time_custom(0, &tm);
    lv_label_set_text_fmt(sg_time_label, "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text_fmt(sg_date_label, "%04d-%02d-%02d 星期%s", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                         WEEK_CN[tm.tm_wday % 7]);
}

void clock_page_create(lv_obj_t *parent)
{
    lv_font_t *text_font = ai_ui_get_text_font();

    sg_root = lv_obj_create(parent);
    lv_obj_set_size(sg_root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(sg_root, lv_color_white(), 0);
    lv_obj_set_style_border_width(sg_root, 0, 0);
    lv_obj_set_style_pad_all(sg_root, 0, 0);
    lv_obj_clear_flag(sg_root, LV_OBJ_FLAG_SCROLLABLE);

    sg_time_label = lv_label_create(sg_root);
    lv_obj_set_style_text_font(sg_time_label, text_font, 0);
    lv_obj_set_style_text_color(sg_time_label, lv_color_black(), 0);
    lv_label_set_text(sg_time_label, "--:--");
    lv_obj_align(sg_time_label, LV_ALIGN_CENTER, 0, -40);

    sg_date_label = lv_label_create(sg_root);
    lv_obj_set_style_text_font(sg_date_label, text_font, 0);
    lv_obj_set_style_text_color(sg_date_label, lv_color_black(), 0);
    lv_label_set_text(sg_date_label, "");
    lv_obj_align(sg_date_label, LV_ALIGN_CENTER, 0, 20);

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
    sg_time_label = NULL;
    sg_date_label = NULL;
}
