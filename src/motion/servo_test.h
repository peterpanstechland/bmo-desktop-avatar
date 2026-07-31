#ifndef __SERVO_TEST_H__
#define __SERVO_TEST_H__

#include "tuya_cloud_types.h"

/**
 * @brief Start the right-arm servo bring-up diagnostic in its own thread.
 *
 * No-op unless ENABLE_SERVO_TEST is set. Hands the servos back to
 * servo_pwm_init() when the sweep finishes.
 */
OPERATE_RET servo_test_start(void);

#endif
