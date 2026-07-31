#ifndef __UI_BG_TASK_H__
#define __UI_BG_TASK_H__

#include "tuya_cloud_types.h"

OPERATE_RET ui_bg_task_init(void);
void        ui_bg_task_request_weather_refresh(void);

/** Pull weather and calendar again right now (center key / manual refresh). */
void        ui_bg_task_request_full_refresh(void);

void        ui_bg_task_push_cached_pages(void);
void        ui_bg_task_push_cached_pages_unlocked(void);

#endif
