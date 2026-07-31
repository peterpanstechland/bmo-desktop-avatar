#ifndef __SERVO_PWM_H__
#define __SERVO_PWM_H__

#include "tuya_cloud_types.h"

typedef enum {
    SERVO_ARM_LEFT = 0,
    SERVO_ARM_RIGHT,
} SERVO_ARM_E;

OPERATE_RET servo_pwm_init(void);
OPERATE_RET servo_set_angle(SERVO_ARM_E arm, uint8_t angle);
OPERATE_RET servo_move_smooth(SERVO_ARM_E arm, uint8_t target, uint32_t duration_ms);

#endif
