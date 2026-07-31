/**
 * @file ui_mcp_tools.c
 * @brief Desktop avatar MCP tools: expression + page navigation.
 *
 * Platform agent prompt (paste into Tuya AI agent settings):
 * ---
 * 你是一只有可变表情的桌面宠物，屏幕有多个页面：表情脸(avatar)、时钟(clock)、
 * 天气(weather)、日历(calendar)。回复时主动用 pet.expression.set 做肢体表达：
 * 同意/开心用 happy，否定/生气用 angry，好奇用 thinking，困/睡用 sleepy。
 * 用户要看时间/天气/日程时，调用 pet.screen.show_page 切换页面；看完可切回 avatar。
 * ---
 */

#include "ui_mcp_tools.h"
#include "ui_avatar.h"
#include "ui_page_mgr.h"
#include "ai_mcp_server.h"
#include "tal_api.h"
#include "lv_vendor.h"

#include <string.h>

#define EXPR_VALID_LIST \
    "neutral, happy, laughing, sad, angry, surprised, loving, embarrassed, " \
    "thinking, wink, sleepy, look_left, look_right"

#define PAGE_VALID_LIST "avatar, clock, weather, calendar"

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
        "- page (string): avatar (face/expression), clock (time), weather, calendar (schedule).\n"
        "Use when the user wants to see time, weather, calendar, or return to the face.\n"
        "Response: ok or error with valid pages.",
        __pet_screen_show_page_cb,
        NULL,
        MCP_PROP_STR("page", "Page id: avatar, clock, weather, or calendar.")
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
