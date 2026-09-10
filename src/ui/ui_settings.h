#ifndef __UI_SETTINGS_H__
#define __UI_SETTINGS_H__

#include "lvgl.h"
#include <stdbool.h>

void settings_page_create(lv_obj_t *parent);
void settings_page_destroy(void);
void settings_page_on_press(void);

bool settings_is_browsing(void);
bool settings_btn_event(int btn_idx, bool pressed);

#endif
