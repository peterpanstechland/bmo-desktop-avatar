/**
 * @file ui_bg_task.c
 * @brief Background refresh for weather and Feishu calendar.
 */

#include "tal_api.h"
#include "lv_vendor.h"
#include "tuya_weather.h"
#include "ui_bg_task.h"
#include "ui_weather.h"
#include "ui_calendar.h"
#include "ui_page_mgr.h"
#include "feishu_cal.h"

#define BG_STACK_SIZE         (8 * 1024)
#define BG_REFRESH_INTERVAL_S (30 * 60)
#define BG_RETRY_INTERVAL_S   60

static THREAD_HANDLE sg_bg_thread = NULL;
static SEM_HANDLE    sg_bg_sem    = NULL;
static volatile bool sg_weather_refresh_now = false;

static UI_WEATHER_DATA_T  sg_weather_cache;
static FEISHU_CAL_DATA_T  sg_calendar_cache;

static const char *__weather_text(int code)
{
    switch (code) {
    case TW_WEATHER_SUNNY:
    case TW_WEATHER_CLEAR:
    case TW_WEATHER_MOSTLY_CLEAR:
        return "晴";
    case TW_WEATHER_PARTLY_CLOUDY:
    case TW_WEATHER_CLOUDY:
        return "多云";
    case TW_WEATHER_OVERCAST:
        return "阴";
    case TW_WEATHER_RAIN:
    case TW_WEATHER_LIGHT_RAIN:
    case TW_WEATHER_MODERATE_RAIN:
    case TW_WEATHER_HEAVY_RAIN:
    case TW_WEATHER_SHOWER:
        return "雨";
    case TW_WEATHER_SNOW:
    case TW_WEATHER_LIGHT_SNOW:
    case TW_WEATHER_HEAVY_SNOW:
        return "雪";
    case TW_WEATHER_FOG:
    case TW_WEATHER_HAZE:
        return "雾";
    default:
        return "未知";
    }
}

static void __refresh_weather(void)
{
    if (!tuya_weather_allow_update()) {
        PR_DEBUG("[bg] weather update not allowed yet");
        return;
    }

    UI_WEATHER_DATA_T data = {0};
    WEATHER_CURRENT_CONDITIONS_T cur = {0};
    WEATHER_FORECAST_CONDITIONS_T fc = {0};

    if (tuya_weather_get_current_conditions(&cur) != OPRT_OK) {
        return;
    }

    data.valid = true;
    data.temp    = cur.temp;
    data.humi    = cur.humi;
    snprintf(data.condition, sizeof(data.condition), "%s", __weather_text(cur.weather));
    tuya_weather_get_today_high_low_temp(&data.hi, &data.lo);
    if (tuya_weather_get_forecast_conditions(3, &fc) == OPRT_OK) {
        for (int i = 0; i < 3; i++) {
            snprintf(data.forecast[i], sizeof(data.forecast[i]), "D+%d %s %dC", i + 1, __weather_text(fc.weather_v[i]),
                     fc.temp_v[i]);
        }
    }

    sg_weather_cache = data;

    if (page_mgr_get_current() == 2) {
        lv_vendor_disp_lock();
        weather_page_update(&sg_weather_cache);
        lv_vendor_disp_unlock();
    }
    PR_NOTICE("[bg] weather refreshed: %dC %s", data.temp, data.condition);
}

static void __refresh_calendar(void)
{
    FEISHU_CAL_DATA_T data = {0};
    if (feishu_cal_fetch(&data) != OPRT_OK) {
        return;
    }
    sg_calendar_cache = data;

    if (page_mgr_get_current() == 3) {
        lv_vendor_disp_lock();
        calendar_page_update(&sg_calendar_cache);
        lv_vendor_disp_unlock();
    }
    PR_NOTICE("[bg] calendar refreshed, count=%d", data.count);
}

static void __bg_thread(void *arg)
{
    (void)arg;
    uint32_t wait_ms = BG_RETRY_INTERVAL_S * 1000U;
    bool first_run   = true;

    while (1) {
        tal_semaphore_wait(sg_bg_sem, wait_ms);

        if (sg_weather_refresh_now) {
            sg_weather_refresh_now = false;
        }
        __refresh_weather();

        if (feishu_cal_take_refresh_request() || first_run) {
            __refresh_calendar();
            first_run = false;
        }

        wait_ms = BG_REFRESH_INTERVAL_S * 1000U;
    }
}

OPERATE_RET ui_bg_task_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(tal_semaphore_create_init(&sg_bg_sem, 0, 1));
    feishu_cal_bind_refresh_sem(sg_bg_sem);

    THREAD_CFG_T cfg = {
        .stackDepth = BG_STACK_SIZE,
        .priority   = THREAD_PRIO_2,
        .thrdname   = "ui_bg",
    };
    return tal_thread_create_and_start(&sg_bg_thread, NULL, NULL, __bg_thread, NULL, &cfg);
}

void ui_bg_task_request_weather_refresh(void)
{
    sg_weather_refresh_now = true;
    if (sg_bg_sem) {
        tal_semaphore_post(sg_bg_sem);
    }
}

void ui_bg_task_request_full_refresh(void)
{
    sg_weather_refresh_now = true;
    feishu_cal_request_refresh();
    if (sg_bg_sem) {
        tal_semaphore_post(sg_bg_sem);
    }
}

void ui_bg_task_push_cached_pages_unlocked(void)
{
    if (page_mgr_get_current() == 2) {
        weather_page_update(&sg_weather_cache);
    } else if (page_mgr_get_current() == 3) {
        calendar_page_update(&sg_calendar_cache);
    }
}

void ui_bg_task_push_cached_pages(void)
{
    lv_vendor_disp_lock();
    ui_bg_task_push_cached_pages_unlocked();
    lv_vendor_disp_unlock();
}
