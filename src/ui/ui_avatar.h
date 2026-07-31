#ifndef __UI_AVATAR_H__
#define __UI_AVATAR_H__

#include "lvgl.h"
#include "ai_ui_manage.h"
#include <stdbool.h>

typedef enum {
    AVATAR_IDLE = 0,
    AVATAR_LISTEN,
    AVATAR_THINK,
    AVATAR_SPEAK,
} AVATAR_STATE_E;

void avatar_page_create(lv_obj_t *parent);
void avatar_page_destroy(void);
void avatar_set_state(AVATAR_STATE_E st);
void avatar_set_emotion(const char *emo);
void avatar_set_caption(const char *txt);
void avatar_set_status_text(const char *txt);
void avatar_set_wifi(AI_UI_WIFI_STATUS_E status);

/** Unified expression entry (lookup table + apply). Returns true if matched. */
bool avatar_express(const char *name);

/** Set expression and ignore cloud emotion overrides for hold_ms. */
void avatar_express_hold(const char *name, uint32_t hold_ms);

/** True while MCP/cloud emotion override is blocked. */
bool avatar_emotion_hold_active(void);

#endif
