/**
 * @file app_chat_bot.h
 * @brief app_chat_bot module is used to
 * @version 0.1
 * @date 2025-03-25
 */

#ifndef __APP_CHAT_BOT_H__
#define __APP_CHAT_BOT_H__

#include "tuya_cloud_types.h"
#include "ai_chat_main.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
********************function declaration********************
***********************************************************/
OPERATE_RET app_chat_bot_init(void);

/**
 * Volume ceiling in effect (BMO_MAX_VOLUME). Both servos starting at once
 * while the speaker is near full output browns out the 5 V rail, so every
 * volume path is capped instead of allowing the full 0-100.
 */
int app_volume_max(void);

/** Set the volume, clamped to [0, app_volume_max()]. */
OPERATE_RET app_volume_set(int vol);

#ifdef __cplusplus
}
#endif

#endif /* __APP_CHAT_BOT_H__ */
