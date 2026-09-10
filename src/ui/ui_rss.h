#ifndef __UI_RSS_H__
#define __UI_RSS_H__

#include "lvgl.h"
#include "rss_feed.h"
#include <stdbool.h>

void rss_page_create(lv_obj_t *parent);
void rss_page_destroy(void);
void rss_page_on_press(void);
void rss_page_update(const RSS_FEED_DATA_T *data);

bool rss_is_browsing(void);
bool rss_btn_event(int btn_idx, bool pressed);

#endif
