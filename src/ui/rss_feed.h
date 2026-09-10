#ifndef __RSS_FEED_H__
#define __RSS_FEED_H__

#include "tuya_cloud_types.h"
#include <stdbool.h>
#include <stdint.h>

#define RSS_SOURCE_CNT     8
#define RSS_MAX_ITEMS      32
#define RSS_TITLE_MAX      96
#define RSS_SUMMARY_MAX    640 /* scrollable detail; keep caches out of SRAM */
#define RSS_SOURCE_NAME_MAX 16

typedef struct {
    uint8_t source; /* index into the built-in source table */
    char    title[RSS_TITLE_MAX];
    char    summary[RSS_SUMMARY_MAX]; /* plain-text teaser; English body OK */
} RSS_ITEM_T;

typedef struct {
    bool       valid;
    int        count;
    RSS_ITEM_T items[RSS_MAX_ITEMS];
} RSS_FEED_DATA_T;

/** Built-in Maker / embedded news sources (order matches the UI list). */
const char *rss_source_name(int idx);

/**
 * Pull every configured feed and keep a few titles (+ summary) from each.
 * Blocks on HTTPS — must run on the background task, not LVGL.
 *
 * @return OPRT_OK when at least one item landed, OPRT_COM_ERROR on total failure.
 */
OPERATE_RET rss_feed_fetch(RSS_FEED_DATA_T *out);

#endif
