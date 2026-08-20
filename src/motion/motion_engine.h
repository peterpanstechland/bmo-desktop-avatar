#ifndef __MOTION_ENGINE_H__
#define __MOTION_ENGINE_H__

#include "tuya_cloud_types.h"

typedef enum {
    MOTION_NONE = 0,
    MOTION_NEUTRAL,
    MOTION_WAVE_LEFT,
    MOTION_WAVE_RIGHT,
    MOTION_CHEER_BOTH,
    MOTION_DROOP_SAD,
    MOTION_THINK_POSE,
    MOTION_DANCE,
    MOTION_IDLE_SWAY,
    /* Keep new ids at the end: ui_avatar.c stores these values in its
     * expression table and passes them back as plain integers. */
    MOTION_LISTEN_POSE,
} MOTION_ID_E;

OPERATE_RET motion_engine_init(void);
void motion_engine_play(MOTION_ID_E id);
void motion_engine_play_by_name(const char *name);
void motion_engine_play_random(void);
void motion_on_expression(uint8_t motion_id);
void motion_on_avatar_state(int avatar_state);

#endif
