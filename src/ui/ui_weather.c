/**
 * @file ui_weather.c
 * @brief Weather page using Tuya cloud weather service.
 */

#include "tal_api.h"
#include "lvgl.h"
#include "ai_ui_icon_font.h"
#include "ui_weather.h"
#include "ui_bg_task.h"

static lv_obj_t *sg_root         = NULL;
static lv_obj_t *sg_title        = NULL;
static lv_obj_t *sg_temp_label   = NULL;
static lv_obj_t *sg_detail_label = NULL;
static lv_obj_t *sg_fc_labels[3] = {NULL};

void weather_page_create(lv_obj_t *parent)
{
    lv_font_t *text_font = ai_ui_get_text_font();

    sg_root = lv_obj_create(parent);
    lv_obj_set_size(sg_root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(sg_root, lv_color_white(), 0);
    lv_obj_set_style_border_width(sg_root, 0, 0);
    lv_obj_set_style_pad_all(sg_root, 8, 0);
    lv_obj_clear_flag(sg_root, LV_OBJ_FLAG_SCROLLABLE);

    sg_title = lv_label_create(sg_root);
    lv_obj_set_style_text_font(sg_title, text_font, 0);
    lv_obj_set_style_text_color(sg_title, lv_color_black(), 0);
    lv_label_set_text(sg_title, "天气");
    lv_obj_align(sg_title, LV_ALIGN_TOP_MID, 0, 8);

    sg_temp_label = lv_label_create(sg_root);
    lv_obj_set_style_text_font(sg_temp_label, text_font, 0);
    lv_obj_set_style_text_color(sg_temp_label, lv_color_black(), 0);
    lv_label_set_text(sg_temp_label, "-- C");
    lv_obj_align(sg_temp_label, LV_ALIGN_TOP_MID, 0, 48);

    sg_detail_label = lv_label_create(sg_root);
    lv_obj_set_style_text_font(sg_detail_label, text_font, 0);
    lv_obj_set_style_text_color(sg_detail_label, lv_color_black(), 0);
    lv_label_set_text(sg_detail_label, "加载中...");
    lv_obj_align(sg_detail_label, LV_ALIGN_TOP_MID, 0, 100);

    for (int i = 0; i < 3; i++) {
        sg_fc_labels[i] = lv_label_create(sg_root);
        lv_obj_set_style_text_font(sg_fc_labels[i], text_font, 0);
        lv_obj_set_style_text_color(sg_fc_labels[i], lv_color_black(), 0);
        lv_label_set_text(sg_fc_labels[i], "--");
        lv_obj_align(sg_fc_labels[i], LV_ALIGN_TOP_LEFT, 8, 160 + i * 28);
    }
}

void weather_page_destroy(void)
{
    if (sg_root) {
        lv_obj_del(sg_root);
        sg_root = NULL;
    }
    sg_title = NULL;
    sg_temp_label = NULL;
    sg_detail_label = NULL;
    for (int i = 0; i < 3; i++) {
        sg_fc_labels[i] = NULL;
    }
}

void weather_page_on_press(void)
{
    ui_bg_task_request_weather_refresh();
}

void weather_page_update(const UI_WEATHER_DATA_T *data)
{
    if (!sg_root || !data) {
        return;
    }

    if (!data->valid) {
        lv_label_set_text(sg_temp_label, "-- C");
        lv_label_set_text(sg_detail_label, "暂无数据");
        for (int i = 0; i < 3; i++) {
            lv_label_set_text(sg_fc_labels[i], "--");
        }
        return;
    }

    lv_label_set_text_fmt(sg_temp_label, "%d C", data->temp);
    lv_label_set_text_fmt(sg_detail_label, "%s  湿度%d%%  %d/%dC", data->condition, data->humi, data->lo, data->hi);
    for (int i = 0; i < 3; i++) {
        lv_label_set_text(sg_fc_labels[i], data->forecast[i]);
    }
}
