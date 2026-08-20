/**
 * @file feishu_cal.c
 * @brief Minimal Feishu calendar API client over HTTPS.
 */

#include "tal_api.h"
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "http_client_interface.h"
#include "iotdns.h"
#include "tal_time_service.h"
#include "feishu_cal.h"

#define FS_HOST            "open.feishu.cn"
#define FS_HTTP_TIMEOUT_MS 15000
#define FS_RESP_BUF_SIZE   (12 * 1024)
/* Creating an event echoes back a single event object, not a listing. */
#define FS_ADD_RESP_BUF_SIZE (4 * 1024)
#define FS_TOKEN_MARGIN_S  300

static char          sg_token[512]      = {0};
static uint32_t      sg_token_expire_ms = 0;
static char          sg_cal_id[128]     = {0};
static uint8_t      *sg_cacert          = NULL;
static uint16_t      sg_cacert_len      = 0;
static SEM_HANDLE    sg_refresh_sem     = NULL;
static volatile bool sg_refresh_now     = false;
static MUTEX_HANDLE  sg_lock            = NULL;

/* Periodic sync runs on the background task while the MCP tool answers on the
 * agent's thread, and both walk the shared token/calendar-id cache. */
static void __lock(void)
{
    if (sg_lock) {
        tal_mutex_lock(sg_lock);
    }
}

static void __unlock(void)
{
    if (sg_lock) {
        tal_mutex_unlock(sg_lock);
    }
}

static OPERATE_RET __ensure_cert(void)
{
    OPERATE_RET rt = OPRT_OK;

    if (sg_cacert && sg_cacert_len > 0) {
        return OPRT_OK;
    }

    uint8_t *cert = NULL;
    uint16_t len  = 0;
    rt = tuya_iotdns_query_domain_certs((char *)FS_HOST, &cert, &len);
    if (rt == OPRT_OK && cert && len > 0) {
        sg_cacert     = cert;
        sg_cacert_len = len;
        return OPRT_OK;
    }
    if (cert) {
        tal_free(cert);
    }
    PR_WARN("[feishu] cert unavailable, use tls_no_verify");
    return OPRT_OK;
}

static OPERATE_RET __http_call(const char *path, const char *method, const char *body, const char *bearer,
                               char *resp, size_t resp_size, uint16_t *status)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(__ensure_cert());

    http_client_header_t headers[2] = {0};
    uint8_t header_cnt = 0;
    headers[header_cnt++] = (http_client_header_t){.key = "Content-Type", .value = "application/json"};

    char auth[640] = {0};
    if (bearer && bearer[0]) {
        snprintf(auth, sizeof(auth), "Bearer %s", bearer);
        headers[header_cnt++] = (http_client_header_t){.key = "Authorization", .value = auth};
    }

    bool tls_no_verify = (sg_cacert == NULL || sg_cacert_len == 0);
    http_client_response_t response = {0};
    http_client_status_t http_rt = http_client_request(
        &(const http_client_request_t){
            .host = FS_HOST,
            .port = 443,
            .path = path,
            .method = method,
            .headers = headers,
            .headers_count = header_cnt,
            .body = (const uint8_t *)(body ? body : ""),
            .body_length = body ? strlen(body) : 0,
            .cacert = sg_cacert,
            .cacert_len = sg_cacert_len,
            .tls_no_verify = tls_no_verify,
            .timeout_ms = FS_HTTP_TIMEOUT_MS,
        },
        &response);

    if (http_rt != HTTP_CLIENT_SUCCESS) {
        return OPRT_LINK_CORE_HTTP_CLIENT_SEND_ERROR;
    }

    if (status) {
        *status = response.status_code;
    }

    resp[0] = '\0';
    if (response.body && response.body_length > 0) {
        size_t copy = response.body_length < resp_size - 1 ? response.body_length : resp_size - 1;
        memcpy(resp, response.body, copy);
        resp[copy] = '\0';
    }

    http_client_free(&response);
    return OPRT_OK;
}

static const char *__json_str(cJSON *obj, const char *key, const char *dft)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);

    return cJSON_IsString(item) ? item->valuestring : dft;
}

static OPERATE_RET __fetch_token(char *token, size_t token_size)
{
    OPERATE_RET rt = OPRT_OK;
    uint32_t now = tal_system_get_millisecond();
    if (sg_token[0] && now + FS_TOKEN_MARGIN_S * 1000U < sg_token_expire_ms) {
        snprintf(token, token_size, "%s", sg_token);
        return OPRT_OK;
    }

    char body[256];
    snprintf(body, sizeof(body), "{\"app_id\":\"%s\",\"app_secret\":\"%s\"}", FEISHU_APP_ID, FEISHU_APP_SECRET);

    char *resp = tal_malloc(FS_RESP_BUF_SIZE);
    if (!resp) {
        return OPRT_MALLOC_FAILED;
    }

    uint16_t status = 0;
    rt = __http_call("/open-apis/auth/v3/tenant_access_token/internal", "POST", body, NULL, resp,
                     FS_RESP_BUF_SIZE, &status);
    if (rt != OPRT_OK || status != 200) {
        PR_ERR("[feishu] token http failed, rt=%d status=%d", rt, status);
        tal_free(resp);
        return OPRT_COM_ERROR;
    }

    cJSON *root = cJSON_Parse(resp);
    tal_free(resp);
    if (!root) {
        return OPRT_CJSON_PARSE_ERR;
    }

    cJSON *code  = cJSON_GetObjectItem(root, "code");
    cJSON *tok   = cJSON_GetObjectItem(root, "tenant_access_token");
    cJSON *exp   = cJSON_GetObjectItem(root, "expire");
    if (!cJSON_IsNumber(code) || code->valueint != 0 || !cJSON_IsString(tok)) {
        /* code 10003 is a bad app_id, 10014 a bad app_secret. */
        PR_ERR("[feishu] token rejected: code=%d msg=%s", cJSON_IsNumber(code) ? code->valueint : -1,
               __json_str(root, "msg", "?"));
        cJSON_Delete(root);
        return OPRT_COM_ERROR;
    }

    snprintf(sg_token, sizeof(sg_token), "%s", tok->valuestring);
    snprintf(token, token_size, "%s", sg_token);
    if (cJSON_IsNumber(exp)) {
        sg_token_expire_ms = now + (uint32_t)exp->valueint * 1000U;
    } else {
        sg_token_expire_ms = now + 3600U * 1000U;
    }

    cJSON_Delete(root);
    PR_NOTICE("[feishu] token ok, expire_ms=%u", (unsigned)sg_token_expire_ms);
    return OPRT_OK;
}

/*
 * With a tenant_access_token this endpoint lists the calendars *the app* is
 * subscribed to, not the operator's. A fresh custom app owns exactly one empty
 * primary calendar, so taking calendar_list[0] finds a valid id that never has
 * any events in it — the classic "it connects but the agenda is always empty".
 * A calendar the user shared with the bot comes back with role reader/writer,
 * so prefer the first non-owner entry and keep list[0] only as a fallback.
 */
static OPERATE_RET __fetch_calendar_id(const char *token, char *cal_id, size_t cal_id_size)
{
    OPERATE_RET rt = OPRT_OK;
    const char *pick = NULL;
    const char *fallback = NULL;
    cJSON *item = NULL;

    char *resp = tal_malloc(FS_RESP_BUF_SIZE);
    if (!resp) {
        return OPRT_MALLOC_FAILED;
    }

    uint16_t status = 0;
    rt = __http_call("/open-apis/calendar/v4/calendars", "GET", NULL, token, resp, FS_RESP_BUF_SIZE, &status);
    if (rt != OPRT_OK || status != 200) {
        PR_ERR("[feishu] list calendars failed, rt=%d status=%d", rt, status);
        tal_free(resp);
        return OPRT_COM_ERROR;
    }

    cJSON *root = cJSON_Parse(resp);
    tal_free(resp);
    if (!root) {
        return OPRT_CJSON_PARSE_ERR;
    }

    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (cJSON_IsNumber(code) && code->valueint != 0) {
        PR_ERR("[feishu] list calendars rejected: code=%d msg=%s", code->valueint, __json_str(root, "msg", "?"));
        cJSON_Delete(root);
        return OPRT_COM_ERROR;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    cJSON *items = data ? cJSON_GetObjectItem(data, "calendar_list") : NULL;

    cJSON_ArrayForEach(item, items)
    {
        const char *cid = __json_str(item, "calendar_id", NULL);
        const char *role = __json_str(item, "role", "");

        if (!cid) {
            continue;
        }
        PR_NOTICE("[feishu] calendar type=%s role=%s summary=%s", __json_str(item, "type", "?"), role,
                  __json_str(item, "summary", ""));

        if (!fallback) {
            fallback = cid;
        }
        if (!pick && 0 != strcmp(role, "owner")) {
            pick = cid;
        }
    }

    if (!pick) {
        pick = fallback;
    }
    if (!pick) {
        PR_ERR("[feishu] no calendar visible to the app, share one with the bot");
        cJSON_Delete(root);
        return OPRT_NOT_FOUND;
    }

    snprintf(cal_id, cal_id_size, "%s", pick);
    cJSON_Delete(root);
    return OPRT_OK;
}

/* The bot/calendar pairing is fixed once it is set up, so the listing is worth
 * one round trip per boot rather than one per refresh. */
static OPERATE_RET __ensure_cal_id(const char *token, char *cal_id, size_t cal_id_size)
{
    OPERATE_RET rt = OPRT_OK;

    if (FEISHU_CAL_ID[0]) {
        snprintf(cal_id, cal_id_size, "%s", FEISHU_CAL_ID);
        return OPRT_OK;
    }
    if (!sg_cal_id[0]) {
        TUYA_CALL_ERR_RETURN(__fetch_calendar_id(token, sg_cal_id, sizeof(sg_cal_id)));
    }
    snprintf(cal_id, cal_id_size, "%s", sg_cal_id);
    return OPRT_OK;
}

/*
 * tal_time_mktime() is the counterpart of tal_time_gmtime_r(), so it reads the
 * struct as UTC and knows nothing about the device time zone. Feeding it a
 * local wall clock silently shifts the result by the offset, which is how a
 * "today 00:00" window ends up starting at 08:00 in UTC+8. Do the arithmetic
 * on the epoch instead, where the offset is explicit.
 */
static TIME_T __local_midnight(void)
{
    int tz_sec = 0;
    TIME_T local;

    tal_time_get_time_zone_seconds(&tz_sec);
    local = tal_time_get_posix() + (TIME_T)tz_sec;
    return local - (local % 86400) - (TIME_T)tz_sec;
}

static void __fill_from_timestamp(TIME_T ts, FEISHU_CAL_EVENT_T *ev)
{
    POSIX_TM_S tm;
    tal_time_get_local_time_custom(ts, &tm);
    snprintf(ev->time_str, sizeof(ev->time_str), "%02d:%02d", tm.tm_hour, tm.tm_min);
    ev->mon  = (uint8_t)(tm.tm_mon + 1);
    ev->mday = (uint8_t)tm.tm_mday;
}

/* All-day events carry "YYYY-MM-DD" in start_time.date instead of a timestamp. */
static void __fill_from_date(const char *date, FEISHU_CAL_EVENT_T *ev)
{
    int year = 0, mon = 0, mday = 0;

    if (date && 3 == sscanf(date, "%d-%d-%d", &year, &mon, &mday)) {
        ev->mon  = (uint8_t)mon;
        ev->mday = (uint8_t)mday;
    }
    snprintf(ev->time_str, sizeof(ev->time_str), "全天");
}

static OPERATE_RET __fetch_events(const char *token, const char *cal_id, FEISHU_CAL_DATA_T *out)
{
    OPERATE_RET rt = OPRT_OK;
    TIME_T day_start = __local_midnight();
    TIME_T day_end   = day_start + 4 * 24 * 3600;

    char path[256];
    snprintf(path, sizeof(path),
             "/open-apis/calendar/v4/calendars/%s/events?start_time=%llu&end_time=%llu", cal_id,
             (unsigned long long)day_start, (unsigned long long)day_end);

    char *resp = tal_malloc(FS_RESP_BUF_SIZE);
    if (!resp) {
        return OPRT_MALLOC_FAILED;
    }

    uint16_t status = 0;
    rt = __http_call(path, "GET", NULL, token, resp, FS_RESP_BUF_SIZE, &status);
    if (rt != OPRT_OK || status != 200) {
        PR_ERR("[feishu] list events failed, rt=%d status=%d", rt, status);
        tal_free(resp);
        return OPRT_COM_ERROR;
    }

    cJSON *root = cJSON_Parse(resp);
    tal_free(resp);
    if (!root) {
        return OPRT_CJSON_PARSE_ERR;
    }

    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (cJSON_IsNumber(code) && code->valueint != 0) {
        PR_ERR("[feishu] list events rejected: code=%d msg=%s", code->valueint, __json_str(root, "msg", "?"));
        cJSON_Delete(root);
        return OPRT_COM_ERROR;
    }

    out->count = 0;
    cJSON *data  = cJSON_GetObjectItem(root, "data");
    cJSON *items = data ? cJSON_GetObjectItem(data, "items") : NULL;
    if (cJSON_IsArray(items)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, items)
        {
            if (out->count >= FEISHU_CAL_MAX_EVENTS) {
                break;
            }
            cJSON *summary = cJSON_GetObjectItem(item, "summary");
            cJSON *start   = cJSON_GetObjectItem(item, "start_time");
            cJSON *ts      = start ? cJSON_GetObjectItem(start, "timestamp") : NULL;
            cJSON *date    = start ? cJSON_GetObjectItem(start, "date") : NULL;
            if (!cJSON_IsString(summary)) {
                continue;
            }
            FEISHU_CAL_EVENT_T *ev = &out->events[out->count];
            snprintf(ev->title, sizeof(ev->title), "%s", summary->valuestring);
            if (cJSON_IsString(ts)) {
                __fill_from_timestamp((TIME_T)atoll(ts->valuestring), ev);
            } else if (cJSON_IsString(date)) {
                __fill_from_date(date->valuestring, ev);
            } else {
                snprintf(ev->time_str, sizeof(ev->time_str), "--:--");
            }
            out->count++;
        }
    }

    cJSON_Delete(root);
    out->valid = true;
    PR_NOTICE("[feishu] %d event(s) in the next 4 days", out->count);
    return OPRT_OK;
}

OPERATE_RET feishu_cal_fetch(FEISHU_CAL_DATA_T *out)
{
    OPERATE_RET rt = OPRT_OK;

    if (!out) {
        return OPRT_INVALID_PARM;
    }
    memset(out, 0, sizeof(*out));

    if (0 == strcmp(FEISHU_APP_ID, "cli_xxxxxxxxxx")) {
        PR_WARN("[feishu] placeholder app_id, skip fetch");
        return OPRT_NOT_SUPPORTED;
    }

    char token[512] = {0};
    char cal_id[128] = {0};

    __lock();
    rt = __fetch_token(token, sizeof(token));
    if (OPRT_OK == rt) {
        rt = __ensure_cal_id(token, cal_id, sizeof(cal_id));
    }
    if (OPRT_OK == rt) {
        rt = __fetch_events(token, cal_id, out);
    }
    __unlock();
    return rt;
}

OPERATE_RET feishu_cal_add_event(const char *title, int day_offset, int hour, int minute,
                                 int duration_min, char *when, size_t when_size)
{
    OPERATE_RET rt = OPRT_OK;
    char token[512] = {0};
    char cal_id[128] = {0};
    char path[256] = {0};
    char ts[24] = {0};
    TIME_T start, end;
    POSIX_TM_S tm;
    cJSON *root = NULL;
    cJSON *node = NULL;
    char *body = NULL;
    char *resp = NULL;
    uint16_t status = 0;

    if (!title || !title[0] || duration_min <= 0) {
        return OPRT_INVALID_PARM;
    }
    if (day_offset < 0 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return OPRT_INVALID_PARM;
    }
    if (0 == strcmp(FEISHU_APP_ID, "cli_xxxxxxxxxx")) {
        PR_WARN("[feishu] placeholder app_id, skip add");
        return OPRT_NOT_SUPPORTED;
    }

    start = __local_midnight() + (TIME_T)day_offset * 86400 + (TIME_T)hour * 3600 + (TIME_T)minute * 60;
    end   = start + (TIME_T)duration_min * 60;
    tal_time_get_local_time_custom(start, &tm);
    if (when && when_size) {
        snprintf(when, when_size, "%02d-%02d %02d:%02d", tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);
    }

    __lock();
    rt = __fetch_token(token, sizeof(token));
    if (OPRT_OK == rt) {
        rt = __ensure_cal_id(token, cal_id, sizeof(cal_id));
    }
    if (OPRT_OK != rt) {
        __unlock();
        return rt;
    }

    /* The title is dictated by the user, so let cJSON escape whatever lands in
     * it instead of pasting it into a format string. */
    root = cJSON_CreateObject();
    node = cJSON_CreateObject();
    if (!root || !node) {
        cJSON_Delete(root);
        cJSON_Delete(node);
        __unlock();
        return OPRT_MALLOC_FAILED;
    }
    cJSON_AddStringToObject(root, "summary", title);
    snprintf(ts, sizeof(ts), "%llu", (unsigned long long)start);
    cJSON_AddStringToObject(node, "timestamp", ts);
    cJSON_AddItemToObject(root, "start_time", node);

    node = cJSON_CreateObject();
    if (!node) {
        cJSON_Delete(root);
        __unlock();
        return OPRT_MALLOC_FAILED;
    }
    snprintf(ts, sizeof(ts), "%llu", (unsigned long long)end);
    cJSON_AddStringToObject(node, "timestamp", ts);
    cJSON_AddItemToObject(root, "end_time", node);

    body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        __unlock();
        return OPRT_MALLOC_FAILED;
    }
    resp = tal_malloc(FS_ADD_RESP_BUF_SIZE);
    if (!resp) {
        cJSON_free(body);
        __unlock();
        return OPRT_MALLOC_FAILED;
    }

    snprintf(path, sizeof(path), "/open-apis/calendar/v4/calendars/%s/events", cal_id);
    rt = __http_call(path, "POST", body, token, resp, FS_ADD_RESP_BUF_SIZE, &status);
    cJSON_free(body);
    __unlock();

    if (rt != OPRT_OK || status != 200) {
        PR_ERR("[feishu] add event failed, rt=%d status=%d", rt, status);
        tal_free(resp);
        return OPRT_COM_ERROR;
    }

    root = cJSON_Parse(resp);
    tal_free(resp);
    if (!root) {
        return OPRT_CJSON_PARSE_ERR;
    }

    cJSON *code = cJSON_GetObjectItem(root, "code");
    if (!cJSON_IsNumber(code) || code->valueint != 0) {
        /* 99991672 means the bot only holds reader on the calendar. */
        PR_ERR("[feishu] add event rejected: code=%d msg=%s", cJSON_IsNumber(code) ? code->valueint : -1,
               __json_str(root, "msg", "?"));
        cJSON_Delete(root);
        return OPRT_COM_ERROR;
    }
    cJSON_Delete(root);

    PR_NOTICE("[feishu] event added: %s at %02d-%02d %02d:%02d", title, tm.tm_mon + 1, tm.tm_mday,
              tm.tm_hour, tm.tm_min);
    feishu_cal_request_refresh();
    return OPRT_OK;
}

void feishu_cal_request_refresh(void)
{
    sg_refresh_now = true;
    if (sg_refresh_sem) {
        tal_semaphore_post(sg_refresh_sem);
    }
}

void feishu_cal_bind_refresh_sem(SEM_HANDLE sem)
{
    sg_refresh_sem = sem;
    if (!sg_lock) {
        tal_mutex_create_init(&sg_lock);
    }
}

bool feishu_cal_take_refresh_request(void)
{
    if (sg_refresh_now) {
        sg_refresh_now = false;
        return true;
    }
    return false;
}
