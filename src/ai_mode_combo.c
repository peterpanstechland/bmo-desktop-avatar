/**
 * @file ai_mode_combo.c
 * @brief Combo chat mode: wake word + single-click wake + long-press PTT.
 *
 * Based on ai_mode_free.c, with HOLD-style long-press push-to-talk added.
 */

#include "tuya_cloud_types.h"

#include "tal_api.h"
#include "tuya_ai_agent.h"

#include "tkl_vad.h"
#include "tkl_kws.h"

#if defined(ENABLE_LED) && (ENABLE_LED == 1)
#include "tdl_led_manage.h"
#endif

#include "lang_config.h"
#include "ai_user_event.h"

#include "ai_audio_input.h"
#include "ai_audio_player.h"
#include "ai_manage_mode.h"
#include "ai_mode_combo.h"
#include "ui_popup.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define MODE_STATE_CHANGE(_old, _new)                                                                              \
    do {                                                                                                           \
        PR_DEBUG("mode combo state change form %s to %s", ai_get_mode_state_str(_old),                             \
                 ai_get_mode_state_str(_new));                                                                     \
        _old = _new;                                                                                               \
    } while (0)

#define AI_CHAT_WAKEUP_TIME_MS (30 * 1000) /* 30sec */

/***********************************************************
***********************variable define**********************
***********************************************************/
#if defined(ENABLE_LED) && (ENABLE_LED == 1)
static TDL_LED_HANDLE_T sg_led_hdl = NULL;
#endif

static AI_MODE_STATE_E sg_mode_set_state = AI_MODE_STATE_INIT;
static AI_MODE_STATE_E sg_mode_cur_state = AI_MODE_STATE_INVALID;
static bool            sg_is_wakeup = false;
static bool            sg_manual_talk = false; /* long-press PTT in progress */
static TIMER_ID        sg_enter_idle_timer = NULL;
static uint32_t        sg_wakeup_time_ms = AI_CHAT_WAKEUP_TIME_MS;

/***********************************************************
***********************function define**********************
***********************************************************/
static void __ai_mode_kws_wakeup(TKL_KWS_WAKEUP_WORD_E wakeup_word)
{
    (void)wakeup_word;

    ai_audio_player_stop(AI_AUDIO_PLAYER_ALL);
    ai_audio_input_reset();
    tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);

    ai_audio_player_alert(AI_AUDIO_ALERT_WAKEUP);

    MODE_STATE_CHANGE(sg_mode_set_state, AI_MODE_STATE_LISTEN);
    sg_is_wakeup = true;
}

static void __ai_mode_enter_idle(void)
{
#if defined(ENABLE_LED) && (ENABLE_LED == 1)
    if (sg_led_hdl != NULL) {
        tdl_led_set_status(sg_led_hdl, TDL_LED_OFF);
    }
#endif

    tal_sw_timer_stop(sg_enter_idle_timer);

    ai_audio_input_wakeup_set(false);

    sg_is_wakeup = false;
    sg_manual_talk = false;

    tkl_vad_set_threshold(TKL_AUDIO_VAD_LOW);
}

static void __ai_mode_enter_listen(void)
{
#if defined(ENABLE_LED) && (ENABLE_LED == 1)
    if (sg_led_hdl != NULL) {
        tdl_led_flash(sg_led_hdl, 500);
    }
#endif

    tal_sw_timer_start(sg_enter_idle_timer, sg_wakeup_time_ms, TAL_TIMER_ONCE);

    sg_is_wakeup = true;
    ai_audio_input_wakeup_set(true);
}

static void __ai_mode_enter_upload(void)
{
    PR_DEBUG("[====ai_combo] upload");
}

static void __ai_mode_enter_think(void)
{
#if defined(ENABLE_LED) && (ENABLE_LED == 1)
    if (sg_led_hdl != NULL) {
        tdl_led_flash(sg_led_hdl, 2000);
    }
#endif

    tal_sw_timer_start(sg_enter_idle_timer, sg_wakeup_time_ms, TAL_TIMER_ONCE);

    ai_audio_input_wakeup_set(true);

    sg_is_wakeup = true;

    tkl_vad_set_threshold(TKL_AUDIO_VAD_MID);
}

static void __ai_mode_enter_speak(void)
{
#if defined(ENABLE_LED) && (ENABLE_LED == 1)
    if (sg_led_hdl != NULL) {
        tdl_led_set_status(sg_led_hdl, TDL_LED_ON);
    }
#endif

    tal_sw_timer_stop(sg_enter_idle_timer);
}

/**
 * Leave the conversation right away instead of waiting out the 30 s idle timer.
 * The state flags are cleared here rather than left to __ai_mode_enter_idle(),
 * which only runs later on the mode task: until then a PLAY_END event would see
 * sg_is_wakeup still set and bounce straight back into LISTEN.
 */
static void __ai_mode_exit_chat(void)
{
    ai_audio_player_stop(AI_AUDIO_PLAYER_ALL);
    ai_audio_input_reset();
    tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);
    tuya_ai_input_stop();

    sg_manual_talk = false;
    sg_is_wakeup = false;

    MODE_STATE_CHANGE(sg_mode_set_state, AI_MODE_STATE_IDLE);
    PR_NOTICE("[====ai_combo] exit chat by key");
    ui_popup_toast("Chat ended");
}

static void __ai_mode_enter_idle_time_cb(TIMER_ID timer_id, void *arg)
{
    (void)arg;

    if (ai_audio_player_is_playing()) {
        PR_NOTICE("player is playing, idle timer reset");
        tal_sw_timer_start(timer_id, sg_wakeup_time_ms, TAL_TIMER_ONCE);
        return;
    }

    MODE_STATE_CHANGE(sg_mode_set_state, AI_MODE_STATE_IDLE);
}

static OPERATE_RET __ai_mode_combo_init(void)
{
    OPERATE_RET rt = OPRT_OK;

#if defined(ENABLE_LED) && (ENABLE_LED == 1)
    sg_led_hdl = tdl_led_find_dev(LED_NAME);
    if (sg_led_hdl != NULL) {
        OPERATE_RET led_rt = tdl_led_open(sg_led_hdl);
        if (led_rt != OPRT_OK) {
            PR_WARN("LED open failed (rt=%d), continuing without LED", led_rt);
            sg_led_hdl = NULL;
        }
    } else {
        PR_WARN("LED \"%s\" not registered, continuing without LED", LED_NAME);
    }
#endif

    ai_audio_input_wakeup_mode_set(AI_AUDIO_VAD_AUTO);

    tkl_kws_reg_wakeup_cb(__ai_mode_kws_wakeup);
    tkl_kws_enable();

    TUYA_CALL_ERR_RETURN(tal_sw_timer_create(__ai_mode_enter_idle_time_cb, NULL, &sg_enter_idle_timer));

    MODE_STATE_CHANGE(sg_mode_set_state, AI_MODE_STATE_IDLE);
    sg_is_wakeup = false;
    sg_manual_talk = false;

    return rt;
}

static OPERATE_RET __ai_mode_combo_deinit(void)
{
    tkl_kws_disable();

    tuya_ai_input_stop();

    sg_mode_cur_state = AI_MODE_STATE_INVALID;
    sg_manual_talk = false;

    return OPRT_OK;
}

static OPERATE_RET __ai_mode_combo_task(void *args)
{
    (void)args;

    if (sg_mode_cur_state == sg_mode_set_state) {
        return OPRT_OK;
    }

    switch (sg_mode_set_state) {
    case AI_MODE_STATE_IDLE:
        __ai_mode_enter_idle();
        break;
    case AI_MODE_STATE_LISTEN:
        __ai_mode_enter_listen();
        break;
    case AI_MODE_STATE_UPLOAD:
        __ai_mode_enter_upload();
        break;
    case AI_MODE_STATE_THINK:
        __ai_mode_enter_think();
        break;
    case AI_MODE_STATE_SPEAK:
        __ai_mode_enter_speak();
        break;
    default:
        break;
    }

    sg_mode_cur_state = sg_mode_set_state;

    ai_user_event_notify(AI_USER_EVT_MODE_STATE_UPDATE, (void *)sg_mode_cur_state);

    return OPRT_OK;
}

static OPERATE_RET __ai_mode_combo_handle_event(AI_NOTIFY_EVENT_T *event)
{
    TUYA_CHECK_NULL_RETURN(event, OPRT_INVALID_PARM);

    if (event->type != AI_USER_EVT_MIC_DATA && event->type != AI_USER_EVT_TTS_DATA) {
        PR_DEBUG("[====ai_combo] event type: %d", event->type);
    }

    switch (event->type) {
    case AI_USER_EVT_ASR_EMPTY:
    case AI_USER_EVT_ASR_ERROR:
        MODE_STATE_CHANGE(sg_mode_set_state, AI_MODE_STATE_LISTEN);
        break;
    case AI_USER_EVT_ASR_OK:
        MODE_STATE_CHANGE(sg_mode_set_state, AI_MODE_STATE_THINK);
        break;
    case AI_USER_EVT_TTS_PRE:
        MODE_STATE_CHANGE(sg_mode_set_state, AI_MODE_STATE_SPEAK);
        break;
    case AI_USER_EVT_EXIT:
        MODE_STATE_CHANGE(sg_mode_set_state, AI_MODE_STATE_IDLE);
        break;
    case AI_USER_EVT_PLAY_CTL_END:
    case AI_USER_EVT_PLAY_END:
        MODE_STATE_CHANGE(sg_mode_set_state, (sg_is_wakeup == true) ? AI_MODE_STATE_LISTEN : AI_MODE_STATE_IDLE);
        break;
    default:
        break;
    }

    return OPRT_OK;
}

static AI_MODE_STATE_E __ai_mode_combo_get_state(void)
{
    return sg_mode_set_state;
}

static OPERATE_RET __ai_mode_combo_client_run(void *data)
{
    (void)data;

    PR_NOTICE("combo mode: connected to server");

    MODE_STATE_CHANGE(sg_mode_set_state, AI_MODE_STATE_IDLE);

    return OPRT_OK;
}

static OPERATE_RET __ai_mode_combo_vad_change(AI_AUDIO_VAD_STATE_E vad_flag)
{
    /* During long-press PTT, ignore VAD so it does not start/stop input. */
    if (sg_manual_talk) {
        return OPRT_OK;
    }

    if (false == sg_is_wakeup) {
        return OPRT_OK;
    }

    PR_DEBUG("[====ai_combo] vad: [%d]", vad_flag);

    if (AI_AUDIO_VAD_START == vad_flag) {
        tuya_ai_agent_set_scode(AI_AGENT_SCODE_DEFAULT);
        tuya_ai_input_start(false);
    } else {
        tuya_ai_input_stop();
    }

    return OPRT_OK;
}

#if defined(ENABLE_BUTTON) && (ENABLE_BUTTON == 1)
static OPERATE_RET __ai_mode_combo_handle_key(TDL_BUTTON_TOUCH_EVENT_E event, void *arg)
{
    OPERATE_RET rt = OPRT_OK;

    (void)arg;

    switch (event) {
    case TDL_BUTTON_PRESS_SINGLE_CLICK: {
        PR_NOTICE("[====ai_combo] single click, state=%s", ai_get_mode_state_str(sg_mode_set_state));

        /* Already listening: the click means "we're done", not "wake again". */
        if (AI_MODE_STATE_LISTEN == sg_mode_set_state && false == sg_manual_talk) {
            __ai_mode_exit_chat();
            break;
        }

        /* Otherwise interrupt whatever is playing or thinking and wake up. */
        ai_audio_player_stop(AI_AUDIO_PLAYER_ALL);
        ai_audio_input_reset();
        tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);

        ai_audio_player_alert(AI_AUDIO_ALERT_WAKEUP);

        MODE_STATE_CHANGE(sg_mode_set_state, AI_MODE_STATE_LISTEN);
        sg_is_wakeup = true;
        sg_manual_talk = false;
    } break;

    case TDL_BUTTON_LONG_PRESS_START: {
        /* Same "we're done" intent as the click above. The press has to land
         * here as well because ai_chat_main.c arms the button with a 400 ms
         * long-press threshold: a deliberate press on a stiff panel key easily
         * runs past that, the state machine goes straight to LONG_HOLD and no
         * single-click is ever emitted on release. Already listening means
         * there is nothing for PTT to add, so treat it as the exit. */
        if (AI_MODE_STATE_LISTEN == sg_mode_set_state && false == sg_manual_talk) {
            __ai_mode_exit_chat();
            break;
        }

        /* Long-press PTT: interrupt and start streaming immediately */
        ai_audio_player_stop(AI_AUDIO_PLAYER_ALL);
        ai_audio_input_reset();
        tuya_ai_agent_event(AI_EVENT_CHAT_BREAK, 0);

        sg_manual_talk = true;
        sg_is_wakeup = true;
        ai_audio_input_wakeup_set(true);
        tuya_ai_agent_set_scode(AI_AGENT_SCODE_DEFAULT);
        tuya_ai_input_start(false);
        MODE_STATE_CHANGE(sg_mode_set_state, AI_MODE_STATE_LISTEN);
        PR_NOTICE("[====ai_combo] long-press PTT start");
    } break;

    case TDL_BUTTON_PRESS_UP: {
        /* Release: stop upload -> ASR -> THINK -> SPEAK */
        if (sg_manual_talk) {
            sg_manual_talk = false;
            tuya_ai_input_stop();
            tal_sw_timer_start(sg_enter_idle_timer, sg_wakeup_time_ms, TAL_TIMER_ONCE);
            PR_NOTICE("[====ai_combo] long-press PTT end -> upload");
        }
    } break;

    default:
        break;
    }

    return rt;
}
#endif

OPERATE_RET ai_mode_combo_register(void)
{
    OPERATE_RET rt = OPRT_OK;
    AI_MODE_HANDLE_T handle;

    memset(&handle, 0, sizeof(AI_MODE_HANDLE_T));

    handle.name = "combo";
    handle.init = __ai_mode_combo_init;
    handle.deinit = __ai_mode_combo_deinit;
    handle.task = __ai_mode_combo_task;
    handle.handle_event = __ai_mode_combo_handle_event;
    handle.get_state = __ai_mode_combo_get_state;
    handle.client_run = __ai_mode_combo_client_run;
    handle.vad_change = __ai_mode_combo_vad_change;

#if defined(ENABLE_BUTTON) && (ENABLE_BUTTON == 1)
    handle.handle_key = __ai_mode_combo_handle_key;
#endif

    TUYA_CALL_ERR_RETURN(ai_mode_register(AI_CHAT_MODE_COMBO, &handle));

    PR_NOTICE("ai combo mode registered (mode=%d)", (int)AI_CHAT_MODE_COMBO);
    return rt;
}
