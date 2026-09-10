/**
 * @file ui_mcp_tools.c
 * @brief Desktop avatar MCP tools: expression + page navigation.
 *
 * Platform agent prompt (paste into Tuya AI agent settings):
 * ---
 * 你是一只有可变表情的桌面宠物，屏幕有多个页面：表情脸(avatar)、时钟(clock)、
 * 天气(weather)、日历(calendar)、小游戏(games)。回复时主动用 pet.expression.set 做肢体表达：
 * 同意/开心用 happy，否定/生气用 angry，好奇用 thinking，困/睡用 sleepy。
 * 用户要看时间/天气/日程时，调用 pet.screen.show_page 切换页面；看完可切回 avatar。
 * 用户要记日程/提醒时，调用 pet.calendar.add_event：日期用 day_offset（今天 0、明天 1），
 * 时间用 24 小时制；没说具体时间就先问一句。添加后复述一遍时间。
 * ---
 */

#include "ui_mcp_tools.h"
#include "ui_avatar.h"
#include "ui_page_mgr.h"
#include "ui_popup.h"
#include "ui_i18n.h"
#include "ui_alarm.h"
#include "ui_bg_task.h"
#include "feishu_cal.h"
#include "rss_feed.h"
#include "ai_mcp_server.h"
#include "tal_api.h"
#include "lv_vendor.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define EXPR_VALID_LIST \
    "neutral, happy, laughing, sad, angry, surprised, loving, embarrassed, " \
    "thinking, wink, sleepy, look_left, look_right"

#define PAGE_VALID_LIST "avatar, clock, weather, calendar, rss, games, settings"
#define RSS_MCP_BUF_SIZE  1024
#define RSS_MCP_LIMIT_DEF 5
#define RSS_MCP_LIMIT_MAX 8

static const char *__mcp_get_str_prop(const MCP_PROPERTY_LIST_T *properties, const char *name)
{
    int i;

    if (properties == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (prop != NULL && strcmp(prop->name, name) == 0 &&
            prop->type == MCP_PROPERTY_TYPE_STRING && prop->default_val.str_val != NULL) {
            return prop->default_val.str_val;
        }
    }
    return NULL;
}

static int __mcp_get_int_prop(const MCP_PROPERTY_LIST_T *properties, const char *name, int dft)
{
    int i;

    if (properties == NULL || name == NULL) {
        return dft;
    }
    for (i = 0; i < properties->count; i++) {
        MCP_PROPERTY_T *prop = properties->properties[i];
        if (prop != NULL && strcmp(prop->name, name) == 0 && prop->type == MCP_PROPERTY_TYPE_INTEGER) {
            return prop->default_val.int_val;
        }
    }
    return dft;
}

static OPERATE_RET __pet_expression_set_cb(const MCP_PROPERTY_LIST_T *properties,
                                           MCP_RETURN_VALUE_T *ret_val,
                                           void *user_data)
{
    const char *name;

    (void)user_data;

    name = __mcp_get_str_prop(properties, "name");
    if (!name || !name[0]) {
        ai_mcp_return_value_set_str(ret_val, "missing name parameter");
        return OPRT_OK;
    }

    page_mgr_goto(0);

    lv_vendor_disp_lock();
    if (avatar_express(name)) {
        avatar_express_hold(name, 10000);
        ai_mcp_return_value_set_str(ret_val, "ok");
    } else {
        ai_mcp_return_value_set_str(ret_val, "unknown expression, valid: " EXPR_VALID_LIST);
    }
    lv_vendor_disp_unlock();

    PR_NOTICE("[MCP] pet.expression.set name=%s", name);
    return OPRT_OK;
}

static OPERATE_RET __pet_screen_show_page_cb(const MCP_PROPERTY_LIST_T *properties,
                                              MCP_RETURN_VALUE_T *ret_val,
                                              void *user_data)
{
    const char *page;
    int idx;

    (void)user_data;

    page = __mcp_get_str_prop(properties, "page");
    if (!page || !page[0]) {
        ai_mcp_return_value_set_str(ret_val, "missing page parameter");
        return OPRT_OK;
    }

    idx = page_mgr_name_to_idx(page);
    if (idx < 0) {
        ai_mcp_return_value_set_str(ret_val, "unknown page, valid: " PAGE_VALID_LIST);
        return OPRT_OK;
    }

    page_mgr_goto(idx);
    ai_mcp_return_value_set_str(ret_val, "ok");
    PR_NOTICE("[MCP] pet.screen.show_page page=%s idx=%d", page, idx);
    return OPRT_OK;
}

/*
 * Dates are relative on purpose: the agent has no reliable notion of what today
 * is, but the device is NTP-synced, so let it resolve "tomorrow" itself.
 */
static OPERATE_RET __pet_calendar_add_event_cb(const MCP_PROPERTY_LIST_T *properties,
                                               MCP_RETURN_VALUE_T *ret_val,
                                               void *user_data)
{
    const char *title;
    int day_offset, hour, minute, duration;
    char when[32] = {0};
    char msg[128];

    (void)user_data;

    title = __mcp_get_str_prop(properties, "title");
    if (!title || !title[0]) {
        ai_mcp_return_value_set_str(ret_val, "missing title parameter");
        return OPRT_OK;
    }

    day_offset = __mcp_get_int_prop(properties, "day_offset", 0);
    hour       = __mcp_get_int_prop(properties, "hour", -1);
    minute     = __mcp_get_int_prop(properties, "minute", 0);
    duration   = __mcp_get_int_prop(properties, "duration_min", 60);

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        ai_mcp_return_value_set_str(ret_val, "hour must be 0-23 and minute 0-59");
        return OPRT_OK;
    }
    if (day_offset < 0 || day_offset > 365 || duration < 1 || duration > 1440) {
        ai_mcp_return_value_set_str(ret_val, "day_offset must be 0-365 and duration_min 1-1440");
        return OPRT_OK;
    }

    if (OPRT_OK != feishu_cal_add_event(title, day_offset, hour, minute, duration, when, sizeof(when))) {
        ai_mcp_return_value_set_str(ret_val,
                                    "could not add the event, check that the calendar is linked and the "
                                    "bot has writer access");
        return OPRT_OK;
    }

    snprintf(msg, sizeof(msg), "added \"%s\" on %s", title, when);
    ai_mcp_return_value_set_str(ret_val, msg);
    PR_NOTICE("[MCP] pet.calendar.add_event %s", msg);

    page_mgr_goto(PAGE_IDX_CALENDAR);
    ui_popup_toast(bmo_tr(BMO_STR_TOAST_EVENT_ADDED));
    return OPRT_OK;
}

static OPERATE_RET __pet_alarm_set_cb(const MCP_PROPERTY_LIST_T *properties, MCP_RETURN_VALUE_T *ret_val,
                                      void *user_data)
{
    int hour, minute;
    char msg[64];

    (void)user_data;
    hour = __mcp_get_int_prop(properties, "hour", -1);
    minute = __mcp_get_int_prop(properties, "minute", 0);
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        ai_mcp_return_value_set_str(ret_val, "hour must be 0-23 and minute 0-59");
        return OPRT_OK;
    }
    if (OPRT_OK != ui_alarm_set((uint8_t)hour, (uint8_t)minute)) {
        ai_mcp_return_value_set_str(ret_val, "failed to set alarm");
        return OPRT_OK;
    }
    snprintf(msg, sizeof(msg), "alarm set for %02d:%02d", hour, minute);
    ai_mcp_return_value_set_str(ret_val, msg);
    page_mgr_goto(PAGE_IDX_CLOCK);
    ui_popup_toast(bmo_tr(BMO_STR_TOAST_ALARM_SET));
    PR_NOTICE("[MCP] pet.alarm.set %s", msg);
    return OPRT_OK;
}

static OPERATE_RET __pet_alarm_clear_cb(const MCP_PROPERTY_LIST_T *properties, MCP_RETURN_VALUE_T *ret_val,
                                        void *user_data)
{
    (void)properties;
    (void)user_data;
    ui_alarm_clear();
    ai_mcp_return_value_set_str(ret_val, "alarm cleared");
    ui_popup_toast(bmo_tr(BMO_STR_TOAST_ALARM_OFF));
    PR_NOTICE("[MCP] pet.alarm.clear");
    return OPRT_OK;
}

static OPERATE_RET __pet_timer_start_cb(const MCP_PROPERTY_LIST_T *properties, MCP_RETURN_VALUE_T *ret_val,
                                        void *user_data)
{
    int minutes, seconds;
    uint32_t total;
    char msg[64];

    (void)user_data;
    minutes = __mcp_get_int_prop(properties, "minutes", 0);
    seconds = __mcp_get_int_prop(properties, "seconds", 0);
    if (minutes < 0 || minutes > 59 || seconds < 0 || seconds > 59) {
        ai_mcp_return_value_set_str(ret_val, "minutes and seconds must be 0-59");
        return OPRT_OK;
    }
    total = (uint32_t)minutes * 60u + (uint32_t)seconds;
    if (total == 0) {
        ai_mcp_return_value_set_str(ret_val, "duration must be at least 1 second");
        return OPRT_OK;
    }
    if (OPRT_OK != ui_alarm_timer_start(total)) {
        ai_mcp_return_value_set_str(ret_val, "failed to start timer");
        return OPRT_OK;
    }
    snprintf(msg, sizeof(msg), "timer started for %d:%02d", minutes, seconds);
    ai_mcp_return_value_set_str(ret_val, msg);
    page_mgr_goto(PAGE_IDX_CLOCK);
    ui_popup_toast(bmo_tr(BMO_STR_TOAST_TIMER_START));
    PR_NOTICE("[MCP] pet.timer.start %s", msg);
    return OPRT_OK;
}

static OPERATE_RET __pet_timer_cancel_cb(const MCP_PROPERTY_LIST_T *properties, MCP_RETURN_VALUE_T *ret_val,
                                         void *user_data)
{
    (void)properties;
    (void)user_data;
    ui_alarm_timer_cancel();
    ai_mcp_return_value_set_str(ret_val, "timer cancelled");
    ui_popup_toast(bmo_tr(BMO_STR_TOAST_TIMER_CANCEL));
    PR_NOTICE("[MCP] pet.timer.cancel");
    return OPRT_OK;
}

static int __rss_source_match(const char *want)
{
    char buf[24];
    int i, n;

    if (!want || !want[0] || 0 == strcmp(want, "all") || 0 == strcmp(want, "全部")) {
        return -1;
    }
    n = (int)strlen(want);
    if (n >= (int)sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
    }
    for (i = 0; i < n; i++) {
        buf[i] = (char)tolower((unsigned char)want[i]);
    }
    buf[n] = '\0';

    for (i = 0; i < RSS_SOURCE_CNT; i++) {
        const char *name = rss_source_name(i);
        char nbuf[24];
        int j, nn = (int)strlen(name);
        if (nn >= (int)sizeof(nbuf)) {
            nn = (int)sizeof(nbuf) - 1;
        }
        for (j = 0; j < nn; j++) {
            nbuf[j] = (char)tolower((unsigned char)name[j]);
        }
        nbuf[nn] = '\0';
        if (0 == strcmp(buf, nbuf) || strstr(nbuf, buf) != NULL || strstr(buf, nbuf) != NULL) {
            return i;
        }
    }
    /* Friendly aliases */
    if (strstr(buf, "hack") != NULL) {
        return 0;
    }
    if (strstr(buf, "cnx") != NULL) {
        return 1;
    }
    if (strstr(buf, "make") != NULL) {
        return 2;
    }
    if (strstr(buf, "learn") != NULL) {
        return 4;
    }
    if (strstr(buf, "adafruit") != NULL) {
        return 3;
    }
    if (0 == strcmp(buf, "pi") || strstr(buf, "raspberry") != NULL) {
        return 5;
    }
    if (strstr(buf, "seeed") != NULL) {
        return 6;
    }
    if (strstr(buf, "arduino") != NULL) {
        return 7;
    }
    return -2;
}

/*
 * Returns a short numbered list of cached Maker RSS titles so the cloud agent
 * can speak them. Does not fetch; uses the bg-task cache (refreshed periodically
 * or via the green key / pet.screen.show_page + press).
 */
static OPERATE_RET __pet_rss_get_headlines_cb(const MCP_PROPERTY_LIST_T *properties,
                                              MCP_RETURN_VALUE_T *ret_val,
                                              void *user_data)
{
    RSS_FEED_DATA_T *data;
    const char *source;
    int limit, src_idx, n = 0;
    char buf[RSS_MCP_BUF_SIZE];
    int used = 0;

    (void)user_data;

    source = __mcp_get_str_prop(properties, "source");
    limit = __mcp_get_int_prop(properties, "limit", RSS_MCP_LIMIT_DEF);
    if (limit < 1) {
        limit = 1;
    }
    if (limit > RSS_MCP_LIMIT_MAX) {
        limit = RSS_MCP_LIMIT_MAX;
    }

    src_idx = __rss_source_match(source);
    if (src_idx == -2) {
        ai_mcp_return_value_set_str(ret_val,
                                    "unknown source, use all or one of: Hackaday, CNX, Make, "
                                    "Adafruit, Learn, Pi, Seeed, Arduino");
        return OPRT_OK;
    }

    data = (RSS_FEED_DATA_T *)tal_psram_malloc(sizeof(*data));
    if (!data) {
        ai_mcp_return_value_set_str(ret_val, "out of memory");
        return OPRT_OK;
    }
    if (!ui_bg_task_copy_rss(data)) {
        tal_psram_free(data);
        ai_mcp_return_value_set_str(ret_val,
                                    "no RSS headlines yet; ask the user to wait for sync or "
                                    "open the rss page and press the green refresh key");
        return OPRT_OK;
    }

    used = snprintf(buf, sizeof(buf), "Maker headlines");
    if (src_idx >= 0) {
        used += snprintf(buf + used, sizeof(buf) - (size_t)used, " (%s)", rss_source_name(src_idx));
    }
    used += snprintf(buf + used, sizeof(buf) - (size_t)used, ":\n");

    for (int i = 0; i < data->count && n < limit; i++) {
        int wrote;

        if (src_idx >= 0 && data->items[i].source != (uint8_t)src_idx) {
            continue;
        }
        n++;
        wrote = snprintf(buf + used, sizeof(buf) - (size_t)used, "%d. [%s] %s\n", n,
                         rss_source_name(data->items[i].source), data->items[i].title);
        if (wrote <= 0 || used + wrote >= (int)sizeof(buf)) {
            break;
        }
        used += wrote;
    }
    tal_psram_free(data);

    if (n == 0) {
        ai_mcp_return_value_set_str(ret_val, "no headlines for that source right now");
        return OPRT_OK;
    }

    ai_mcp_return_value_set_str(ret_val, buf);
    PR_NOTICE("[MCP] pet.rss.get_headlines source=%s limit=%d n=%d",
              source && source[0] ? source : "all", limit, n);
    return OPRT_OK;
}

static OPERATE_RET __ui_mcp_tools_register(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "pet.expression.set",
        "Set the desktop pet face expression. Use when expressing emotion while talking, "
        "or when the user asks for a specific face.\n"
        "Parameters:\n"
        "- name (string): one of " EXPR_VALID_LIST ".\n"
        "Examples: nod agreement with happy; disagree with angry; user says 'look sleepy' -> sleepy.\n"
        "Response: ok or error with valid names.",
        __pet_expression_set_cb,
        NULL,
        MCP_PROP_STR("name", "Expression name, e.g. happy, sad, sleepy, look_left.")
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "pet.screen.show_page",
        "Switch the device screen to an information page.\n"
        "Parameters:\n"
        "- page (string): avatar (face/expression), clock (time), weather, calendar (schedule), "
        "rss (Maker news headlines), games (snake/tetris), settings (language/OTA).\n"
        "Use when the user wants to see time, weather, calendar, Maker news/RSS, games, settings, or return to the face.\n"
        "Response: ok or error with valid pages.",
        __pet_screen_show_page_cb,
        NULL,
        MCP_PROP_STR("page", "Page id: avatar, clock, weather, calendar, rss, games, or settings.")
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "pet.calendar.add_event",
        "Add an event to the user's calendar. Use when the user asks to schedule, book or remind "
        "them of something at a given time.\n"
        "The date is relative to today because the device holds the clock: use day_offset=0 for "
        "today, 1 for tomorrow, 2 for the day after. Ask the user for a time if none was given.\n"
        "Example: 'remind me to call mum tomorrow at 7pm' -> "
        "title='call mum', day_offset=1, hour=19, minute=0.\n"
        "Response: a confirmation with the booked date, or an error to relay to the user.",
        __pet_calendar_add_event_cb,
        NULL,
        MCP_PROP_STR("title", "Event title, e.g. 'team meeting'."),
        MCP_PROP_INT_DEF_RANGE("day_offset", "Days from today: 0 today, 1 tomorrow.", 0, 0, 365),
        MCP_PROP_INT_RANGE("hour", "Start hour in 24h local time, 0-23.", 0, 23),
        MCP_PROP_INT_DEF_RANGE("minute", "Start minute, 0-59.", 0, 0, 59),
        MCP_PROP_INT_DEF_RANGE("duration_min", "Length in minutes.", 60, 1, 1440)
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "pet.alarm.set",
        "Set a local alarm clock on the device (plays a sound at that time). This is NOT a Feishu "
        "calendar event — use pet.calendar.add_event for schedule entries.\n"
        "Parameters: hour (0-23) and minute (0-59) in local time. If the time is already past today, "
        "it will ring tomorrow at that time.\n"
        "Example: 'set an alarm for 7:30' -> hour=7, minute=30.\n"
        "Response: confirmation with the time.",
        __pet_alarm_set_cb,
        NULL,
        MCP_PROP_INT_RANGE("hour", "Alarm hour 0-23 local time.", 0, 23),
        MCP_PROP_INT_DEF_RANGE("minute", "Alarm minute 0-59.", 0, 0, 59)
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "pet.alarm.clear",
        "Cancel the device alarm clock. Use when the user says cancel/turn off the alarm.",
        __pet_alarm_clear_cb,
        NULL
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "pet.timer.start",
        "Start a countdown timer on the device. Use for 'timer for 5 minutes', 'countdown 90 seconds'.\n"
        "Parameters: minutes (0-59) and seconds (0-59). Total must be at least 1 second, max 59:59.\n"
        "Example: 'timer 5 minutes' -> minutes=5, seconds=0.\n"
        "Response: confirmation with the duration.",
        __pet_timer_start_cb,
        NULL,
        MCP_PROP_INT_DEF_RANGE("minutes", "Minutes 0-59.", 0, 0, 59),
        MCP_PROP_INT_DEF_RANGE("seconds", "Seconds 0-59.", 0, 0, 59)
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "pet.timer.cancel",
        "Cancel the running countdown timer.",
        __pet_timer_cancel_cb,
        NULL
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "pet.rss.get_headlines",
        "Read Maker RSS headlines currently cached on the device so you can speak them aloud.\n"
        "Use when the user asks what is new in hardware/Maker news, Hackaday, CNX, Adafruit, etc.\n"
        "Optionally call pet.screen.show_page with page=rss first so they can follow on screen.\n"
        "Parameters:\n"
        "- source (string, optional): all (default), or Hackaday, CNX, Make, Adafruit, Learn, Pi, Seeed, Arduino.\n"
        "- limit (int, optional): how many titles to return, default 5, max 8.\n"
        "Response: a short numbered list of titles. Speak at most three aloud unless asked for more.\n"
        "If empty, tell the user headlines are still syncing.",
        __pet_rss_get_headlines_cb,
        NULL,
        MCP_PROP_STR_DEF("source", "Feed filter: all, Hackaday, CNX, Make, Adafruit, Learn, Pi, Seeed, Arduino.", "all"),
        MCP_PROP_INT_DEF_RANGE("limit", "Max headlines to return.", RSS_MCP_LIMIT_DEF, 1, RSS_MCP_LIMIT_MAX)
    ), err);

    PR_NOTICE("Desktop avatar MCP tools registered");
    return OPRT_OK;

err:
    PR_ERR("Desktop avatar MCP tools register failed: %d", rt);
    return rt;
}

static OPERATE_RET __ui_mcp_on_mqtt_connected(void *data)
{
    (void)data;
    return __ui_mcp_tools_register();
}

OPERATE_RET ui_mcp_tools_init(void)
{
    return tal_event_subscribe(EVENT_MQTT_CONNECTED, "avatar_mcp_tools", __ui_mcp_on_mqtt_connected,
                               SUBSCRIBE_TYPE_ONETIME);
}
