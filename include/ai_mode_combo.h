/**
 * @file ai_mode_combo.h
 * @brief Combo chat mode: wake word + single click + long-press PTT.
 */

#ifndef __AI_MODE_COMBO_H__
#define __AI_MODE_COMBO_H__

#include "tuya_cloud_types.h"
#include "ai_manage_mode.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Custom mode id starting at AI_CHAT_MODE_CUSTOM_START (0x100). */
#define AI_CHAT_MODE_COMBO ((AI_CHAT_MODE_E)AI_CHAT_MODE_CUSTOM_START)

/**
 * @brief Register combo chat mode into the mode manager.
 * Must be called before ai_chat_init().
 */
OPERATE_RET ai_mode_combo_register(void);

#ifdef __cplusplus
}
#endif

#endif /* __AI_MODE_COMBO_H__ */
