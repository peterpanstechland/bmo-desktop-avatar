#ifndef __UI_PAGE_MGR_H__
#define __UI_PAGE_MGR_H__

#include <stdbool.h>
#include "lvgl.h"

typedef struct {
    const char *name;
    void (*create)(lv_obj_t *parent);
    void (*destroy)(void);
    void (*on_press)(void);
} UI_PAGE_T;

void page_mgr_init(void);
void page_mgr_next(void);
void page_mgr_prev(void);
void page_mgr_goto(int idx);
void page_mgr_press(void);
int  page_mgr_get_current(void);

/** @return page index 0..3, or -1 if name unknown */
int page_mgr_name_to_idx(const char *page);

/** @return true if @p text looks like a page-switch command and a page was selected */
bool page_mgr_try_asr_navigate(const char *text);

#endif
