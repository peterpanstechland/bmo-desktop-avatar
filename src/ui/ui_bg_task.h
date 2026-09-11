#ifndef __UI_BG_TASK_H__
#define __UI_BG_TASK_H__

#include "tuya_cloud_types.h"
#include "rss_feed.h"
#include "feishu_cal.h"
#include <stdbool.h>

OPERATE_RET ui_bg_task_init(void);

/** Pull weather, calendar and RSS again right now (centre key, page press, after a
 *  calendar write). Safe from any thread; the fetch runs on the bg task. */
void        ui_bg_task_request_refresh(void);

void        ui_bg_task_push_cached_pages(void);
void        ui_bg_task_push_cached_pages_unlocked(void);

/** Copy the latest RSS cache for MCP / voice readout. @return false if empty. */
bool        ui_bg_task_copy_rss(RSS_FEED_DATA_T *out);

/** Copy the latest Feishu calendar cache for MCP / voice readout.
 *  @return false if not synced or empty. */
bool        ui_bg_task_copy_calendar(FEISHU_CAL_DATA_T *out);

#endif
