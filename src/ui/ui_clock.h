#ifndef __UI_CLOCK_H__
#define __UI_CLOCK_H__

#include "lvgl.h"
#include <stdbool.h>

void clock_page_create(lv_obj_t *parent);
void clock_page_destroy(void);

/** True while the clock page has taken over the D-pad for alarm/timer setup. */
bool clock_is_editing(void);

/**
 * Button routing from ui_buttons.c. @p btn_idx follows the BMO panel order:
 * 0=UP .. 8=GREEN. Called on both edges; only the press edge acts.
 *
 * @return true when the clock page consumed the event.
 */
bool clock_btn_event(int btn_idx, bool pressed);

#endif
