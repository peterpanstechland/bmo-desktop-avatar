#ifndef __UI_CHAT_OVERLAY_H__
#define __UI_CHAT_OVERLAY_H__

#include "lvgl.h"

void chat_overlay_init(void);
void chat_overlay_update(const char *state_text, const char *caption);

#endif
