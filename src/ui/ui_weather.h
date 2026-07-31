#ifndef __UI_WEATHER_H__
#define __UI_WEATHER_H__

#include "lvgl.h"
#include "tuya_weather.h"

typedef struct {
    bool     valid;
    int      temp;
    int      hi;
    int      lo;
    int      humi;
    char     condition[16];
    char     forecast[3][32];
} UI_WEATHER_DATA_T;

void weather_page_create(lv_obj_t *parent);
void weather_page_destroy(void);
void weather_page_on_press(void);
void weather_page_update(const UI_WEATHER_DATA_T *data);

#endif
