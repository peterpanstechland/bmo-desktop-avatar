#ifndef __FEISHU_CAL_H__
#define __FEISHU_CAL_H__

#include "tuya_cloud_types.h"

/*
 * Credentials come from Kconfig (CONFIG_FEISHU_APP_ID / _APP_SECRET / _CAL_ID)
 * and land in tuya_kconfig.h, so they live in the same gitignored config files
 * as the Tuya license. See docs/feishu-calendar.md. The fallbacks below only
 * matter for builds that skip Kconfig (the PC simulator); an empty app_id
 * disables the calendar sync.
 */
#ifndef FEISHU_APP_ID
#define FEISHU_APP_ID     ""
#endif
#ifndef FEISHU_APP_SECRET
#define FEISHU_APP_SECRET ""
#endif

/* Optional. Leave empty to auto-pick the calendar shared with the bot; set it
 * when the app can see several calendars and the wrong one wins. */
#ifndef FEISHU_CAL_ID
#define FEISHU_CAL_ID ""
#endif

#define FEISHU_CAL_MAX_EVENTS 10

typedef struct {
    char    time_str[16];
    char    title[64];
    uint8_t mon;  /* 1-12, 0 when the event date could not be parsed */
    uint8_t mday; /* 1-31, 0 when the event date could not be parsed */
} FEISHU_CAL_EVENT_T;

typedef struct {
    bool               valid;
    int                count;
    FEISHU_CAL_EVENT_T events[FEISHU_CAL_MAX_EVENTS];
} FEISHU_CAL_DATA_T;

/** Creates the lock shared by the bg task and the MCP tool. Call once before
 *  either of them runs. */
OPERATE_RET feishu_cal_init(void);

/**
 * Pull the next 4 days of events. Blocks on HTTPS.
 *
 * @return OPRT_OK on success, OPRT_NOT_SUPPORTED when no app_id is configured
 *         (nothing to retry), any other error is transient (no network, clock
 *         not synced, HTTP/API failure) and worth retrying.
 */
OPERATE_RET feishu_cal_fetch(FEISHU_CAL_DATA_T *out);

/**
 * Create an event on the synced calendar. day_offset counts days forward from
 * today in local time (0 = today), hour/minute are the local start. Fills
 * `when` with a "MM-DD HH:MM" echo of what was booked.
 *
 * Blocks on HTTPS, so keep it off the LVGL thread. The bot needs writer on the
 * calendar; reader access fails with a permission error.
 */
OPERATE_RET feishu_cal_add_event(const char *title, int day_offset, int hour, int minute,
                                 int duration_min, char *when, size_t when_size);

#endif
