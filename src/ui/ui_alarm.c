/**
 * @file ui_alarm.c
 * @brief One daily alarm + one countdown timer, shared by the clock page and MCP.
 *
 * A 1 Hz software timer compares local wall clock against the armed alarm and
 * counts the timer down. When either fires: toast, play the wakeup alert a few
 * times, jump to the clock page, and keep a "ringing" flag until any key calls
 * ui_alarm_try_dismiss().
 */

#include "ui_alarm.h"
#include "ui_page_mgr.h"
#include "ui_popup.h"
#include "ui_i18n.h"
#include "tal_api.h"
#include "tal_sw_timer.h"
#include "tal_time_service.h"
#include "tal_kv.h"
#include "ai_audio_player.h"

#include <stdio.h>
#include <string.h>

#define ALARM_KV_KEY       "bmo_alarm"
#define ALARM_TICK_MS      1000
#define ALARM_RING_REPEAT  5
#define ALARM_RING_GAP_MS  2500

typedef struct {
    uint8_t on;
    uint8_t hour;
    uint8_t min;
} ALARM_KV_T;

static MUTEX_HANDLE sg_lock = NULL;
static TIMER_ID     sg_tick = NULL;
static TIMER_ID     sg_ring_tm = NULL;

static bool     sg_alarm_on = false;
static uint8_t  sg_alarm_hour = 7;
static uint8_t  sg_alarm_min = 30;
static int      sg_alarm_fired_min = -1; /* minute-of-day last fired, avoids re-fire */

static bool     sg_timer_on = false;
static uint32_t sg_timer_remain_s = 0;

static bool          sg_ringing = false;
static ALARM_KIND_E  sg_ring_kind = ALARM_KIND_NONE;
static int           sg_ring_left = 0;

static void __lock(void)
{
    if (sg_lock) {
        tal_mutex_lock(sg_lock);
    }
}

static void __unlock(void)
{
    if (sg_lock) {
        tal_mutex_unlock(sg_lock);
    }
}

static void __persist_alarm(void)
{
    ALARM_KV_T kv = {
        .on = sg_alarm_on ? 1 : 0,
        .hour = sg_alarm_hour,
        .min = sg_alarm_min,
    };
    (void)tal_kv_set(ALARM_KV_KEY, (const uint8_t *)&kv, sizeof(kv));
}

static void __load_alarm(void)
{
    uint8_t *buf = NULL;
    size_t len = 0;
    ALARM_KV_T kv;

    if (OPRT_OK != tal_kv_get(ALARM_KV_KEY, &buf, &len) || !buf || len < sizeof(kv)) {
        if (buf) {
            tal_kv_free(buf);
        }
        return;
    }
    memcpy(&kv, buf, sizeof(kv));
    tal_kv_free(buf);

    if (kv.hour > 23 || kv.min > 59) {
        return;
    }
    sg_alarm_on = kv.on ? true : false;
    sg_alarm_hour = kv.hour;
    sg_alarm_min = kv.min;
}

static void __ring_stop_unlocked(void)
{
    sg_ringing = false;
    sg_ring_kind = ALARM_KIND_NONE;
    sg_ring_left = 0;
    if (sg_ring_tm) {
        tal_sw_timer_stop(sg_ring_tm);
    }
}

static void __ring_beep(TIMER_ID timer_id, void *arg)
{
    bool beep = false;
    bool again = false;

    (void)timer_id;
    (void)arg;

    __lock();
    if (!sg_ringing || sg_ring_left <= 0) {
        __ring_stop_unlocked();
        __unlock();
        ui_popup_hold_hide();
        return;
    }
    sg_ring_left--;
    beep = true;
    again = (sg_ring_left > 0);
    __unlock();

    if (beep) {
        ai_audio_player_alert(AI_AUDIO_ALERT_WAKEUP);
    }
    if (again && sg_ring_tm) {
        tal_sw_timer_start(sg_ring_tm, ALARM_RING_GAP_MS, TAL_TIMER_ONCE);
    }
}

static void __ring_start(ALARM_KIND_E kind, const char *banner)
{
    __lock();
    sg_ringing = true;
    sg_ring_kind = kind;
    sg_ring_left = ALARM_RING_REPEAT;
    __unlock();

    page_mgr_goto(PAGE_IDX_CLOCK);
    ui_popup_hold_show(banner ? banner : "提醒 · 按键关闭");
    ai_audio_player_alert(AI_AUDIO_ALERT_WAKEUP);

    if (sg_ring_tm) {
        tal_sw_timer_start(sg_ring_tm, ALARM_RING_GAP_MS, TAL_TIMER_ONCE);
    }
}

static void __on_tick(TIMER_ID timer_id, void *arg)
{
    POSIX_TM_S tm;
    int minute_of_day;
    bool fire_alarm = false;
    bool fire_timer = false;

    (void)timer_id;
    (void)arg;

    if (OPRT_OK != tal_time_check_time_sync()) {
        return;
    }
    tal_time_get_local_time_custom(0, &tm);
    minute_of_day = tm.tm_hour * 60 + tm.tm_min;

    __lock();
    if (sg_timer_on) {
        if (sg_timer_remain_s > 0) {
            sg_timer_remain_s--;
        }
        if (sg_timer_remain_s == 0) {
            sg_timer_on = false;
            fire_timer = true;
        }
    }

    if (sg_alarm_on && !sg_ringing && tm.tm_sec == 0) {
        if (tm.tm_hour == sg_alarm_hour && tm.tm_min == sg_alarm_min &&
            sg_alarm_fired_min != minute_of_day) {
            sg_alarm_fired_min = minute_of_day;
            fire_alarm = true;
        }
    }
    /* Clear the fired marker once the minute rolls past so tomorrow works. */
    if (sg_alarm_fired_min >= 0 && sg_alarm_fired_min != minute_of_day) {
        sg_alarm_fired_min = -1;
    }
    __unlock();

    if (fire_timer) {
        PR_NOTICE("[alarm] timer done");
        __ring_start(ALARM_KIND_TIMER, "计时到 · 按键关闭");
        ui_popup_toast(bmo_tr(BMO_STR_TOAST_TIMER_END));
    }
    if (fire_alarm) {
        PR_NOTICE("[alarm] alarm %02d:%02d", sg_alarm_hour, sg_alarm_min);
        __ring_start(ALARM_KIND_ALARM, "闹钟 · 按键关闭");
        ui_popup_toast(bmo_tr(BMO_STR_TOAST_ALARM_RING));
    }
}

OPERATE_RET ui_alarm_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    if (sg_tick) {
        return OPRT_OK;
    }

    TUYA_CALL_ERR_RETURN(tal_mutex_create_init(&sg_lock));
    __load_alarm();

    TUYA_CALL_ERR_RETURN(tal_sw_timer_create(__on_tick, NULL, &sg_tick));
    TUYA_CALL_ERR_RETURN(tal_sw_timer_start(sg_tick, ALARM_TICK_MS, TAL_TIMER_CYCLE));
    TUYA_CALL_ERR_RETURN(tal_sw_timer_create(__ring_beep, NULL, &sg_ring_tm));

    PR_NOTICE("[alarm] init ok (alarm %s %02d:%02d)", sg_alarm_on ? "on" : "off", sg_alarm_hour,
              sg_alarm_min);
    return rt;
}

void ui_alarm_get_status(ALARM_STATUS_T *out)
{
    if (!out) {
        return;
    }
    __lock();
    out->alarm_on = sg_alarm_on;
    out->alarm_hour = sg_alarm_hour;
    out->alarm_min = sg_alarm_min;
    out->timer_on = sg_timer_on;
    out->timer_remain_s = sg_timer_remain_s;
    out->ringing = sg_ringing;
    out->ring_kind = sg_ring_kind;
    __unlock();
}

OPERATE_RET ui_alarm_set(uint8_t hour, uint8_t minute)
{
    if (hour > 23 || minute > 59) {
        return OPRT_INVALID_PARM;
    }
    __lock();
    sg_alarm_hour = hour;
    sg_alarm_min = minute;
    sg_alarm_on = true;
    sg_alarm_fired_min = -1;
    __persist_alarm();
    __unlock();
    PR_NOTICE("[alarm] set %02d:%02d", hour, minute);
    return OPRT_OK;
}

OPERATE_RET ui_alarm_clear(void)
{
    __lock();
    sg_alarm_on = false;
    sg_alarm_fired_min = -1;
    __persist_alarm();
    __unlock();
    PR_NOTICE("[alarm] cleared");
    return OPRT_OK;
}

OPERATE_RET ui_alarm_timer_start(uint32_t total_s)
{
    if (total_s < 1 || total_s > 59 * 60 + 59) {
        return OPRT_INVALID_PARM;
    }
    __lock();
    sg_timer_on = true;
    sg_timer_remain_s = total_s;
    __unlock();
    PR_NOTICE("[alarm] timer start %us", (unsigned)total_s);
    return OPRT_OK;
}

OPERATE_RET ui_alarm_timer_cancel(void)
{
    __lock();
    sg_timer_on = false;
    sg_timer_remain_s = 0;
    __unlock();
    PR_NOTICE("[alarm] timer cancel");
    return OPRT_OK;
}

void ui_alarm_dismiss(void)
{
    bool was;

    __lock();
    was = sg_ringing;
    __ring_stop_unlocked();
    __unlock();
    if (was) {
        ui_popup_hold_hide();
    }
}

bool ui_alarm_try_dismiss(void)
{
    bool was;

    __lock();
    was = sg_ringing;
    if (was) {
        __ring_stop_unlocked();
    }
    __unlock();
    if (was) {
        ui_popup_hold_hide();
        ui_popup_toast(bmo_tr(BMO_STR_TOAST_ALERT_OFF));
        PR_NOTICE("[alarm] dismissed");
    }
    return was;
}

bool ui_alarm_is_ringing(void)
{
    bool r;
    __lock();
    r = sg_ringing;
    __unlock();
    return r;
}
