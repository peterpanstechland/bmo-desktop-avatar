/**
 * @file ui_weather.c
 * @brief Weather page using Tuya cloud weather service.
 *
 * The condition icon is drawn from primitives instead of a glyph: the icon font
 * built into this SDK has no weather symbols, and solid shapes survive the
 * panel's black-and-white thresholding better than a scaled-up glyph would.
 */

#include <string.h>
#include "tal_api.h"
#include "lvgl.h"
#include "ai_ui_icon_font.h"
#include "ui_weather.h"
#include "ui_bg_task.h"

#define HDR_Y     8
#define RULE1_Y   50
#define RULE2_Y   174

#define ICON_X 24
#define ICON_Y 70
#define ICON_D 64

#define TEMP_X 104
#define TEMP_Y 68
#define TEMP_W 130
#define TEMP_H 62

#define COND_Y (TEMP_Y + TEMP_H + 4)

#define STAT_X  256
#define STAT_Y  66
#define STAT_LH 34

#define FC_Y  182
#define FC_LH 36

/* Icon geometry, all relative to the ICON_D box. */
#define SUN_CX 32
#define SUN_CY 32
#define SUN_R  13
#define RAY_R0 17
#define RAY_R1 26

typedef enum {
    WX_SUN,
    WX_CLOUD,
    WX_RAIN,
    WX_SNOW,
    WX_THUNDER,
} WX_ICON_E;

static lv_obj_t *sg_root      = NULL;
static lv_obj_t *sg_temp      = NULL;
static lv_obj_t *sg_cond      = NULL;
static lv_obj_t *sg_stat[3]   = {NULL};
static lv_obj_t *sg_fc[3]     = {NULL};

static lv_obj_t *sg_icon_box    = NULL;
static lv_obj_t *sg_sun_core    = NULL;
static lv_obj_t *sg_sun_ray[8]  = {NULL};
static lv_obj_t *sg_cloud[4]    = {NULL}; /* base + three puffs */
static lv_obj_t *sg_rain[3]     = {NULL};
static lv_obj_t *sg_snow[3]     = {NULL};
static lv_obj_t *sg_bolt        = NULL;

/* lv_line keeps the pointer rather than copying, so these outlive the call. */
static lv_point_precise_t sg_ray_pts[8][2];
static lv_point_precise_t sg_rain_pts[3][2];
static lv_point_precise_t sg_bolt_pts[4] = {{36, 44}, {27, 55}, {33, 55}, {24, 64}};

/* cos/sin of k*45 degrees scaled by 1000, so the rays need no float math. */
static const int sg_cos8[8] = {1000, 707, 0, -707, -1000, -707, 0, 707};
static const int sg_sin8[8] = {0, 707, 1000, 707, 0, -707, -1000, -707};

static void __show(lv_obj_t *obj, bool on)
{
    if (!obj) {
        return;
    }
    if (on) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static WX_ICON_E __icon_of(const char *cond)
{
    if (!cond || !cond[0]) {
        return WX_CLOUD;
    }
    /* Checked most specific first: "雷阵雨" must not fall through to rain. */
    if (strstr(cond, "雷")) {
        return WX_THUNDER;
    }
    if (strstr(cond, "雪")) {
        return WX_SNOW;
    }
    if (strstr(cond, "雨")) {
        return WX_RAIN;
    }
    if (strstr(cond, "云") || strstr(cond, "阴") || strstr(cond, "雾") || strstr(cond, "霾")) {
        return WX_CLOUD;
    }
    return WX_SUN;
}

static void __icon_apply(WX_ICON_E icon)
{
    bool sun    = (icon == WX_SUN);
    bool cloud  = (icon != WX_SUN);

    __show(sg_sun_core, sun);
    for (int i = 0; i < 8; i++) {
        __show(sg_sun_ray[i], sun);
    }
    for (int i = 0; i < 4; i++) {
        __show(sg_cloud[i], cloud);
    }
    for (int i = 0; i < 3; i++) {
        __show(sg_rain[i], icon == WX_RAIN);
        __show(sg_snow[i], icon == WX_SNOW);
    }
    __show(sg_bolt, icon == WX_THUNDER);
}

static lv_obj_t *__solid(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h, lv_coord_t radius)
{
    lv_obj_t *obj = lv_obj_create(parent);

    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    return obj;
}

static lv_obj_t *__stroke(lv_obj_t *parent, lv_coord_t width)
{
    lv_obj_t *line = lv_line_create(parent);

    lv_obj_set_pos(line, 0, 0);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_color(line, lv_color_black(), 0);
    lv_obj_set_style_line_rounded(line, true, 0);

    return line;
}

static lv_obj_t *__label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, "");

    return label;
}

static void __icon_create(lv_obj_t *parent)
{
    sg_icon_box = lv_obj_create(parent);
    lv_obj_set_pos(sg_icon_box, ICON_X, ICON_Y);
    lv_obj_set_size(sg_icon_box, ICON_D, ICON_D);
    lv_obj_set_style_bg_opa(sg_icon_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sg_icon_box, 0, 0);
    lv_obj_set_style_pad_all(sg_icon_box, 0, 0);
    lv_obj_clear_flag(sg_icon_box, LV_OBJ_FLAG_SCROLLABLE);

    sg_sun_core = __solid(sg_icon_box, SUN_CX - SUN_R, SUN_CY - SUN_R, SUN_R * 2, SUN_R * 2, LV_RADIUS_CIRCLE);

    for (int i = 0; i < 8; i++) {
        sg_ray_pts[i][0].x = SUN_CX + RAY_R0 * sg_cos8[i] / 1000;
        sg_ray_pts[i][0].y = SUN_CY + RAY_R0 * sg_sin8[i] / 1000;
        sg_ray_pts[i][1].x = SUN_CX + RAY_R1 * sg_cos8[i] / 1000;
        sg_ray_pts[i][1].y = SUN_CY + RAY_R1 * sg_sin8[i] / 1000;

        sg_sun_ray[i] = __stroke(sg_icon_box, 3);
        lv_line_set_points(sg_sun_ray[i], sg_ray_pts[i], 2);
    }

    /* The cloud sits high in the box to leave room underneath for rain, snow
     * and the bolt, which would otherwise be swallowed by it. */
    sg_cloud[0] = __solid(sg_icon_box, 6, 26, 52, 20, 10);
    sg_cloud[1] = __solid(sg_icon_box, 12, 16, 20, 20, LV_RADIUS_CIRCLE);
    sg_cloud[2] = __solid(sg_icon_box, 24, 8, 26, 26, LV_RADIUS_CIRCLE);
    sg_cloud[3] = __solid(sg_icon_box, 40, 18, 18, 18, LV_RADIUS_CIRCLE);

    for (int i = 0; i < 3; i++) {
        sg_rain_pts[i][0].x = 18 + i * 12;
        sg_rain_pts[i][0].y = 50;
        sg_rain_pts[i][1].x = 14 + i * 12;
        sg_rain_pts[i][1].y = 62;

        sg_rain[i] = __stroke(sg_icon_box, 3);
        lv_line_set_points(sg_rain[i], sg_rain_pts[i], 2);

        sg_snow[i] = __solid(sg_icon_box, 15 + i * 12, 52, 7, 7, LV_RADIUS_CIRCLE);
    }

    sg_bolt = __stroke(sg_icon_box, 4);
    lv_line_set_points(sg_bolt, sg_bolt_pts, 4);

    __icon_apply(WX_CLOUD);
}

void weather_page_create(lv_obj_t *parent)
{
    lv_font_t *text_font = ai_ui_get_text_font();
    lv_obj_t *hdr;

    sg_root = lv_obj_create(parent);
    lv_obj_set_size(sg_root, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_bg_color(sg_root, lv_color_white(), 0);
    lv_obj_set_style_border_width(sg_root, 0, 0);
    lv_obj_set_style_pad_all(sg_root, 0, 0);
    lv_obj_clear_flag(sg_root, LV_OBJ_FLAG_SCROLLABLE);

    hdr = __label(sg_root, text_font, lv_color_black());
    lv_label_set_text(hdr, "天气");
    lv_obj_set_pos(hdr, 12, HDR_Y);

    __solid(sg_root, 12, RULE1_Y, LV_HOR_RES - 24, 2, 0);
    __solid(sg_root, 12, RULE2_Y, LV_HOR_RES - 24, 2, 0);

    __icon_create(sg_root);

    /* Reversed card so the reading you actually want is the loudest thing on
     * a panel that has no colour to spend on emphasis. */
    __solid(sg_root, TEMP_X, TEMP_Y, TEMP_W, TEMP_H, 8);
    sg_temp = __label(sg_root, text_font, lv_color_white());
    lv_obj_set_size(sg_temp, TEMP_W, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(sg_temp, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(sg_temp, "--°");
    lv_obj_set_pos(sg_temp, TEMP_X, TEMP_Y + (TEMP_H - 36) / 2);

    sg_cond = __label(sg_root, text_font, lv_color_black());
    lv_obj_set_size(sg_cond, TEMP_W, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(sg_cond, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(sg_cond, "加载中");
    lv_obj_set_pos(sg_cond, TEMP_X, COND_Y);

    for (int i = 0; i < 3; i++) {
        sg_stat[i] = __label(sg_root, text_font, lv_color_black());
        lv_obj_set_pos(sg_stat[i], STAT_X, STAT_Y + i * STAT_LH);
    }
    lv_label_set_text(sg_stat[0], "最高 --");
    lv_label_set_text(sg_stat[1], "最低 --");
    lv_label_set_text(sg_stat[2], "湿度 --");

    for (int i = 0; i < 3; i++) {
        sg_fc[i] = __label(sg_root, text_font, lv_color_black());
        lv_obj_set_width(sg_fc[i], LV_HOR_RES - 24);
        lv_label_set_long_mode(sg_fc[i], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(sg_fc[i], 12, FC_Y + i * FC_LH);
    }
}

void weather_page_destroy(void)
{
    if (sg_root) {
        lv_obj_del(sg_root);
        sg_root = NULL;
    }
    sg_temp = NULL;
    sg_cond = NULL;
    sg_icon_box = NULL;
    sg_sun_core = NULL;
    sg_bolt = NULL;
    for (int i = 0; i < 3; i++) {
        sg_stat[i] = NULL;
        sg_fc[i] = NULL;
        sg_rain[i] = NULL;
        sg_snow[i] = NULL;
    }
    for (int i = 0; i < 4; i++) {
        sg_cloud[i] = NULL;
    }
    for (int i = 0; i < 8; i++) {
        sg_sun_ray[i] = NULL;
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
        lv_label_set_text(sg_temp, "--°");
        lv_label_set_text(sg_cond, "暂无数据");
        lv_label_set_text(sg_stat[0], "最高 --");
        lv_label_set_text(sg_stat[1], "最低 --");
        lv_label_set_text(sg_stat[2], "湿度 --");
        for (int i = 0; i < 3; i++) {
            lv_label_set_text(sg_fc[i], "");
        }
        __icon_apply(WX_CLOUD);
        return;
    }

    lv_label_set_text_fmt(sg_temp, "%d°", data->temp);
    lv_label_set_text(sg_cond, data->condition);
    lv_label_set_text_fmt(sg_stat[0], "最高 %d°", data->hi);
    lv_label_set_text_fmt(sg_stat[1], "最低 %d°", data->lo);
    lv_label_set_text_fmt(sg_stat[2], "湿度 %d%%", data->humi);
    for (int i = 0; i < 3; i++) {
        lv_label_set_text(sg_fc[i], data->forecast[i]);
    }

    __icon_apply(__icon_of(data->condition));
}
