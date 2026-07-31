/**
 * @file motion_mcp.c
 * @brief MCP tools: pet.arm.pose, pet.motion.play
 */

#include "motion_mcp.h"
#include "motion_engine.h"
#include "servo_pwm.h"
#include "ai_mcp_server.h"
#include "tal_api.h"
#include <string.h>
#include <ctype.h>

#define MOTION_VALID_LIST \
    "neutral, wave_left, wave_right, cheer_both, droop_sad, think_pose, dance, idle_sway"

static int __mcp_strcasecmp(const char *a, const char *b)
{
    if (!a || !b) {
        return 1;
    }
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

static const char *__mcp_get_str_prop(const MCP_PROPERTY_LIST_T *props, const char *name)
{
    int i;
    if (!props || !name) {
        return NULL;
    }
    for (i = 0; i < props->count; i++) {
        MCP_PROPERTY_T *p = props->properties[i];
        if (p && 0 == strcmp(p->name, name) && p->type == MCP_PROPERTY_TYPE_STRING) {
            return p->default_val.str_val;
        }
    }
    return NULL;
}

static int __mcp_get_int_prop(const MCP_PROPERTY_LIST_T *props, const char *name, int def)
{
    int i;
    if (!props || !name) {
        return def;
    }
    for (i = 0; i < props->count; i++) {
        MCP_PROPERTY_T *p = props->properties[i];
        if (p && 0 == strcmp(p->name, name) && p->type == MCP_PROPERTY_TYPE_INTEGER) {
            return p->default_val.int_val;
        }
    }
    return def;
}

static OPERATE_RET __pet_arm_pose_cb(const MCP_PROPERTY_LIST_T *properties,
                                      MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    const char *arm_name;
    int angle;

    (void)user_data;

    arm_name = __mcp_get_str_prop(properties, "arm");
    angle = __mcp_get_int_prop(properties, "angle", 90);
    if (angle < 0) {
        angle = 0;
    }
    if (angle > 180) {
        angle = 180;
    }

    if (!arm_name) {
        ai_mcp_return_value_set_str(ret_val, "missing arm (left/right)");
        return OPRT_OK;
    }
    if (0 == __mcp_strcasecmp(arm_name, "left")) {
        servo_set_angle(SERVO_ARM_LEFT, (uint8_t)angle);
    } else if (0 == __mcp_strcasecmp(arm_name, "right")) {
        servo_set_angle(SERVO_ARM_RIGHT, (uint8_t)angle);
    } else {
        ai_mcp_return_value_set_str(ret_val, "arm must be left or right");
        return OPRT_OK;
    }

    ai_mcp_return_value_set_str(ret_val, "ok");
    PR_NOTICE("[MCP] pet.arm.pose arm=%s angle=%d", arm_name, angle);
    return OPRT_OK;
}

static OPERATE_RET __pet_motion_play_cb(const MCP_PROPERTY_LIST_T *properties,
                                         MCP_RETURN_VALUE_T *ret_val, void *user_data)
{
    const char *name;

    (void)user_data;

    name = __mcp_get_str_prop(properties, "name");
    if (!name || !name[0]) {
        ai_mcp_return_value_set_str(ret_val, "missing name, valid: " MOTION_VALID_LIST);
        return OPRT_OK;
    }

    motion_engine_play_by_name(name);
    ai_mcp_return_value_set_str(ret_val, "ok");
    PR_NOTICE("[MCP] pet.motion.play name=%s", name);
    return OPRT_OK;
}

static OPERATE_RET __motion_mcp_register(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "pet.arm.pose",
        "Set one BMO arm servo angle.\n"
        "Parameters:\n"
        "- arm (string): left or right.\n"
        "- angle (integer): 0-180 degrees.\n"
        "Response: ok or error.",
        __pet_arm_pose_cb, NULL,
        MCP_PROP_STR("arm", "left or right"),
        MCP_PROP_INT_RANGE("angle", "Servo angle 0-180", 0, 180)
    ), err);

    TUYA_CALL_ERR_GOTO(AI_MCP_TOOL_ADD(
        "pet.motion.play",
        "Play a preset dual-arm motion sequence.\n"
        "Parameters:\n"
        "- name (string): one of " MOTION_VALID_LIST ".\n"
        "Use when user asks to wave, cheer, dance, or look sad.",
        __pet_motion_play_cb, NULL,
        MCP_PROP_STR("name", "Motion sequence name")
    ), err);

    PR_NOTICE("BMO motion MCP tools registered");
    return OPRT_OK;

err:
    PR_ERR("BMO motion MCP register failed: %d", rt);
    return rt;
}

static OPERATE_RET __motion_mcp_on_mqtt(void *data)
{
    (void)data;
    return __motion_mcp_register();
}

OPERATE_RET motion_mcp_init(void)
{
#if defined(ENABLE_MOTION_ENGINE) && (ENABLE_MOTION_ENGINE == 1)
    return tal_event_subscribe(EVENT_MQTT_CONNECTED, "bmo_motion_mcp", __motion_mcp_on_mqtt,
                               SUBSCRIBE_TYPE_ONETIME);
#else
    return OPRT_OK;
#endif
}
