#ifndef __UI_GAMES_H__
#define __UI_GAMES_H__

#include "lvgl.h"
#include <stdbool.h>

void games_page_create(lv_obj_t *parent);
void games_page_destroy(void);
void games_page_on_press(void);

/** True while snake or tetris is running (not on the picker menu). */
bool games_is_playing(void);

/**
 * Button routing from ui_buttons.c. @p btn_idx follows the BMO panel order:
 * 0=UP .. 8=GREEN. Called on both edges; only the press edge acts.
 *
 * @return true when the games page consumed the event; false lets the caller
 *         run the button's normal action (page nav, mute, sysinfo, ...).
 */
bool games_btn_event(int btn_idx, bool pressed);

#endif
