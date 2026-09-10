#ifndef __UI_ALARM_H__
#define __UI_ALARM_H__

#include "tuya_cloud_types.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ALARM_KIND_NONE = 0,
    ALARM_KIND_ALARM,
    ALARM_KIND_TIMER,
} ALARM_KIND_E;

typedef struct {
    bool     alarm_on;
    uint8_t  alarm_hour;   /* 0-23 */
    uint8_t  alarm_min;    /* 0-59 */
    bool     timer_on;
    uint32_t timer_remain_s;
    bool     ringing;      /* alarm or timer is sounding */
    ALARM_KIND_E ring_kind;
} ALARM_STATUS_T;

/** Start the 1 Hz tick. Call once from UI init after time service is up. */
OPERATE_RET ui_alarm_init(void);

void ui_alarm_get_status(ALARM_STATUS_T *out);

/**
 * Arm a one-shot-per-day alarm at local HH:MM. Fires the next time that
 * wall-clock minute arrives (today if still ahead, otherwise tomorrow).
 */
OPERATE_RET ui_alarm_set(uint8_t hour, uint8_t minute);

/** Disable the alarm without touching the last HH:MM (kept for the editor). */
OPERATE_RET ui_alarm_clear(void);

/** Start a countdown. @p total_s must be 1..3599 (up to 59:59). */
OPERATE_RET ui_alarm_timer_start(uint32_t total_s);

OPERATE_RET ui_alarm_timer_cancel(void);

/** Stop the ringing tone/toast. Safe to call when nothing is ringing. */
void ui_alarm_dismiss(void);

/** @return true if a ring was active and got cleared (caller may consume the key). */
bool ui_alarm_try_dismiss(void);

bool ui_alarm_is_ringing(void);

#endif
