/**
 * @file rss_feed.c
 * @brief Fetch a shortlist of Maker RSS / Atom titles + summaries over HTTPS.
 *
 * No full XML library: a linear scan for <item>/<entry>, then <title> and a
 * short plain-text body (description / summary / content). Each feed
 * contributes a few items so one slow source cannot empty the whole page.
 */

#include "rss_feed.h"
#include "tal_api.h"
#include "http_client_interface.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RSS_HTTP_TIMEOUT_MS 20000
#define RSS_RESP_BUF_SIZE   (40 * 1024)
#define RSS_PER_SOURCE      4
#define RSS_EXTRACT_RAW_MAX 4096

/* Set for the duration of rss_feed_fetch() — live in PSRAM, not BSS. */
static char *sg_extract_raw;
static char *sg_extract_best;

static int __strncasecmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int ca = tolower((unsigned char)a[i]);
        int cb = tolower((unsigned char)b[i]);
        if (ca != cb || ca == 0) {
            return ca - cb;
        }
    }
    return 0;
}
typedef struct {
    const char *name; /* short label for the UI */
    const char *host;
    const char *path;
} RSS_SOURCE_T;

/* Official public feeds. Make: Maker Pro is skipped so the list stays at eight. */
static const RSS_SOURCE_T sg_sources[RSS_SOURCE_CNT] = {
    {"Hackaday", "hackaday.com", "/blog/feed/"},
    {"CNX", "www.cnx-software.com", "/feed/"},
    {"Make", "makezine.com", "/feed/"},
    {"Adafruit", "blog.adafruit.com", "/feed/"},
    {"Learn", "learn.adafruit.com", "/feed"},
    {"Pi", "www.raspberrypi.com", "/news/feed/"},
    {"Seeed", "www.seeedstudio.com", "/blog/feed/"},
    {"Arduino", "blog.arduino.cc", "/feed/"},
};

const char *rss_source_name(int idx)
{
    if (idx < 0 || idx >= RSS_SOURCE_CNT) {
        return "?";
    }
    return sg_sources[idx].name;
}

/*
 * Public Maker sites are not covered by Tuya IoTDNS CA bundles. With cacert=NULL
 * the TLS stack still calls legacy_mbedtls_cacert_load() → IoTDNS, stores a
 * blob mbedtls cannot parse (0x3a00), and the handshake dies even when
 * VERIFY_NONE is requested. Pass any valid PEM as ca_cert so that path is
 * skipped; keep tls_no_verify so the placeholder need not match the peer.
 * PEM below is ISRG Root X1 (Let's Encrypt).
 */
static const char sg_tls_placeholder_pem[] =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "-----END CERTIFICATE-----\n";

static OPERATE_RET __http_get(const char *host, const char *path, char *resp, size_t resp_size, uint16_t *status)
{
    http_client_header_t headers[3];
    http_client_response_t response = {0};
    http_client_status_t http_rt;

    headers[0] = (http_client_header_t){.key = "User-Agent", .value = "BMO-Desktop-Avatar/1.0 RSS"};
    headers[1] = (http_client_header_t){.key = "Accept", .value = "application/rss+xml, application/atom+xml, application/xml, text/xml, */*"};
    headers[2] = (http_client_header_t){.key = "Accept-Encoding", .value = "identity"};

    http_rt = http_client_request(
        &(const http_client_request_t){
            .host = host,
            .port = 443,
            .path = path,
            .method = "GET",
            .headers = headers,
            .headers_count = 3,
            .body = (const uint8_t *)"",
            .body_length = 0,
            .cacert = (const uint8_t *)sg_tls_placeholder_pem,
            .cacert_len = sizeof(sg_tls_placeholder_pem),
            .tls_no_verify = true,
            .timeout_ms = RSS_HTTP_TIMEOUT_MS,
        },
        &response);

    if (status) {
        *status = response.status_code;
    }
    if (http_rt != HTTP_CLIENT_SUCCESS) {
        PR_ERR("[rss] http %s%s failed, client_rt=%d", host, path, (int)http_rt);
        http_client_free(&response);
        return OPRT_COM_ERROR;
    }
    if (response.body && response.body_length > 0) {
        size_t n = response.body_length;
        if (n >= resp_size) {
            n = resp_size - 1;
        }
        memcpy(resp, response.body, n);
        resp[n] = '\0';
    } else {
        resp[0] = '\0';
    }
    http_client_free(&response);
    return OPRT_OK;
}

static void __decode_entities(char *s)
{
    char *r = s;
    char *w = s;

    while (*r) {
        if (r[0] == '&') {
            if (0 == strncmp(r, "&amp;", 5)) {
                *w++ = '&';
                r += 5;
                continue;
            }
            if (0 == strncmp(r, "&lt;", 4)) {
                *w++ = '<';
                r += 4;
                continue;
            }
            if (0 == strncmp(r, "&gt;", 4)) {
                *w++ = '>';
                r += 4;
                continue;
            }
            if (0 == strncmp(r, "&quot;", 6)) {
                *w++ = '"';
                r += 6;
                continue;
            }
            if (0 == strncmp(r, "&apos;", 6) || 0 == strncmp(r, "&#39;", 5)) {
                *w++ = '\'';
                r += (r[1] == '#' ? 5 : 6);
                continue;
            }
            if (0 == strncmp(r, "&#8217;", 7) || 0 == strncmp(r, "&#8216;", 7)) {
                *w++ = '\'';
                r += 7;
                continue;
            }
            if (0 == strncmp(r, "&#8211;", 7) || 0 == strncmp(r, "&#8212;", 7)) {
                *w++ = '-';
                r += 7;
                continue;
            }
            if (r[1] == '#' && isdigit((unsigned char)r[2])) {
                int code = 0;
                r += 2;
                while (isdigit((unsigned char)*r)) {
                    code = code * 10 + (*r - '0');
                    r++;
                }
                if (*r == ';') {
                    r++;
                }
                *w++ = (code > 0 && code < 128) ? (char)code : '?';
                continue;
            }
        }
        *w++ = *r++;
    }
    *w = '\0';
}

static void __strip_tags(char *s)
{
    char *r = s;
    char *w = s;
    bool in_tag = false;

    while (*r) {
        if (*r == '<') {
            in_tag = true;
            r++;
            continue;
        }
        if (*r == '>') {
            in_tag = false;
            r++;
            continue;
        }
        if (!in_tag) {
            *w++ = *r;
        }
        r++;
    }
    *w = '\0';
}

static char *__find_ci(char *hay, const char *needle)
{
    size_t n = strlen(needle);

    for (; *hay; hay++) {
        if (0 == __strncasecmp(hay, needle, n)) {
            return hay;
        }
    }
    return NULL;
}

static void __collapse_ws(char *s)
{
    char *r = s;
    char *w = s;
    bool sp = false;

    while (*r) {
        if (isspace((unsigned char)*r)) {
            if (!sp && w != s) {
                *w++ = ' ';
                sp = true;
            }
            r++;
            continue;
        }
        sp = false;
        *w++ = *r++;
    }
    *w = '\0';
}

/**
 * Pull tag body → strip HTML into sg_extract_raw.
 * @return plain-text length, or 0 if missing/empty.
 */
static size_t __extract_tag_plain(char *block, const char *open, const char *close)
{
    char *t0, *body, *end;
    size_t len;

    t0 = __find_ci(block, open);
    if (!t0) {
        return 0;
    }
    t0 = strchr(t0, '>');
    if (!t0) {
        return 0;
    }
    body = t0 + 1;
    while (*body && isspace((unsigned char)*body)) {
        body++;
    }
    if (0 == __strncasecmp(body, "<![CDATA[", 9)) {
        body += 9;
        end = strstr(body, "]]>");
    } else {
        end = __find_ci(body, close);
    }
    if (!end || end <= body) {
        return 0;
    }
    len = (size_t)(end - body);
    if (!sg_extract_raw) {
        return 0;
    }
    if (len >= RSS_EXTRACT_RAW_MAX) {
        len = RSS_EXTRACT_RAW_MAX - 1;
    }
    memcpy(sg_extract_raw, body, len);
    sg_extract_raw[len] = '\0';
    __strip_tags(sg_extract_raw);
    __decode_entities(sg_extract_raw);
    __collapse_ws(sg_extract_raw);
    return strlen(sg_extract_raw);
}

static void __copy_plain_truncated(const char *src, char *out, size_t out_size)
{
    size_t plain_len, copy_len;

    if (!out || out_size == 0) {
        return;
    }
    if (!src || !src[0]) {
        out[0] = '\0';
        return;
    }
    plain_len = strlen(src);
    if (plain_len < out_size) {
        memcpy(out, src, plain_len + 1);
        return;
    }
    copy_len = out_size - 1;
    if (copy_len >= 3) {
        memcpy(out, src, copy_len - 3);
        out[copy_len - 3] = '.';
        out[copy_len - 2] = '.';
        out[copy_len - 1] = '.';
        out[copy_len] = '\0';
    } else {
        memcpy(out, src, copy_len);
        out[copy_len] = '\0';
    }
}

static bool __extract_title(char *block, char *out, size_t out_size)
{
    if (__extract_tag_plain(block, "<title", "</title>") == 0) {
        return false;
    }
    __copy_plain_truncated(sg_extract_raw, out, out_size);
    return out[0] != '\0';
}

/**
 * Keep the longest plain-text body among RSS/Atom fields. Many Maker feeds
 * put a one-line blurb in <description> and the real article in
 * <content:encoded>; preferring description alone looked "incomplete".
 */
static bool __extract_summary(char *block, char *out, size_t out_size)
{
    static const char *const tags[][2] = {
        {"<description", "</description>"},
        {"<summary", "</summary>"},
        {"<content:encoded", "</content:encoded>"},
        {"<content", "</content>"},
    };
    size_t best_len = 0;

    if (!out || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    if (!sg_extract_best || !sg_extract_raw) {
        return false;
    }
    sg_extract_best[0] = '\0';

    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        size_t n = __extract_tag_plain(block, tags[i][0], tags[i][1]);
        if (n > best_len) {
            best_len = n;
            memcpy(sg_extract_best, sg_extract_raw, n + 1);
        }
    }
    if (best_len == 0) {
        return false;
    }
    __copy_plain_truncated(sg_extract_best, out, out_size);
    return out[0] != '\0';
}

static int __parse_feed(char *xml, int source, RSS_FEED_DATA_T *out)
{
    char *p = xml;
    int added = 0;
    char title[RSS_TITLE_MAX];

    while (added < RSS_PER_SOURCE && out->count < RSS_MAX_ITEMS && p && *p) {
        char *item = __find_ci(p, "<item");
        char *entry = __find_ci(p, "<entry");
        char *block;
        char *close;
        char save;

        if (item && entry) {
            block = (item < entry) ? item : entry;
        } else {
            block = item ? item : entry;
        }
        if (!block) {
            break;
        }

        if (0 == __strncasecmp(block, "<item", 5)) {
            close = __find_ci(block + 5, "</item>");
            if (close) {
                close += 7;
            }
        } else {
            close = __find_ci(block + 6, "</entry>");
            if (close) {
                close += 8;
            }
        }
        if (!close) {
            break;
        }

        save = *close;
        *close = '\0';
        if (__extract_title(block, title, sizeof(title))) {
            bool dup = false;
            for (int i = 0; i < out->count; i++) {
                if (out->items[i].source == (uint8_t)source && 0 == strcmp(out->items[i].title, title)) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                RSS_ITEM_T *it = &out->items[out->count];
                it->source = (uint8_t)source;
                snprintf(it->title, sizeof(it->title), "%s", title);
                it->summary[0] = '\0';
                (void)__extract_summary(block, it->summary, sizeof(it->summary));
                out->count++;
                added++;
            }
        }
        *close = save;
        p = close;
    }
    return added;
}

OPERATE_RET rss_feed_fetch(RSS_FEED_DATA_T *out)
{
    char *resp;
    int ok_sources = 0;

    if (!out) {
        return OPRT_INVALID_PARM;
    }
    memset(out, 0, sizeof(*out));

    sg_extract_raw = (char *)tal_psram_malloc(RSS_EXTRACT_RAW_MAX);
    sg_extract_best = (char *)tal_psram_malloc(RSS_EXTRACT_RAW_MAX);
    resp = (char *)tal_psram_malloc(RSS_RESP_BUF_SIZE);
    if (!sg_extract_raw || !sg_extract_best || !resp) {
        PR_ERR("[rss] psram alloc failed");
        tal_psram_free(sg_extract_raw);
        tal_psram_free(sg_extract_best);
        tal_psram_free(resp);
        sg_extract_raw = sg_extract_best = NULL;
        return OPRT_MALLOC_FAILED;
    }

    for (int i = 0; i < RSS_SOURCE_CNT; i++) {
        uint16_t status = 0;
        OPERATE_RET rt;

        rt = __http_get(sg_sources[i].host, sg_sources[i].path, resp, RSS_RESP_BUF_SIZE, &status);
        if (rt != OPRT_OK || status != 200) {
            PR_WARN("[rss] %s failed rt=%d status=%d", sg_sources[i].name, rt, status);
            continue;
        }
        if (__parse_feed(resp, i, out) > 0) {
            ok_sources++;
            PR_NOTICE("[rss] %s: got items (total %d)", sg_sources[i].name, out->count);
        } else {
            PR_WARN("[rss] %s: no titles parsed", sg_sources[i].name);
        }
        /* Yield so UI / TLS can reclaim SRAM between handshakes. */
        tal_system_sleep(50);
    }

    tal_psram_free(resp);
    tal_psram_free(sg_extract_raw);
    tal_psram_free(sg_extract_best);
    sg_extract_raw = sg_extract_best = NULL;

    out->valid = (out->count > 0);
    PR_NOTICE("[rss] fetch done sources=%d items=%d", ok_sources, out->count);
    return out->valid ? OPRT_OK : OPRT_COM_ERROR;
}
