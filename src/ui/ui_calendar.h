#ifndef __UI_CALENDAR_H__
#define __UI_CALENDAR_H__

#include "lvgl.h"
#include "feishu_cal.h"

void calendar_page_create(lv_obj_t *parent);
void calendar_page_destroy(void);
void calendar_page_on_press(void);
void calendar_page_update(const FEISHU_CAL_DATA_T *data);

#endif
