/**
 * @file motion_engine.c
 * @brief Keyframe player for dual-arm BMO gestures (20 ms tick).
 */

#include "motion_engine.h"
#include "servo_pwm.h"
#include "tal_api.h"
#include <string.h>

#define MOTION_TICK_MS  20

typedef struct {
    uint16_t t_ms;
    uint8_t left_deg;
    uint8_t right_deg;
} MOTION_KF_T;

typedef struct {
    const char *name;
    MOTION_ID_E id;
    const MOTION_KF_T *kfs;
    uint8_t kf_cnt;
} MOTION_SEQ_T;

static const MOTION_KF_T sg_kf_neutral[] = {
    {0, 90, 90},
};
static const MOTION_KF_T sg_kf_wave_r[] = {
    {0, 90, 90}, {200, 90, 140}, {500, 90, 60}, {800, 90, 90},
};
static const MOTION_KF_T sg_kf_wave_l[] = {
    {0, 90, 90}, {200, 140, 90}, {500, 60, 90}, {800, 90, 90},
};
static const MOTION_KF_T sg_kf_cheer[] = {
    {0, 90, 90}, {250, 130, 130}, {600, 90, 90},
};
static const MOTION_KF_T sg_kf_droop[] = {
    {0, 90, 90}, {400, 45, 45},
};
static const MOTION_KF_T sg_kf_think[] = {
    {0, 90, 90}, {300, 110, 70},
};
static const MOTION_KF_T sg_kf_dance[] = {
    {0, 90, 90}, {200, 120, 60}, {400, 60, 120}, {600, 120, 60}, {800, 90, 90},
};
static const MOTION_KF_T sg_kf_sway[] = {
    {0, 80, 100}, {400, 100, 80}, {800, 80, 100},
};

static const MOTION_SEQ_T sg_sequences[] = {
    {"neutral", MOTION_NEUTRAL, sg_kf_neutral, 1},
    {"wave_left", MOTION_WAVE_LEFT, sg_kf_wave_l, 4},
    {"wave_right", MOTION_WAVE_RIGHT, sg_kf_wave_r, 4},
    {"cheer_both", MOTION_CHEER_BOTH, sg_kf_cheer, 3},
    {"droop_sad", MOTION_DROOP_SAD, sg_kf_droop, 2},
    {"think_pose", MOTION_THINK_POSE, sg_kf_think, 2},
    {"dance", MOTION_DANCE, sg_kf_dance, 5},
    {"idle_sway", MOTION_IDLE_SWAY, sg_kf_sway, 3},
};

static THREAD_HANDLE sg_motion_thread = NULL;
static volatile MOTION_ID_E sg_pending = MOTION_NONE;
static volatile bool sg_running = false;

static const MOTION_SEQ_T *__find_seq(MOTION_ID_E id)
{
    for (size_t i = 0; i < sizeof(sg_sequences) / sizeof(sg_sequences[0]); i++) {
        if (sg_sequences[i].id == id) {
            return &sg_sequences[i];
        }
    }
    return NULL;
}

static const MOTION_SEQ_T *__find_seq_by_name(const char *name)
{
    if (!name) {
        return NULL;
    }
    for (size_t i = 0; i < sizeof(sg_sequences) / sizeof(sg_sequences[0]); i++) {
        if (0 == strcmp(sg_sequences[i].name, name)) {
            return &sg_sequences[i];
        }
    }
    return NULL;
}

static void __play_seq(const MOTION_SEQ_T *seq)
{
    uint16_t elapsed = 0;
    uint8_t li = 0;

    if (!seq || seq->kf_cnt == 0) {
        return;
    }

    sg_running = true;
    servo_set_angle(SERVO_ARM_LEFT, seq->kfs[0].left_deg);
    servo_set_angle(SERVO_ARM_RIGHT, seq->kfs[0].right_deg);

    while (elapsed < seq->kfs[seq->kf_cnt - 1].t_ms) {
        while (li + 1 < seq->kf_cnt && elapsed >= seq->kfs[li + 1].t_ms) {
            li++;
        }
        servo_set_angle(SERVO_ARM_LEFT, seq->kfs[li].left_deg);
        servo_set_angle(SERVO_ARM_RIGHT, seq->kfs[li].right_deg);
        tal_system_sleep(MOTION_TICK_MS);
        elapsed += MOTION_TICK_MS;
    }

    servo_set_angle(SERVO_ARM_LEFT, seq->kfs[seq->kf_cnt - 1].left_deg);
    servo_set_angle(SERVO_ARM_RIGHT, seq->kfs[seq->kf_cnt - 1].right_deg);
    sg_running = false;
}

static void __motion_thread(void *arg)
{
    (void)arg;

    while (1) {
        MOTION_ID_E id = sg_pending;
        if (id != MOTION_NONE && !sg_running) {
            sg_pending = MOTION_NONE;
            const MOTION_SEQ_T *seq = __find_seq(id);
            if (seq) {
                PR_NOTICE("[motion] play %s", seq->name);
                __play_seq(seq);
            }
        }
        tal_system_sleep(30);
    }
}

OPERATE_RET motion_engine_init(void)
{
#if defined(ENABLE_MOTION_ENGINE) && (ENABLE_MOTION_ENGINE == 1)
    OPERATE_RET rt = OPRT_OK;
    THREAD_CFG_T cfg = {
        .stackDepth = 4 * 1024,
        .priority = THREAD_PRIO_2,
        .thrdname = "motion_eng",
    };

    TUYA_CALL_ERR_RETURN(servo_pwm_init());
    return tal_thread_create_and_start(&sg_motion_thread, NULL, NULL, __motion_thread, NULL, &cfg);
#else
    return OPRT_OK;
#endif
}

void motion_engine_play(MOTION_ID_E id)
{
    if (id == MOTION_NONE || sg_running) {
        return;
    }
    sg_pending = id;
}

void motion_engine_play_by_name(const char *name)
{
    const MOTION_SEQ_T *seq = __find_seq_by_name(name);
    if (seq) {
        motion_engine_play(seq->id);
    }
}

void motion_engine_play_random(void)
{
    static const MOTION_ID_E choices[] = {
        MOTION_WAVE_LEFT, MOTION_WAVE_RIGHT, MOTION_CHEER_BOTH, MOTION_DANCE,
    };
    uint32_t idx = tal_system_get_millisecond() % (sizeof(choices) / sizeof(choices[0]));
    motion_engine_play(choices[idx]);
}

void motion_on_expression(uint8_t motion_id)
{
    if (motion_id != MOTION_NONE) {
        motion_engine_play((MOTION_ID_E)motion_id);
    }
}

void motion_on_avatar_state(int avatar_state)
{
    /* AVATAR_IDLE=0 LISTEN=1 THINK=2 SPEAK=3 from ui_avatar.h */
    switch (avatar_state) {
    case 1: /* LISTEN */
        servo_set_angle(SERVO_ARM_LEFT, 100);
        servo_set_angle(SERVO_ARM_RIGHT, 100);
        break;
    case 2: /* THINK */
        motion_engine_play(MOTION_THINK_POSE);
        break;
    case 3: /* SPEAK */
        motion_engine_play(MOTION_IDLE_SWAY);
        break;
    default:
        motion_engine_play(MOTION_NEUTRAL);
        break;
    }
}
