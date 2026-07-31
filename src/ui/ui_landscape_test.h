/**
 * @file ui_landscape_test.h
 * @brief Optional boot-time corner markers to verify landscape rotation.
 */

#ifndef UI_LANDSCAPE_TEST_H
#define UI_LANDSCAPE_TEST_H

#include "lvgl.h"

/**
 * Show digits 1-4 in the four corners for @p hold_ms, then delete the overlay.
 * No-op when UI_LANDSCAPE_TEST is disabled.
 */
void ui_landscape_test_show(lv_obj_t *parent, uint32_t hold_ms);

#endif
