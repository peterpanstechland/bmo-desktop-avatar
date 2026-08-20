/**
 * @file servo_pwm.c
 * @brief SG90/MG90S PWM driver via tkl_pwm (50 Hz, 500-2500 us).
 */

#include "servo_pwm.h"
#include "tal_api.h"
#include "tkl_pwm.h"
#include "tkl_gpio.h"

#define SERVO_FREQ_HZ     50
#define SERVO_PERIOD_US   20000
#define SERVO_MIN_US      500
#define SERVO_MAX_US      2500
#define SERVO_NEUTRAL_DEG   90

typedef struct {
    TUYA_PWM_NUM_E pwm_id;
    uint8_t angle;
    bool inited;
} SERVO_CH_T;

static SERVO_CH_T sg_servos[2];

/* TUYA_PWM_NUM_* to pin, per ty_to_bk_pwm() in tkl_pwm.c combined with
 * GPIO_PWM_MAP_TABLE. NUM_* is not the hardware PWM index: NUM_1 is PWM4/P24. */
static const uint8_t sg_pwm_gpio[] = {18, 24, 32, 34, 36, 19, 8, 9, 25, 33, 35};

/* 0.5-2.5 ms of the 20 ms frame, expressed in 1/10000 of the period. */
static uint32_t __angle_duty(uint8_t angle)
{
    if (angle > 180) {
        angle = 180;
    }
    return (uint32_t)((0.5 + (double)angle / 180.0 * 2.0) * 10000.0 / 20.0);
}

/* The SDK GPIO table hands these pins to the RGB LCD with time-sharing
 * multiplex disabled. Claiming the pin as plain GPIO first makes the
 * gpio_dev_map() inside bk_pwm_init() stick on a cold boot. */
static void __pin_claim(TUYA_PWM_NUM_E num)
{
    TUYA_GPIO_BASE_CFG_T cfg = {
        .direct = TUYA_GPIO_OUTPUT,
        .mode = TUYA_GPIO_PUSH_PULL,
        .level = TUYA_GPIO_LEVEL_LOW,
    };

    if ((size_t)num >= sizeof(sg_pwm_gpio) / sizeof(sg_pwm_gpio[0])) {
        return;
    }
    tkl_gpio_init((TUYA_GPIO_NUM_E)sg_pwm_gpio[num], &cfg);
    tkl_gpio_write((TUYA_GPIO_NUM_E)sg_pwm_gpio[num], TUYA_GPIO_LEVEL_LOW);
}

static OPERATE_RET __servo_apply(SERVO_ARM_E arm, uint8_t angle)
{
    if (arm > SERVO_ARM_RIGHT || !sg_servos[arm].inited) {
        return OPRT_INVALID_PARM;
    }

    if (angle > 180) {
        angle = 180;
    }
    sg_servos[arm].angle = angle;
    tkl_pwm_duty_set(sg_servos[arm].pwm_id, __angle_duty(angle));
    return tkl_pwm_start(sg_servos[arm].pwm_id);
}

OPERATE_RET servo_pwm_init(void)
{
    /* POSITIVE puts the duty window on the high side, which is what a servo
     * expects. NEGATIVE inverts it, turning a 1.5 ms request into an 18.5 ms
     * pulse that is far outside the servo's range.
     *
     * The init duty must also be non-zero: at duty 0 the BK driver forces the
     * channel into flip mode 4 (see pwm_adjust_init_signal_via_duty), and a
     * later duty update does not lift it back out. */
    TUYA_PWM_BASE_CFG_T cfg = {
        .duty = __angle_duty(SERVO_NEUTRAL_DEG),
        .frequency = SERVO_FREQ_HZ,
        .polarity = TUYA_PWM_POSITIVE,
    };

#if defined(ENABLE_MOTION_ENGINE) && (ENABLE_MOTION_ENGINE == 1)
    OPERATE_RET rt = OPRT_OK;
    sg_servos[SERVO_ARM_LEFT].pwm_id = (TUYA_PWM_NUM_E)SERVO_LEFT_PWM;
    sg_servos[SERVO_ARM_RIGHT].pwm_id = (TUYA_PWM_NUM_E)SERVO_RIGHT_PWM;

    for (int i = 0; i < 2; i++) {
        __pin_claim(sg_servos[i].pwm_id);

        rt = tkl_pwm_init(sg_servos[i].pwm_id, &cfg);
        if (rt != OPRT_OK) {
            PR_ERR("[servo] pwm init ch%d failed: %d", i, rt);
            return rt;
        }
        sg_servos[i].inited = true;
        sg_servos[i].angle = SERVO_NEUTRAL_DEG;

        rt = tkl_pwm_start(sg_servos[i].pwm_id);
        if (rt != OPRT_OK) {
            PR_ERR("[servo] pwm start ch%d failed: %d", i, rt);
            return rt;
        }
    }
    PR_NOTICE("[servo] dual arm init ok (L=PWM%d/P%d R=PWM%d/P%d, positive, duty=%u)",
              (int)SERVO_LEFT_PWM, (int)sg_pwm_gpio[SERVO_LEFT_PWM], (int)SERVO_RIGHT_PWM,
              (int)sg_pwm_gpio[SERVO_RIGHT_PWM], __angle_duty(SERVO_NEUTRAL_DEG));
#else
    (void)cfg;
#endif
    return OPRT_OK;
}

OPERATE_RET servo_set_angle(SERVO_ARM_E arm, uint8_t angle)
{
    return __servo_apply(arm, angle);
}

uint8_t servo_get_angle(SERVO_ARM_E arm)
{
    if (arm > SERVO_ARM_RIGHT) {
        return SERVO_NEUTRAL_DEG;
    }
    return sg_servos[arm].inited ? sg_servos[arm].angle : SERVO_NEUTRAL_DEG;
}

OPERATE_RET servo_move_smooth(SERVO_ARM_E arm, uint8_t target, uint32_t duration_ms)
{
    uint8_t start;
    uint32_t steps;
    uint32_t i;

    if (arm > SERVO_ARM_RIGHT || !sg_servos[arm].inited) {
        return OPRT_INVALID_PARM;
    }

    start = sg_servos[arm].angle;
    if (duration_ms == 0) {
        return __servo_apply(arm, target);
    }

    steps = duration_ms / 20;
    if (steps == 0) {
        steps = 1;
    }

    for (i = 0; i <= steps; i++) {
        uint8_t ang = (uint8_t)(start + ((int)target - (int)start) * (int)i / (int)steps);
        __servo_apply(arm, ang);
        tal_system_sleep(20);
    }
    return OPRT_OK;
}
