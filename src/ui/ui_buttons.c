/**
 * @file ui_buttons.c
 * @brief BMO 6-key panel driven by a self-contained GPIO poll loop.
 *
 * The tdl_button framework is bypassed here on purpose. Panel keys may be wired
 * to GND or to 3V3, with or without on-board resistors, and a fixed active level
 * cannot cover all of those. Instead each line is sampled twice per cycle, once
 * with the internal pull-up and once with the internal pull-down. The resulting
 * pair tells whether the line is floating or externally driven:
 *
 *   pull-up=1, pull-down=0 -> floating (switch open, no external resistor)
 *   pull-up=1, pull-down=1 -> externally driven high (shorted to 3V3)
 *   pull-up=0, pull-down=0 -> externally driven low  (shorted to GND)
 *
 * The signature seen at init is taken as the idle state, and any deviation from
 * it counts as a press. That works for switch-to-GND, switch-to-3V3, and for
 * breakout modules that carry their own pull-up or pull-down.
 */

#include "tal_api.h"
#include "tkl_gpio.h"
#include "ai_chat_main.h"
#include "ui_buttons.h"
#include "ui_page_mgr.h"
#include "ui_avatar.h"
#include "ui_bg_task.h"
#include "ui_popup.h"
#include "motion_engine.h"
#include "lv_vendor.h"

#define BMO_POLL_MS       20  /* one pull phase per tick, so a cycle is 40 ms */
#define BMO_STABLE_CYCLES 2   /* 80 ms of a steady signature counts as an edge */
#define BMO_STUCK_CYCLES  250 /* 10 s pressed means the idle baseline was wrong */
#define BMO_HEARTBEAT_CYC 125 /* 5 s */

#define BMO_SIG_LOW   0 /* 0,0 */
#define BMO_SIG_FLOAT 2 /* 1,0 */
#define BMO_SIG_HIGH  3 /* 1,1 */

typedef void (*BMO_BTN_ACTION_CB)(void);

typedef struct {
    const char *name;
    TUYA_GPIO_NUM_E pin;
    BMO_BTN_ACTION_CB action;
} BMO_BTN_HW_T;

typedef struct {
    uint8_t idle_sig;
    uint8_t sig;      /* signature being accumulated across the two phases */
    uint8_t last_sig; /* last completed signature */
    uint8_t match;    /* consecutive cycles reporting last_sig */
    uint8_t pressed;
    uint16_t held;
} BMO_BTN_ST_T;

#define BMO_VOL_DEFAULT 70

static int sg_vol_before_mute = 0; /* 0 means not muted */

static void __vol_apply(int vol)
{
    char buf[16];

    ai_chat_set_volume(vol);
    snprintf(buf, sizeof(buf), "Vol %d", vol);
    ui_popup_toast(buf);

    lv_vendor_disp_lock();
    avatar_set_status_text(buf);
    lv_vendor_disp_unlock();
}

static void __act_up(void)
{
    int vol = ai_chat_get_volume() + 10;
    if (vol > 100) {
        vol = 100;
    }
    sg_vol_before_mute = 0;
    __vol_apply(vol);
}

static void __act_down(void)
{
    int vol = ai_chat_get_volume() - 10;
    if (vol < 0) {
        vol = 0;
    }
    sg_vol_before_mute = 0;
    __vol_apply(vol);
}

static void __act_left(void)
{
    page_mgr_prev();
}

static void __act_right(void)
{
    page_mgr_next();
}

static void __act_mid(void)
{
    ui_bg_task_request_full_refresh();
    ui_popup_toast("Refreshing...");
}

static void __act_sw1(void)
{
    if (sg_vol_before_mute > 0) {
        int vol = sg_vol_before_mute;
        sg_vol_before_mute = 0;
        __vol_apply(vol);
        return;
    }

    sg_vol_before_mute = ai_chat_get_volume();
    if (sg_vol_before_mute <= 0) {
        sg_vol_before_mute = BMO_VOL_DEFAULT;
    }
    ai_chat_set_volume(0);
    ui_popup_toast("Muted");
}

static void __act_sw2(void)
{
    ui_popup_sysinfo_toggle();
}

static void __act_tri(void)
{
    page_mgr_goto(0);
}

static void __act_green(void)
{
    if (page_mgr_get_current() == 0) {
        motion_engine_play_random();
    } else {
        page_mgr_press();
    }
}

#if defined(ENABLE_BMO_BUTTONS) && (ENABLE_BMO_BUTTONS == 1)

static const BMO_BTN_HW_T sg_hw[] = {
    {"UP",    (TUYA_GPIO_NUM_E)BMO_BTN_UP,    __act_up},
    {"DOWN",  (TUYA_GPIO_NUM_E)BMO_BTN_DOWN,  __act_down},
    {"LEFT",  (TUYA_GPIO_NUM_E)BMO_BTN_LEFT,  __act_left},
    {"RIGHT", (TUYA_GPIO_NUM_E)BMO_BTN_RIGHT, __act_right},
    {"MID",   (TUYA_GPIO_NUM_E)BMO_BTN_MID,   __act_mid},
    {"SW1",   (TUYA_GPIO_NUM_E)BMO_BTN_SW1,   __act_sw1},
    {"SW2",   (TUYA_GPIO_NUM_E)BMO_BTN_SW2,   __act_sw2},
    {"TRI",   (TUYA_GPIO_NUM_E)BMO_BTN_TRI,   __act_tri},
    {"GREEN", (TUYA_GPIO_NUM_E)BMO_BTN_GREEN, __act_green},
};

#define BMO_BTN_CNT (sizeof(sg_hw) / sizeof(sg_hw[0]))

static BMO_BTN_ST_T sg_st[BMO_BTN_CNT];
static THREAD_HANDLE sg_btn_thread = NULL;

static void __pin_set_pull(TUYA_GPIO_NUM_E pin, uint8_t pull_up)
{
    TUYA_GPIO_BASE_CFG_T cfg = {
        .direct = TUYA_GPIO_INPUT,
        .mode = pull_up ? TUYA_GPIO_PULLUP : TUYA_GPIO_PULLDOWN,
        .level = pull_up ? TUYA_GPIO_LEVEL_HIGH : TUYA_GPIO_LEVEL_LOW,
    };
    tkl_gpio_init(pin, &cfg);
}

static uint8_t __pin_read(TUYA_GPIO_NUM_E pin)
{
    TUYA_GPIO_LEVEL_E lv = TUYA_GPIO_LEVEL_LOW;
    tkl_gpio_read(pin, &lv);
    return (lv == TUYA_GPIO_LEVEL_HIGH) ? 1 : 0;
}

static const char *__sig_name(uint8_t sig)
{
    switch (sig) {
    case BMO_SIG_HIGH:
        return "high";
    case BMO_SIG_LOW:
        return "low";
    case BMO_SIG_FLOAT:
        return "float";
    default:
        return "?";
    }
}

static uint8_t __measure_sig(TUYA_GPIO_NUM_E pin)
{
    uint8_t up, down;

    __pin_set_pull(pin, 1);
    tal_system_sleep(5);
    up = __pin_read(pin);
    __pin_set_pull(pin, 0);
    tal_system_sleep(5);
    down = __pin_read(pin);

    return (uint8_t)((up << 1) | down);
}

static void __on_cycle(int i)
{
    BMO_BTN_ST_T *st = &sg_st[i];
    uint8_t sig = st->sig & 0x03;
    uint8_t pressed;

    if (sig != st->last_sig) {
        st->last_sig = sig;
        st->match = 1;
        return;
    }
    if (st->match < BMO_STABLE_CYCLES) {
        st->match++;
        return;
    }

    pressed = (sig != st->idle_sig) ? 1 : 0;
    if (pressed != st->pressed) {
        st->pressed = pressed;
        st->held = 0;
        PR_NOTICE("[bmo-btn] %s P%d %s (%s)", sg_hw[i].name, (int)sg_hw[i].pin,
                  pressed ? "DOWN" : "UP", __sig_name(sig));
        if (pressed && sg_hw[i].action) {
            sg_hw[i].action();
        }
    } else if (pressed && ++st->held > BMO_STUCK_CYCLES) {
        PR_WARN("[bmo-btn] %s P%d held 10s, re-baselining idle to %s", sg_hw[i].name,
                (int)sg_hw[i].pin, __sig_name(sig));
        st->idle_sig = sig;
        st->pressed = 0;
        st->held = 0;
    }
}

static void __btn_thread(void *arg)
{
    uint8_t phase = 1; /* pull-up is applied on entry, so phase 0 is sampled first */
    uint32_t cycle = 0;
    (void)arg;

    for (;;) {
        phase ^= 1;

        for (int i = 0; i < (int)BMO_BTN_CNT; i++) {
            uint8_t bit = __pin_read(sg_hw[i].pin);
            if (phase == 0) {
                sg_st[i].sig = (uint8_t)(bit << 1);
            } else {
                sg_st[i].sig |= bit;
            }
            __pin_set_pull(sg_hw[i].pin, phase); /* phase 0 read -> pull down next */
        }

        if (phase == 1) {
            for (int i = 0; i < (int)BMO_BTN_CNT; i++) {
                __on_cycle(i);
            }
            if (++cycle % BMO_HEARTBEAT_CYC == 0) {
                char line[160];
                int n = 0;
                for (int i = 0; i < (int)BMO_BTN_CNT && n < (int)sizeof(line) - 1; i++) {
                    n += snprintf(line + n, sizeof(line) - n, "%s=%s ", sg_hw[i].name,
                                  __sig_name(sg_st[i].sig & 0x03));
                }
                PR_NOTICE("[bmo-btn] %s", line);
            }
        }

        tal_system_sleep(BMO_POLL_MS);
    }
}
#endif

OPERATE_RET ui_buttons_init(void)
{
#if defined(ENABLE_BMO_BUTTONS) && (ENABLE_BMO_BUTTONS == 1)
    THREAD_CFG_T cfg = {
        .stackDepth = 4 * 1024,
        .priority = THREAD_PRIO_2,
        .thrdname = "bmo_btn",
    };

    for (int i = 0; i < (int)BMO_BTN_CNT; i++) {
        sg_st[i].idle_sig = __measure_sig(sg_hw[i].pin);
        sg_st[i].last_sig = sg_st[i].idle_sig;
        sg_st[i].sig = sg_st[i].idle_sig;
        sg_st[i].match = BMO_STABLE_CYCLES;
        __pin_set_pull(sg_hw[i].pin, 1);
        PR_NOTICE("[bmo-btn] %s on P%d, idle=%s", sg_hw[i].name, (int)sg_hw[i].pin,
                  __sig_name(sg_st[i].idle_sig));
    }

    return tal_thread_create_and_start(&sg_btn_thread, NULL, NULL, __btn_thread, NULL, &cfg);
#else
    PR_WARN("[bmo] buttons disabled");
    return OPRT_OK;
#endif
}
