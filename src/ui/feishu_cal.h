#ifndef __FEISHU_CAL_H__
#define __FEISHU_CAL_H__

#include "tuya_cloud_types.h"

/* Replace with your Feishu app credentials before use */
#ifndef FEISHU_APP_ID
#define FEISHU_APP_ID     "cli_xxxxxxxxxx"
#endif
#ifndef FEISHU_APP_SECRET
#define FEISHU_APP_SECRET "xxxxxxxxxxxxxxxxxxxxxxxx"
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

OPERATE_RET feishu_cal_fetch(FEISHU_CAL_DATA_T *out);
void        feishu_cal_request_refresh(void);
void        feishu_cal_bind_refresh_sem(SEM_HANDLE sem);
bool        feishu_cal_take_refresh_request(void);

#endif
