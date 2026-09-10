/**
 * @file ui_i18n.c
 * @brief Runtime bilingual UI strings + KV persistence.
 */

#include "ui_i18n.h"
#include "ui_bg_task.h"
#include "tal_api.h"
#include "tal_kv.h"

#include <string.h>

#define BMO_LANG_KV_KEY "bmo_lang"

static BMO_LANG_E sg_lang = BMO_LANG_ZH;
static bool sg_inited = false;

static const char *const sg_zh[BMO_STR_COUNT] = {
    [BMO_STR_SETTINGS_TITLE] = "设置",
    [BMO_STR_LANGUAGE] = "语言",
    [BMO_STR_LANG_ZH] = "中文",
    [BMO_STR_LANG_EN] = "English",
    [BMO_STR_FIRMWARE] = "固件版本",
    [BMO_STR_CHECK_UPDATE] = "检查更新",
    [BMO_STR_UPDATE] = "更新",
    [BMO_STR_HINT_ENTER] = "按十字中进入设置",
    [BMO_STR_HINT_BROWSE] = "上下选行 · 左右切语言 · 中/绿执行 · 三角退出",
    [BMO_STR_EXIT_BROWSE] = "已退出浏览",

    [BMO_STR_OTA_LATEST] = "已是最新",
    [BMO_STR_OTA_FOUND] = "发现更新，开始下载",
    [BMO_STR_OTA_OFFLINE] = "离线无法检查",
    [BMO_STR_OTA_BUSY] = "更新进行中，请稍候",
    [BMO_STR_OTA_CHECKING] = "正在检查更新...",

    [BMO_STR_REFRESHING] = "正在刷新...",
    [BMO_STR_MUTED] = "已静音",
    [BMO_STR_NETCFG_HOLD] = "重置网络 %d\n松开取消",

    [BMO_STR_RSS_TITLE] = "Maker 资讯",
    [BMO_STR_RSS_EMPTY] = "暂无",
    [BMO_STR_RSS_WAIT] = "等待拉取或绿键刷新",
    [BMO_STR_RSS_NO_TITLES] = "这个源暂时没有标题",
    [BMO_STR_RSS_HINT] = "按十字中浏览 · 绿键刷新",
    [BMO_STR_RSS_HINT_BROWSE] = "左右换源 · 上下选 · 中看摘要 · 三角退出",
    [BMO_STR_RSS_HINT_DETAIL] = "上下滚动 · 左右换条 · 三角返回",
    [BMO_STR_RSS_NO_SUMMARY] = "暂无摘要",
    [BMO_STR_RSS_ALL] = "全部",

    [BMO_STR_CAL_WAIT_SYNC] = "等待时间同步...",
    [BMO_STR_CAL_EVENTS] = "日程",
    [BMO_STR_CAL_TODAY_EVENTS] = "今日日程",
    [BMO_STR_CAL_NO_DATA] = "暂无数据",
    [BMO_STR_CAL_LOADING] = "加载中...",
    [BMO_STR_CAL_NONE_TODAY] = "今天没有安排 :)",
    [BMO_STR_CAL_NONE_DAY] = "这天没有安排",
    [BMO_STR_CAL_HINT] = "按十字中进入浏览",
    [BMO_STR_CAL_HINT_BROWSE] = "左右换天 · 三角退出",
    [BMO_STR_CAL_PICK_DAY] = "选 %d月%d日 周%s",
    [BMO_STR_CAL_TODAY_LINE] = "%d月%d日 周%s",

    [BMO_STR_CLK_WAIT_SYNC] = "等待时间同步...",
    [BMO_STR_CLK_HINT] = "按十字中设置闹钟/计时",
    [BMO_STR_CLK_HINT_MENU] = "上下选择 · 中确认 · 绿键关闭 · 三角退出",
    [BMO_STR_CLK_HINT_ALARM] = "左右切位 · 上下调 · 中保存",
    [BMO_STR_CLK_HINT_TIMER] = "左右切位 · 上下调 · 中开始",
    [BMO_STR_CLK_MENU_PICK] = "选择要设置的项目",
    [BMO_STR_CLK_ALARM] = "闹钟",
    [BMO_STR_CLK_TIMER] = "计时器",
    [BMO_STR_WEEK_PREFIX] = "星期",

    [BMO_STR_WX_TITLE] = "天气",
    [BMO_STR_WX_LOADING] = "加载中",
    [BMO_STR_WX_NO_DATA] = "暂无数据",
    [BMO_STR_WX_HI] = "最高 %d°",
    [BMO_STR_WX_LO] = "最低 %d°",
    [BMO_STR_WX_HUMI] = "湿度 %d%%",

    [BMO_STR_GAMES_TITLE] = "游戏",
    [BMO_STR_GAMES_HINT] = "上下选择 · 中开始 · 左右翻页",
    [BMO_STR_GAMES_HINT_IDLE] = "三角退出",

    [BMO_STR_TOAST_EVENT_ADDED] = "日程已添加",
    [BMO_STR_TOAST_ALARM_SET] = "闹钟已设置",
    [BMO_STR_TOAST_ALARM_OFF] = "闹钟已关闭",
    [BMO_STR_TOAST_TIMER_START] = "计时已开始",
    [BMO_STR_TOAST_TIMER_CANCEL] = "计时已取消",
    [BMO_STR_TOAST_TIMER_END] = "计时结束",
    [BMO_STR_TOAST_ALARM_RING] = "闹钟响了",
    [BMO_STR_TOAST_ALERT_OFF] = "已关闭提醒",
};

static const char *const sg_en[BMO_STR_COUNT] = {
    [BMO_STR_SETTINGS_TITLE] = "Settings",
    [BMO_STR_LANGUAGE] = "Language",
    [BMO_STR_LANG_ZH] = "中文",
    [BMO_STR_LANG_EN] = "English",
    [BMO_STR_FIRMWARE] = "Firmware",
    [BMO_STR_CHECK_UPDATE] = "Check update",
    [BMO_STR_UPDATE] = "Update",
    [BMO_STR_HINT_ENTER] = "Press Mid to open",
    [BMO_STR_HINT_BROWSE] = "U/D row · L/R language · Mid/Green run · Tri exit",
    [BMO_STR_EXIT_BROWSE] = "Exited browse",

    [BMO_STR_OTA_LATEST] = "Up to date",
    [BMO_STR_OTA_FOUND] = "Update found, downloading",
    [BMO_STR_OTA_OFFLINE] = "Offline, cannot check",
    [BMO_STR_OTA_BUSY] = "Update in progress",
    [BMO_STR_OTA_CHECKING] = "Checking for updates...",

    [BMO_STR_REFRESHING] = "Refreshing...",
    [BMO_STR_MUTED] = "Muted",
    [BMO_STR_NETCFG_HOLD] = "Reset net %d\nrelease cancel",

    [BMO_STR_RSS_TITLE] = "Maker RSS",
    [BMO_STR_RSS_EMPTY] = "None",
    [BMO_STR_RSS_WAIT] = "Waiting, or Green to refresh",
    [BMO_STR_RSS_NO_TITLES] = "No headlines for this source",
    [BMO_STR_RSS_HINT] = "Mid browse · Green refresh",
    [BMO_STR_RSS_HINT_BROWSE] = "L/R source · U/D select · Mid open · Tri exit",
    [BMO_STR_RSS_HINT_DETAIL] = "U/D scroll · L/R item · Tri back",
    [BMO_STR_RSS_NO_SUMMARY] = "No summary",
    [BMO_STR_RSS_ALL] = "All",

    [BMO_STR_CAL_WAIT_SYNC] = "Waiting for time sync...",
    [BMO_STR_CAL_EVENTS] = "Events",
    [BMO_STR_CAL_TODAY_EVENTS] = "Today",
    [BMO_STR_CAL_NO_DATA] = "No data",
    [BMO_STR_CAL_LOADING] = "Loading...",
    [BMO_STR_CAL_NONE_TODAY] = "Nothing today :)",
    [BMO_STR_CAL_NONE_DAY] = "Nothing this day",
    [BMO_STR_CAL_HINT] = "Press Mid to browse",
    [BMO_STR_CAL_HINT_BROWSE] = "L/R day · Tri exit",
    [BMO_STR_CAL_PICK_DAY] = "Pick %d/%d %s",
    [BMO_STR_CAL_TODAY_LINE] = "%d/%d %s",

    [BMO_STR_CLK_WAIT_SYNC] = "Waiting for time sync...",
    [BMO_STR_CLK_HINT] = "Press Mid for alarm/timer",
    [BMO_STR_CLK_HINT_MENU] = "U/D pick · Mid OK · Green clear · Tri exit",
    [BMO_STR_CLK_HINT_ALARM] = "L/R digit · U/D adjust · Mid save",
    [BMO_STR_CLK_HINT_TIMER] = "L/R digit · U/D adjust · Mid start",
    [BMO_STR_CLK_MENU_PICK] = "Choose what to set",
    [BMO_STR_CLK_ALARM] = "Alarm",
    [BMO_STR_CLK_TIMER] = "Timer",
    [BMO_STR_WEEK_PREFIX] = "",

    [BMO_STR_WX_TITLE] = "Weather",
    [BMO_STR_WX_LOADING] = "Loading",
    [BMO_STR_WX_NO_DATA] = "No data",
    [BMO_STR_WX_HI] = "Hi %d°",
    [BMO_STR_WX_LO] = "Lo %d°",
    [BMO_STR_WX_HUMI] = "Hum %d%%",

    [BMO_STR_GAMES_TITLE] = "Games",
    [BMO_STR_GAMES_HINT] = "U/D pick · Mid start · L/R page",
    [BMO_STR_GAMES_HINT_IDLE] = "Tri to quit",

    [BMO_STR_TOAST_EVENT_ADDED] = "Event added",
    [BMO_STR_TOAST_ALARM_SET] = "Alarm set",
    [BMO_STR_TOAST_ALARM_OFF] = "Alarm cleared",
    [BMO_STR_TOAST_TIMER_START] = "Timer started",
    [BMO_STR_TOAST_TIMER_CANCEL] = "Timer cancelled",
    [BMO_STR_TOAST_TIMER_END] = "Timer done",
    [BMO_STR_TOAST_ALARM_RING] = "Alarm!",
    [BMO_STR_TOAST_ALERT_OFF] = "Alert dismissed",
};

static const char *const sg_week_zh[7] = {"日", "一", "二", "三", "四", "五", "六"};
/* Two letters so headers fit the 26 px calendar cells. */
static const char *const sg_week_en[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};

static void __persist(void)
{
    uint8_t v = (uint8_t)sg_lang;
    (void)tal_kv_set(BMO_LANG_KV_KEY, &v, sizeof(v));
}

static void __load(void)
{
    uint8_t *buf = NULL;
    size_t len = 0;

    if (OPRT_OK != tal_kv_get(BMO_LANG_KV_KEY, &buf, &len) || !buf || len < 1) {
        if (buf) {
            tal_kv_free(buf);
        }
        return;
    }
    if (buf[0] == (uint8_t)BMO_LANG_EN) {
        sg_lang = BMO_LANG_EN;
    } else {
        sg_lang = BMO_LANG_ZH;
    }
    tal_kv_free(buf);
}

void bmo_lang_init(void)
{
    if (sg_inited) {
        return;
    }
    __load();
    sg_inited = true;
    PR_NOTICE("[i18n] lang=%s", sg_lang == BMO_LANG_EN ? "en" : "zh");
}

BMO_LANG_E bmo_lang_get(void)
{
    if (!sg_inited) {
        bmo_lang_init();
    }
    return sg_lang;
}

void bmo_lang_set(BMO_LANG_E lang)
{
    if (!sg_inited) {
        bmo_lang_init();
    }
    if (lang != BMO_LANG_ZH && lang != BMO_LANG_EN) {
        return;
    }
    if (sg_lang == lang) {
        return;
    }
    sg_lang = lang;
    __persist();
    /* Weather/forecast strings are baked at fetch time — pull again so the
     * next weather page visit matches the new UI language. */
    ui_bg_task_request_refresh();
    PR_NOTICE("[i18n] set lang=%s", sg_lang == BMO_LANG_EN ? "en" : "zh");
}

void bmo_lang_toggle(void)
{
    bmo_lang_set(bmo_lang_get() == BMO_LANG_ZH ? BMO_LANG_EN : BMO_LANG_ZH);
}

const char *bmo_tr(BMO_STR_E id)
{
    const char *const *table;

    if (!sg_inited) {
        bmo_lang_init();
    }
    if ((int)id < 0 || id >= BMO_STR_COUNT) {
        return "";
    }
    table = (sg_lang == BMO_LANG_EN) ? sg_en : sg_zh;
    return table[id] ? table[id] : "";
}

const char *bmo_tr_weekday(int wday)
{
    if (!sg_inited) {
        bmo_lang_init();
    }
    if (wday < 0 || wday > 6) {
        wday = 0;
    }
    return (bmo_lang_get() == BMO_LANG_EN) ? sg_week_en[wday] : sg_week_zh[wday];
}
