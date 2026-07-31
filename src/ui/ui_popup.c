/**
 * @file ui_popup.c
 * @brief Toast and system info popups on the LVGL top layer.
 */

#include <stdio.h>
#include "tal_api.h"
#include "tal_memory.h"
#include "tal_wifi.h"
#include "lvgl.h"
#include "lv_vendor.h"
#include "ai_ui_icon_font.h"
#include "ui_popup.h"

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "1.0.0"
#endif

#define TOAST_HOLD_MS   1500
#define SYSINFO_HOLD_MS 10000

static lv_obj_t *sg_toast = NULL;
static lv_timer_t *sg_toast_timer = NULL;
static lv_obj_t *sg_sysinfo = NULL;
static lv_timer_t *sg_sysinfo_timer = NULL;

/* Boxes are plain white with a black outline: the panel is a 1-bit reflective
 * LCD, so anything relying on shades of grey disappears. */
static lv_obj_t *__box_create(lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *box = lv_obj_create(lv_layer_top());

    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_black(), 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 4, 0);
    lv_obj_set_style_pad_all(box, 8, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    return box;
}

static lv_obj_t *__label_create(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_style_text_font(label, ai_ui_get_text_font(), 0);
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_label_set_text(label, text);

    return label;
}

static void __toast_close_cb(lv_timer_t *timer)
{
    (void)timer;
    if (sg_toast) {
        lv_obj_del(sg_toast);
        sg_toast = NULL;
    }
    sg_toast_timer = NULL;
}

void ui_popup_toast(const char *msg)
{
    if (!msg) {
        return;
    }

    lv_vendor_disp_lock();

    if (sg_toast_timer) {
        lv_timer_del(sg_toast_timer);
        sg_toast_timer = NULL;
    }
    if (sg_toast) {
        lv_obj_del(sg_toast);
        sg_toast = NULL;
    }

    sg_toast = __box_create(LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_center(__label_create(sg_toast, msg));
    lv_obj_align(sg_toast, LV_ALIGN_BOTTOM_MID, 0, -16);

    sg_toast_timer = lv_timer_create(__toast_close_cb, TOAST_HOLD_MS, NULL);
    lv_timer_set_repeat_count(sg_toast_timer, 1);

    lv_vendor_disp_unlock();
}

static void __sysinfo_close(void)
{
    if (sg_sysinfo_timer) {
        lv_timer_del(sg_sysinfo_timer);
        sg_sysinfo_timer = NULL;
    }
    if (sg_sysinfo) {
        lv_obj_del(sg_sysinfo);
        sg_sysinfo = NULL;
    }
}

static void __sysinfo_close_cb(lv_timer_t *timer)
{
    (void)timer;
    if (sg_sysinfo) {
        lv_obj_del(sg_sysinfo);
        sg_sysinfo = NULL;
    }
    sg_sysinfo_timer = NULL;
}

static void __sysinfo_text(char *buf, size_t len)
{
    NW_IP_S ip = {0};
    int8_t rssi = 0;
    const char *ip_str = "0.0.0.0";

    if (OPRT_OK == tal_wifi_get_ip(WF_STATION, &ip)) {
#ifdef nwipstr
        ip_str = ip.nwipstr;
#else
        ip_str = ip.ip;
#endif
    }
    if (OPRT_OK != tal_wifi_station_get_conn_ap_rssi(&rssi)) {
        rssi = 0;
    }

    snprintf(buf, len, "IP    %s\nWiFi  %d dBm\nHeap  %d KB\nVer   %s", ip_str, (int)rssi,
             tal_system_get_free_heap_size() / 1024, PROJECT_VERSION);
}

void ui_popup_sysinfo_toggle(void)
{
    char text[128];
    lv_obj_t *title, *body;

    /* Queried outside the display lock: these can block on the network stack. */
    __sysinfo_text(text, sizeof(text));

    lv_vendor_disp_lock();

    if (sg_sysinfo) {
        __sysinfo_close();
        lv_vendor_disp_unlock();
        return;
    }

    sg_sysinfo = __box_create(300, 170);
    lv_obj_center(sg_sysinfo);

    title = __label_create(sg_sysinfo, "SYSTEM");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    body = __label_create(sg_sysinfo, text);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 36);

    sg_sysinfo_timer = lv_timer_create(__sysinfo_close_cb, SYSINFO_HOLD_MS, NULL);
    lv_timer_set_repeat_count(sg_sysinfo_timer, 1);

    lv_vendor_disp_unlock();
}
