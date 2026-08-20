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
#include "feishu_cal.h"
#include "ai_mcp_server.h"
#include "tal_api.h"
#include "lv_vendor.h"

#include <string.h>

#define EXPR_VALID_LIST \
    "neutral, happy, laughing, sad, angry, surprised, loving, embarrassed, " \
    "thinking, wink, sleepy, look_left, look_right"

#define PAGE_VALID_LIST "avatar, clock, weather, calendar, games"

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
    ui_popup_toast("日程已添加");
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
        "- page (string): avatar (face/expression), clock (time), weather, calendar (schedule), games (snake/tetris).\n"
        "Use when the user wants to see time, weather, calendar, games, or return to the face.\n"
        "Response: ok or error with valid pages.",
        __pet_screen_show_page_cb,
        NULL,
        MCP_PROP_STR("page", "Page id: avatar, clock, weather, calendar, or games.")
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
