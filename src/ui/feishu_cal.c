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
#define FS_TOKEN_MARGIN_S  300

static char          sg_token[512]      = {0};
static uint32_t      sg_token_expire_ms = 0;
static uint8_t      *sg_cacert          = NULL;
static uint16_t      sg_cacert_len      = 0;
static SEM_HANDLE    sg_refresh_sem     = NULL;
static volatile bool sg_refresh_now     = false;

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

static OPERATE_RET __fetch_calendar_id(const char *token, char *cal_id, size_t cal_id_size)
{
    OPERATE_RET rt = OPRT_OK;

    char *resp = tal_malloc(FS_RESP_BUF_SIZE);
    if (!resp) {
        return OPRT_MALLOC_FAILED;
    }

    uint16_t status = 0;
    rt = __http_call("/open-apis/calendar/v4/calendars", "GET", NULL, token, resp, FS_RESP_BUF_SIZE, &status);
    if (rt != OPRT_OK || status != 200) {
        tal_free(resp);
        return OPRT_COM_ERROR;
    }

    cJSON *root = cJSON_Parse(resp);
    tal_free(resp);
    if (!root) {
        return OPRT_CJSON_PARSE_ERR;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    cJSON *items = data ? cJSON_GetObjectItem(data, "calendar_list") : NULL;
    cJSON *first = cJSON_IsArray(items) ? cJSON_GetArrayItem(items, 0) : NULL;
    cJSON *cid   = first ? cJSON_GetObjectItem(first, "calendar_id") : NULL;
    if (!cJSON_IsString(cid)) {
        cJSON_Delete(root);
        return OPRT_NOT_FOUND;
    }

    snprintf(cal_id, cal_id_size, "%s", cid->valuestring);
    cJSON_Delete(root);
    return OPRT_OK;
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
    TIME_T now = tal_time_get_posix();
    POSIX_TM_S tm;
    tal_time_get_local_time_custom(now, &tm);
    tm.tm_hour = 0;
    tm.tm_min  = 0;
    tm.tm_sec  = 0;
    TIME_T day_start = tal_time_mktime(&tm);
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
        tal_free(resp);
        return OPRT_COM_ERROR;
    }

    cJSON *root = cJSON_Parse(resp);
    tal_free(resp);
    if (!root) {
        return OPRT_CJSON_PARSE_ERR;
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
    TUYA_CALL_ERR_RETURN(__fetch_token(token, sizeof(token)));
    TUYA_CALL_ERR_RETURN(__fetch_calendar_id(token, cal_id, sizeof(cal_id)));
    return __fetch_events(token, cal_id, out);
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
}

bool feishu_cal_take_refresh_request(void)
{
    if (sg_refresh_now) {
        sg_refresh_now = false;
        return true;
    }
    return false;
}
