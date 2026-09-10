/**
 * @file ui_bg_task.c
 * @brief Background refresh for weather and Feishu calendar.
 */

#include "tal_api.h"
#include "tal_time_service.h"
#include "lv_vendor.h"
#include "tuya_weather.h"
#include "ui_bg_task.h"
#include "ui_weather.h"
#include "ui_calendar.h"
#include "ui_rss.h"
#include "ui_page_mgr.h"
#include "ui_i18n.h"
#include "feishu_cal.h"
#include "rss_feed.h"

#include "tal_wifi.h"

#include <string.h>

#define BG_STACK_SIZE         (12 * 1024)
#define BG_REFRESH_INTERVAL_S (30 * 60)
#define BG_RETRY_INTERVAL_S   60

static THREAD_HANDLE sg_bg_thread = NULL;
static SEM_HANDLE    sg_bg_sem    = NULL;

static UI_WEATHER_DATA_T  sg_weather_cache;
static FEISHU_CAL_DATA_T  sg_calendar_cache;
static RSS_FEED_DATA_T   *sg_rss_cache; /* PSRAM — ~20KB+; must not live in SRAM BSS */

/** Station has a non-zero IP — SoftAP-only / pre-join has no outbound DNS. */
static bool __sta_has_ip(void)
{
    NW_IP_S ip = {0};
    const char *ip_str = NULL;

    if (OPRT_OK != tal_wifi_get_ip(WF_STATION, &ip)) {
        return false;
    }
#ifdef nwipstr
    ip_str = ip.nwipstr;
#else
    ip_str = ip.ip;
#endif
    if (!ip_str || !ip_str[0] || 0 == strcmp(ip_str, "0.0.0.0")) {
        return false;
    }
    return true;
}

static const char *__weather_text(int code)
{
    bool en = (bmo_lang_get() == BMO_LANG_EN);

    switch (code) {
    case TW_WEATHER_SUNNY:
    case TW_WEATHER_CLEAR:
    case TW_WEATHER_MOSTLY_CLEAR:
        return en ? "Sunny" : "晴";
    case TW_WEATHER_PARTLY_CLOUDY:
    case TW_WEATHER_CLOUDY:
        return en ? "Cloudy" : "多云";
    case TW_WEATHER_OVERCAST:
        return en ? "Overcast" : "阴";
    case TW_WEATHER_RAIN:
    case TW_WEATHER_LIGHT_RAIN:
    case TW_WEATHER_MODERATE_RAIN:
    case TW_WEATHER_HEAVY_RAIN:
    case TW_WEATHER_SHOWER:
        return en ? "Rain" : "雨";
    case TW_WEATHER_SNOW:
    case TW_WEATHER_LIGHT_SNOW:
    case TW_WEATHER_HEAVY_SNOW:
        return en ? "Snow" : "雪";
    case TW_WEATHER_FOG:
    case TW_WEATHER_HAZE:
        return en ? "Fog" : "雾";
    default:
        return en ? "Unknown" : "未知";
    }
}

/* Near days use Tomorrow/Day after; further out use weekday names. */
static const char *__day_label(int day_offset)
{
    static const char *const wday_zh[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    static const char *const wday_en[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    POSIX_TM_S tm;
    bool en = (bmo_lang_get() == BMO_LANG_EN);

    if (day_offset == 1) {
        return en ? "Tomorrow" : "明天";
    }
    if (day_offset == 2) {
        return en ? "Day+2" : "后天";
    }

    tal_time_get_local_time_custom(tal_time_get_posix() + (TIME_T)day_offset * 86400, &tm);
    return en ? wday_en[tm.tm_wday % 7] : wday_zh[tm.tm_wday % 7];
}

/*
 * Index 0 of every forecast endpoint is today, not tomorrow — see
 * tuya_weather_get_today_high_low_temp(), which asks for one day and then reads
 * w.thigh.0. So the three future days live at 1..3 and the request has to cover
 * FC_DAYS of them.
 *
 * Temperature comes from thigh/tlow rather than the forecast conditions block:
 * w.temp is documented as unsupported in mainland China and comes back as a
 * flat 0, which is what made the rows look empty.
 */
#define FC_DAYS 4

static void __fill_forecast(UI_WEATHER_DATA_T *data)
{
    WEATHER_FORECAST_CONDITIONS_T fc = {0};
    int  hi[FC_DAYS] = {0};
    int  lo[FC_DAYS] = {0};
    bool has_cond, has_temp;

    has_cond = (tuya_weather_get_forecast_conditions(FC_DAYS, &fc) == OPRT_OK);
    has_temp = (tuya_weather_get_forecast_high_low_temp(FC_DAYS, hi, lo) == OPRT_OK);

    PR_NOTICE("[bg] forecast cond=%d temp=%d w=[%d,%d,%d] hi=[%d,%d,%d] lo=[%d,%d,%d]", has_cond, has_temp,
              fc.weather_v[1], fc.weather_v[2], fc.weather_v[3], hi[1], hi[2], hi[3], lo[1], lo[2], lo[3]);

    for (int i = 0; i < 3; i++) {
        char      *slot  = data->forecast[i];
        size_t     size  = sizeof(data->forecast[i]);
        int        d     = i + 1;
        const char *label = __day_label(d);

        /* Valid condition codes are 101..146, so a zero means the cloud left
         * that day out of the response. */
        if (!has_cond || fc.weather_v[d] == 0) {
            snprintf(slot, size, "%s  --", label);
        } else if (has_temp && (hi[d] != 0 || lo[d] != 0)) {
            snprintf(slot, size, "%s  %s  %d°/%d°", label, __weather_text(fc.weather_v[d]), hi[d], lo[d]);
        } else {
            snprintf(slot, size, "%s  %s", label, __weather_text(fc.weather_v[d]));
        }
    }
}

/** @return false when the cloud call failed and the cycle should retry soon. */
static bool __refresh_weather(void)
{
    if (!tuya_weather_allow_update()) {
        PR_DEBUG("[bg] weather update not allowed yet");
        return true;
    }

    UI_WEATHER_DATA_T data = {0};
    WEATHER_CURRENT_CONDITIONS_T cur = {0};

    if (tuya_weather_get_current_conditions(&cur) != OPRT_OK) {
        return false;
    }

    data.valid = true;
    data.temp    = cur.temp;
    data.humi    = cur.humi;
    snprintf(data.condition, sizeof(data.condition), "%s", __weather_text(cur.weather));
    tuya_weather_get_today_high_low_temp(&data.hi, &data.lo);
    __fill_forecast(&data);

    sg_weather_cache = data;

    if (page_mgr_get_current() == PAGE_IDX_WEATHER) {
        lv_vendor_disp_lock();
        weather_page_update(&sg_weather_cache);
        lv_vendor_disp_unlock();
    }
    PR_NOTICE("[bg] weather refreshed: %dC %s", data.temp, data.condition);
    return true;
}

/*
 * @return false when the fetch failed for a transient reason (no network yet,
 * clock not synced, HTTP error) so the cycle retries soon. A calendar that is
 * not configured at all counts as done: nothing will change until a rebuild.
 */
static bool __refresh_calendar(void)
{
    FEISHU_CAL_DATA_T data = {0};
    OPERATE_RET rt = feishu_cal_fetch(&data);

    if (rt == OPRT_NOT_SUPPORTED) {
        return true;
    }
    if (rt != OPRT_OK) {
        return false;
    }
    sg_calendar_cache = data;

    if (page_mgr_get_current() == PAGE_IDX_CALENDAR) {
        lv_vendor_disp_lock();
        calendar_page_update(&sg_calendar_cache);
        lv_vendor_disp_unlock();
    }
    PR_NOTICE("[bg] calendar refreshed, count=%d", data.count);
    return true;
}

static bool __refresh_rss(void)
{
    RSS_FEED_DATA_T *data;
    OPERATE_RET rt;

    if (!sg_rss_cache) {
        return false;
    }
    if (!__sta_has_ip()) {
        PR_DEBUG("[bg] rss skip, station has no IP yet");
        return false;
    }

    data = (RSS_FEED_DATA_T *)tal_psram_malloc(sizeof(*data));
    if (!data) {
        return false;
    }
    rt = rss_feed_fetch(data);
    if (rt != OPRT_OK) {
        tal_psram_free(data);
        return false;
    }
    memcpy(sg_rss_cache, data, sizeof(*sg_rss_cache));
    tal_psram_free(data);

    if (page_mgr_get_current() == PAGE_IDX_RSS) {
        lv_vendor_disp_lock();
        rss_page_update(sg_rss_cache);
        lv_vendor_disp_unlock();
    }
    PR_NOTICE("[bg] rss refreshed, count=%d", sg_rss_cache->count);
    return true;
}

/*
 * Every wake-up refreshes weather, calendar and Maker RSS: the 30 min tick, a
 * manual refresh (semaphore), or the 60 s retry after a failed round.
 */
static void __bg_thread(void *arg)
{
    (void)arg;
    uint32_t wait_ms = BG_RETRY_INTERVAL_S * 1000U;

    while (1) {
        bool ok;

        tal_semaphore_wait(sg_bg_sem, wait_ms);

        ok = __refresh_weather();
        ok = __refresh_calendar() && ok;
        ok = __refresh_rss() && ok;

        wait_ms = (ok ? BG_REFRESH_INTERVAL_S : BG_RETRY_INTERVAL_S) * 1000U;
    }
}

OPERATE_RET ui_bg_task_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    sg_rss_cache = (RSS_FEED_DATA_T *)tal_psram_malloc(sizeof(*sg_rss_cache));
    if (!sg_rss_cache) {
        return OPRT_MALLOC_FAILED;
    }
    memset(sg_rss_cache, 0, sizeof(*sg_rss_cache));

    TUYA_CALL_ERR_RETURN(tal_semaphore_create_init(&sg_bg_sem, 0, 1));
    feishu_cal_init();

    THREAD_CFG_T cfg = {
        .stackDepth = BG_STACK_SIZE,
        .priority   = THREAD_PRIO_2,
        .thrdname   = "ui_bg",
    };
    return tal_thread_create_and_start(&sg_bg_thread, NULL, NULL, __bg_thread, NULL, &cfg);
}

void ui_bg_task_request_refresh(void)
{
    if (sg_bg_sem) {
        tal_semaphore_post(sg_bg_sem);
    }
}

void ui_bg_task_push_cached_pages_unlocked(void)
{
    if (page_mgr_get_current() == PAGE_IDX_WEATHER) {
        weather_page_update(&sg_weather_cache);
    } else if (page_mgr_get_current() == PAGE_IDX_CALENDAR) {
        calendar_page_update(&sg_calendar_cache);
    } else if (page_mgr_get_current() == PAGE_IDX_RSS) {
        if (sg_rss_cache) {
            rss_page_update(sg_rss_cache);
        }
    }
}

void ui_bg_task_push_cached_pages(void)
{
    lv_vendor_disp_lock();
    ui_bg_task_push_cached_pages_unlocked();
    lv_vendor_disp_unlock();
}

bool ui_bg_task_copy_rss(RSS_FEED_DATA_T *out)
{
    if (!out || !sg_rss_cache) {
        return false;
    }
    memcpy(out, sg_rss_cache, sizeof(*out));
    return sg_rss_cache->valid && sg_rss_cache->count > 0;
}
