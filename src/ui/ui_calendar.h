#ifndef __UI_CALENDAR_H__
#define __UI_CALENDAR_H__

#include "lvgl.h"
#include "feishu_cal.h"
#include <stdbool.h>

void calendar_page_create(lv_obj_t *parent);
void calendar_page_destroy(void);
void calendar_page_on_press(void);
void calendar_page_update(const FEISHU_CAL_DATA_T *data);

/** True while the calendar page has taken over the D-pad for day browsing. */
bool calendar_is_browsing(void);

/**
 * Button routing from ui_buttons.c. @p btn_idx follows the BMO panel order:
 * 0=UP .. 8=GREEN. Called on both edges; only the press edge acts.
 *
 * @return true when the calendar page consumed the event; false lets the caller
 *         run the button's normal action (page nav, volume, ...).
 */
bool calendar_btn_event(int btn_idx, bool pressed);

#endif
