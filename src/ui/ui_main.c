/**
 * @file ui_main.c
 * @brief Custom AI chat UI registration and event dispatch.
 */

#include "tal_api.h"
#include <string.h>
#include "lvgl.h"
#include "lv_vendor.h"
#include "ai_ui_manage.h"
#include "ai_ui_icon_font.h"
#include "lang_config.h"
#include "ui_main.h"
#include "ui_avatar.h"
#include "ui_page_mgr.h"
#include "ui_buttons.h"
#include "ui_bg_task.h"
#include "ui_alarm.h"
#include "ui_i18n.h"
#include "ui_chat_overlay.h"
#include "ui_landscape_test.h"

static bool sg_lvgl_ready = false;
static char sg_last_caption[128];
static char sg_last_state[48] = "Standby";

/* ASCII status for monochrome font without CJK glyphs. */
static const char *__status_ascii(const char *state)
{
    if (!state) {
        return "Say NiHaoTuYa / Click";
    }
    if (0 == strcmp(state, STANDBY) || 0 == strcmp(state, INITIALIZING)) {
        return "Say NiHaoTuYa / Click";
    }
    if (0 == strcmp(state, LISTENING)) {
        return "Listening...";
    }
    if (0 == strcmp(state, UPLOADING)) {
        return "Uploading...";
    }
    if (0 == strcmp(state, THINKING)) {
        return "Thinking...";
    }
    if (0 == strcmp(state, SPEAKING)) {
        return "Speaking...";
    }
    /* Already ASCII or unknown: pass through */
    return state;
}

static AVATAR_STATE_E __map_mode_state(const char *state)
{
    if (!state) {
        return AVATAR_IDLE;
    }
    if (0 == strcmp(state, LISTENING)) {
        return AVATAR_LISTEN;
    }
    if (0 == strcmp(state, UPLOADING) || 0 == strcmp(state, THINKING)) {
        return AVATAR_THINK;
    }
    if (0 == strcmp(state, SPEAKING)) {
        return AVATAR_SPEAK;
    }
    return AVATAR_IDLE;
}

static void __ui_lock_update_mode(const char *state)
{
    const char *ascii_st;

    if (!sg_lvgl_ready) {
        return;
    }

    ascii_st = __status_ascii(state);

    lv_vendor_disp_lock();
    /* Going idle keeps the current mood on screen; ui_avatar drifts to the
     * sleep face by itself once nothing has happened for a while. */
    avatar_set_state(__map_mode_state(state));
    avatar_set_status_text(ascii_st);
    strncpy(sg_last_state, ascii_st, sizeof(sg_last_state) - 1);
    sg_last_state[sizeof(sg_last_state) - 1] = '\0';
    chat_overlay_update(sg_last_state, sg_last_caption);
    lv_vendor_disp_unlock();
}

static void __ui_lock_update_emotion(char *emotion)
{
    if (!sg_lvgl_ready) {
        return;
    }
    lv_vendor_disp_lock();
    avatar_set_emotion(emotion);
    lv_vendor_disp_unlock();
}

static OPERATE_RET __ui_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    lv_vendor_init(DISPLAY_NAME);
    lv_vendor_start(5, 1024 * 8);

    lv_vendor_disp_lock();
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);
    bmo_lang_init();
    page_mgr_init();
    chat_overlay_init();
#if defined(UI_LANDSCAPE_TEST) && (UI_LANDSCAPE_TEST == 1)
    ui_landscape_test_show(scr, 2500);
#endif
    sg_lvgl_ready = true;
    lv_vendor_disp_unlock();

    TUYA_CALL_ERR_RETURN(ui_buttons_init());
    TUYA_CALL_ERR_RETURN(ui_bg_task_init());
    TUYA_CALL_ERR_RETURN(ui_alarm_init());

    PR_NOTICE("avatar ui init ok");
    return OPRT_OK;
}

static void __ui_mode_state(char *state)
{
    PR_NOTICE("[UI] mode state: %s", state ? state : "(null)");
    __ui_lock_update_mode(state);
}

static void __ui_emotion(char *emotion)
{
    PR_NOTICE("[UI] emotion: %s", emotion ? emotion : "(null)");
    __ui_lock_update_emotion(emotion);
}

static void __ui_notification(char *text)
{
    const char *ascii_st;

    PR_NOTICE("[UI] notify: %s", text ? text : "(null)");
    if (!sg_lvgl_ready || !text) {
        return;
    }
    ascii_st = __status_ascii(text);
    lv_vendor_disp_lock();
    avatar_set_status_text(ascii_st);
    strncpy(sg_last_state, ascii_st, sizeof(sg_last_state) - 1);
    sg_last_state[sizeof(sg_last_state) - 1] = '\0';
    chat_overlay_update(sg_last_state, sg_last_caption);
    lv_vendor_disp_unlock();
}

static void __ui_wifi(AI_UI_WIFI_STATUS_E status)
{
    if (!sg_lvgl_ready) {
        return;
    }
    lv_vendor_disp_lock();
    avatar_set_wifi(status);
    lv_vendor_disp_unlock();
}

static void __ui_user_msg(char *text)
{
    PR_NOTICE("[UI] user said: %s", text ? text : "(null)");
    if (!sg_lvgl_ready || !text) {
        return;
    }
    strncpy(sg_last_caption, text, sizeof(sg_last_caption) - 1);
    sg_last_caption[sizeof(sg_last_caption) - 1] = '\0';
    page_mgr_try_asr_navigate(text);
    lv_vendor_disp_lock();
    avatar_set_caption(text);
    chat_overlay_update(sg_last_state, sg_last_caption);
    lv_vendor_disp_unlock();
}

static void __ui_ai_stream_data(char *text)
{
    if (!text) {
        return;
    }
    PR_NOTICE("[UI] ai text: %s", text);
    if (!sg_lvgl_ready) {
        return;
    }
    strncpy(sg_last_caption, text, sizeof(sg_last_caption) - 1);
    sg_last_caption[sizeof(sg_last_caption) - 1] = '\0';
    lv_vendor_disp_lock();
    avatar_set_caption(text);
    chat_overlay_update(sg_last_state, sg_last_caption);
    lv_vendor_disp_unlock();
}

static void __ui_ai_msg(char *text)
{
    __ui_ai_stream_data(text);
}

OPERATE_RET avatar_ui_register(void)
{
    OPERATE_RET rt = OPRT_OK;

    AI_UI_INTFS_T intfs = {0};
    intfs.disp_init          = __ui_init;
    intfs.disp_emotion       = __ui_emotion;
    intfs.disp_ai_mode_state = __ui_mode_state;
    intfs.disp_notification  = __ui_notification;
    intfs.disp_wifi_state    = __ui_wifi;
    TUYA_CALL_ERR_RETURN(ai_ui_register(&intfs));

    AI_UI_CHAT_INTFS_T chat = {0};
    chat.disp_user_msg           = __ui_user_msg;
    chat.disp_ai_msg             = __ui_ai_msg;
    chat.disp_ai_msg_stream_data = __ui_ai_stream_data;
    TUYA_CALL_ERR_RETURN(ai_ui_chat_register(&chat));

    PR_NOTICE("avatar ui registered");
    return OPRT_OK;
}
