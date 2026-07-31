/**
 * @file ui_page_mgr.c
 * @brief Multi-page navigation for desktop avatar.
 */

#include "tal_api.h"
#include "lvgl.h"
#include "lv_vendor.h"
#include "ui_page_mgr.h"
#include "ui_avatar.h"
#include "ui_clock.h"
#include "ui_weather.h"
#include "ui_calendar.h"
#include "ui_bg_task.h"
#include <string.h>
#include <ctype.h>

static UI_PAGE_T sg_pages[] = {
    {"avatar",   avatar_page_create,   avatar_page_destroy,   NULL},
    {"clock",    clock_page_create,    clock_page_destroy,    NULL},
    {"weather",  weather_page_create,  weather_page_destroy,  weather_page_on_press},
    {"calendar", calendar_page_create, calendar_page_destroy, calendar_page_on_press},
};

static int sg_page_cnt = (int)(sizeof(sg_pages) / sizeof(sg_pages[0]));
static int sg_cur_idx  = 0;

static void __show_page(int idx)
{
    if (idx < 0 || idx >= sg_page_cnt) {
        return;
    }

    lv_vendor_disp_lock();
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    if (sg_cur_idx >= 0 && sg_cur_idx < sg_page_cnt && sg_pages[sg_cur_idx].destroy) {
        sg_pages[sg_cur_idx].destroy();
    }

    sg_cur_idx = idx;
    if (sg_pages[sg_cur_idx].create) {
        sg_pages[sg_cur_idx].create(scr);
    }
    ui_bg_task_push_cached_pages_unlocked();
    lv_vendor_disp_unlock();

    PR_NOTICE("[page] -> %s (%d)", sg_pages[sg_cur_idx].name, sg_cur_idx);
}

void page_mgr_init(void)
{
    sg_cur_idx = 0;
    __show_page(0);
}

void page_mgr_next(void)
{
    __show_page((sg_cur_idx + 1) % sg_page_cnt);
}

void page_mgr_prev(void)
{
    __show_page((sg_cur_idx - 1 + sg_page_cnt) % sg_page_cnt);
}

void page_mgr_goto(int idx)
{
    __show_page(idx);
}

void page_mgr_press(void)
{
    if (sg_cur_idx >= 0 && sg_cur_idx < sg_page_cnt && sg_pages[sg_cur_idx].on_press) {
        sg_pages[sg_cur_idx].on_press();
    }
}

int page_mgr_get_current(void)
{
    return sg_cur_idx;
}

static int __strcasecmp_local(const char *a, const char *b)
{
    if (!a || !b) {
        return (a == b) ? 0 : 1;
    }
    while (*a && *b) {
        int ca = (int)tolower((unsigned char)*a);
        int cb = (int)tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    return (int)tolower((unsigned char)*a) - (int)tolower((unsigned char)*b);
}

int page_mgr_name_to_idx(const char *page)
{
    if (!page) {
        return -1;
    }
    if (0 == __strcasecmp_local(page, "avatar") || 0 == __strcasecmp_local(page, "face") ||
        0 == strcmp(page, "表情") || 0 == strcmp(page, "表情脸")) {
        return 0;
    }
    if (0 == __strcasecmp_local(page, "clock") || 0 == __strcasecmp_local(page, "time") ||
        0 == strcmp(page, "时钟") || 0 == strcmp(page, "时间")) {
        return 1;
    }
    if (0 == __strcasecmp_local(page, "weather") || 0 == strcmp(page, "天气")) {
        return 2;
    }
    if (0 == __strcasecmp_local(page, "calendar") || 0 == strcmp(page, "日历") ||
        0 == strcmp(page, "日程")) {
        return 3;
    }
    return -1;
}

static bool __text_has_nav_intent(const char *text)
{
    static const char *markers[] = {
        "切换", "切到", "切回", "打开", "显示", "回到", "返回", "看看", "页面", "页",
    };

    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
        if (strstr(text, markers[i]) != NULL) {
            return true;
        }
    }
    return false;
}

bool page_mgr_try_asr_navigate(const char *text)
{
    int idx = -1;

    if (!text || !text[0]) {
        return false;
    }
    if (!__text_has_nav_intent(text)) {
        return false;
    }

    if (strstr(text, "表情") != NULL) {
        idx = 0;
    } else if (strstr(text, "时钟") != NULL || strstr(text, "时间") != NULL) {
        idx = 1;
    } else if (strstr(text, "天气") != NULL) {
        idx = 2;
    } else if (strstr(text, "日历") != NULL || strstr(text, "日程") != NULL) {
        idx = 3;
    } else {
        return false;
    }

    page_mgr_goto(idx);
    PR_NOTICE("[page] ASR navigate -> %s (%d)", sg_pages[idx].name, idx);
    return true;
}
