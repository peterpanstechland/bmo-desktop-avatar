/**
 * @file ui_i18n.h
 * @brief Runtime UI language (zh / en) with KV persistence.
 *
 * Cloud ASR/TTS language is not switched here — only local screen strings.
 */

#ifndef __UI_I18N_H__
#define __UI_I18N_H__

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BMO_LANG_ZH = 0,
    BMO_LANG_EN = 1,
} BMO_LANG_E;

typedef enum {
    BMO_STR_SETTINGS_TITLE = 0,
    BMO_STR_LANGUAGE,
    BMO_STR_LANG_ZH,
    BMO_STR_LANG_EN,
    BMO_STR_FIRMWARE,
    BMO_STR_CHECK_UPDATE,
    BMO_STR_UPDATE,
    BMO_STR_HINT_ENTER,
    BMO_STR_HINT_BROWSE,
    BMO_STR_EXIT_BROWSE,

    BMO_STR_OTA_LATEST,
    BMO_STR_OTA_FOUND,
    BMO_STR_OTA_OFFLINE,
    BMO_STR_OTA_BUSY,
    BMO_STR_OTA_CHECKING,

    BMO_STR_REFRESHING,
    BMO_STR_MUTED,
    BMO_STR_NETCFG_HOLD, /* "重置网络 %d\n松开取消" — use with snprintf */

    BMO_STR_RSS_TITLE,
    BMO_STR_RSS_EMPTY,
    BMO_STR_RSS_WAIT,
    BMO_STR_RSS_NO_TITLES,
    BMO_STR_RSS_HINT,
    BMO_STR_RSS_HINT_BROWSE,
    BMO_STR_RSS_HINT_DETAIL,
    BMO_STR_RSS_NO_SUMMARY,
    BMO_STR_RSS_ALL,

    BMO_STR_CAL_WAIT_SYNC,
    BMO_STR_CAL_EVENTS,
    BMO_STR_CAL_TODAY_EVENTS,
    BMO_STR_CAL_NO_DATA,
    BMO_STR_CAL_LOADING,
    BMO_STR_CAL_NONE_TODAY,
    BMO_STR_CAL_NONE_DAY,
    BMO_STR_CAL_HINT,
    BMO_STR_CAL_HINT_BROWSE,
    BMO_STR_CAL_PICK_DAY, /* use with snprintf: mon, day, week */
    BMO_STR_CAL_TODAY_LINE, /* mon, day, week */

    BMO_STR_CLK_WAIT_SYNC,
    BMO_STR_CLK_HINT,
    BMO_STR_CLK_HINT_MENU,
    BMO_STR_CLK_HINT_ALARM,
    BMO_STR_CLK_HINT_TIMER,
    BMO_STR_CLK_MENU_PICK,
    BMO_STR_CLK_ALARM,
    BMO_STR_CLK_TIMER,
    BMO_STR_WEEK_PREFIX, /* "星期" / "" then weekday name */

    BMO_STR_WX_TITLE,
    BMO_STR_WX_LOADING,
    BMO_STR_WX_NO_DATA,
    BMO_STR_WX_HI,
    BMO_STR_WX_LO,
    BMO_STR_WX_HUMI,

    BMO_STR_GAMES_TITLE,
    BMO_STR_GAMES_HINT,
    BMO_STR_GAMES_HINT_IDLE,

    BMO_STR_TOAST_EVENT_ADDED,
    BMO_STR_TOAST_ALARM_SET,
    BMO_STR_TOAST_ALARM_OFF,
    BMO_STR_TOAST_TIMER_START,
    BMO_STR_TOAST_TIMER_CANCEL,
    BMO_STR_TOAST_TIMER_END,
    BMO_STR_TOAST_ALARM_RING,
    BMO_STR_TOAST_ALERT_OFF,

    BMO_STR_COUNT
} BMO_STR_E;

void bmo_lang_init(void);
BMO_LANG_E bmo_lang_get(void);
void bmo_lang_set(BMO_LANG_E lang);
void bmo_lang_toggle(void);

/** @return static string for current language; never NULL */
const char *bmo_tr(BMO_STR_E id);

/** Weekday short name (Sun..Sat index 0..6) in current language */
const char *bmo_tr_weekday(int wday);

#endif
