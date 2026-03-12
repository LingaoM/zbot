/*
 * SPDX-License-Identifier: Apache-2.0
 * ZephyrClaw - LLM Client Implementation
 *
 * Uses Zephyr's BSD-socket API + HTTP client to call OpenAI-compatible APIs.
 * TLS (mbedTLS) is used for HTTPS. The CA certificate bundle must be provided
 * at build time (see ca_cert.h) or TLS verification can be skipped for dev.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/http/client.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "llm_client.h"
#include "soul.h"

LOG_MODULE_REGISTER(zephyrclaw_llm, LOG_LEVEL_INF);

/* TLS credential tag for the CA certificate */
#define LLM_TLS_TAG 42

/* HTTP request timeout (ms) */
#define LLM_HTTP_TIMEOUT_MS 30000

/* ------------------------------------------------------------------ */
/* Response accumulation buffer                                         */
/* ------------------------------------------------------------------ */

static char g_recv_buf[LLM_RESPONSE_BUF_LEN];
static size_t g_recv_len;

/* http_response_cb_t requires int return since Zephyr 4.x */
static int http_response_cb(struct http_response *rsp,
                             enum http_final_call final_data,
                             void *user_data)
{
    if (rsp->data_len > 0) {
        size_t to_copy = rsp->data_len;
        if (g_recv_len + to_copy >= LLM_RESPONSE_BUF_LEN - 1) {
            to_copy = LLM_RESPONSE_BUF_LEN - 1 - g_recv_len;
        }
        if (to_copy > 0) {
            memcpy(g_recv_buf + g_recv_len, rsp->recv_buf, to_copy);
            g_recv_len += to_copy;
        }
    }

    if (final_data == HTTP_DATA_FINAL) {
        g_recv_buf[g_recv_len] = '\0';
        LOG_DBG("HTTP response complete (%zu bytes)", g_recv_len);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* JSON response parser                                                 */
/* ------------------------------------------------------------------ */

/*
 * Extract a JSON string value from a buffer.
 * Handles simple cases needed for LLM responses.
 */
static int extract_json_str(const char *json, const char *key,
                             char *out, size_t out_len)
{
    if (!json || !key || !out) return -EINVAL;

    char search[80];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return -ENOENT;

    pos += strlen(search);
    while (*pos == ' ' || *pos == ':' || *pos == '\t') pos++;
    if (*pos == 'n') { /* null */
        out[0] = '\0';
        return 0;
    }
    if (*pos != '"') return -ENOENT;
    pos++;

    size_t i = 0;
    while (*pos != '\0' && i + 1 < out_len) {
        if (*pos == '\\') {
            pos++;
            if (*pos == 'n')       { out[i++] = '\n'; }
            else if (*pos == 't')  { out[i++] = '\t'; }
            else if (*pos == '"')  { out[i++] = '"';  }
            else if (*pos == '\\') { out[i++] = '\\'; }
            else                   { out[i++] = *pos; }
            pos++;
        } else if (*pos == '"') {
            break;
        } else {
            out[i++] = *pos++;
        }
    }
    out[i] = '\0';
    return (int)i;
}

/*
 * Parse the OpenAI chat completion response JSON.
 * Handles both regular text responses and tool_call responses.
 */
static int parse_llm_response(const char *json, struct llm_response *resp)
{
    memset(resp, 0, sizeof(*resp));
    resp->finish_reason = LLM_FINISH_ERROR;

    if (!json || json[0] == '\0') {
        LOG_ERR("Empty response body");
        return -ENODATA;
    }

    /* Check for error response */
    if (strstr(json, "\"error\"")) {
        char err_msg[128] = {0};
        extract_json_str(json, "message", err_msg, sizeof(err_msg));
        LOG_ERR("LLM API error: %s", err_msg);
        return -EIO;
    }

    /* Extract finish_reason from choices[0].
     * Some providers return finish_reason=null when tool_calls are present,
     * so also check for the presence of a "tool_calls" array. */
    char finish_reason[32] = {0};
    extract_json_str(json, "finish_reason", finish_reason, sizeof(finish_reason));

    /* Detect tool_calls: must have the field AND it must not be null/empty.
     * "tool_calls\":null" and "tool_calls\":[]" mean no tool call. */
    bool has_tool_calls_field = false;
    const char *tc_pos = strstr(json, "\"tool_calls\"");
    if (tc_pos) {
        /* Skip past the key to the value */
        const char *val = tc_pos + strlen("\"tool_calls\"");
        while (*val == ' ' || *val == ':' || *val == '\t') val++;
        /* Only treat as a real tool call if value starts with '[' (array)
         * and the array is not empty '[]' */
        if (*val == '[' && *(val + 1) != ']') {
            has_tool_calls_field = true;
        }
    }

    if (strcmp(finish_reason, "tool_calls") == 0 || has_tool_calls_field) {
        resp->finish_reason = LLM_FINISH_TOOL_CALL;
        resp->has_tool_call = true;
    } else if (strcmp(finish_reason, "length") == 0) {
        resp->finish_reason = LLM_FINISH_LENGTH;
    } else {
        resp->finish_reason = LLM_FINISH_STOP;
    }

    /* Extract content */
    extract_json_str(json, "content", resp->content, LLM_CONTENT_MAX_LEN);

    /* Parse tool_calls if present */
    if (resp->has_tool_call) {
        const char *tc_pos = strstr(json, "\"tool_calls\"");
        if (tc_pos) {
            /* Extract tool call id */
            extract_json_str(tc_pos, "id",
                             resp->tool_call.id,
                             sizeof(resp->tool_call.id));
            /* Extract function name */
            const char *fn_pos = strstr(tc_pos, "\"function\"");
            if (fn_pos) {
                extract_json_str(fn_pos, "name",
                                 resp->tool_call.name,
                                 LLM_TOOL_NAME_MAX_LEN);
                /* Arguments is a JSON string (escaped JSON) */
                extract_json_str(fn_pos, "arguments",
                                 resp->tool_call.arguments,
                                 LLM_TOOL_ARGS_MAX_LEN);
            }
            LOG_INF("Tool call: %s(%s)",
                    resp->tool_call.name, resp->tool_call.arguments);
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* HTTP request builder                                                 */
/* ------------------------------------------------------------------ */

static int build_request_body(const char *messages_json,
                               const char *tools_json,
                               char *buf, size_t buf_len)
{
    const struct soul_config *soul = soul_get();
    size_t pos = 0;
    int n;

    /* Emit temperature as "X.YY" using integer arithmetic to avoid
     * float->double promotion warning (-Wdouble-promotion). */
    n = snprintf(buf + pos, buf_len - pos,
                 "{\"model\":\"%s\","
                 "\"max_completion_tokens\":%d,"
                 "\"messages\":%s",
                 soul->model,
                 soul->max_tokens,
                 messages_json);
    if (n < 0 || (size_t)n >= buf_len - pos) return -ENOMEM;
    pos += n;

    if (tools_json && tools_json[0] != '\0') {
        /* Non-empty tools array: let model choose */
        n = snprintf(buf + pos, buf_len - pos,
                     ",\"tools\":%s,\"tool_choice\":\"auto\"",
                     tools_json);
        if (n < 0 || (size_t)n >= buf_len - pos) return -ENOMEM;
        pos += n;
    }

    n = snprintf(buf + pos, buf_len - pos, "}");
    if (n < 0 || (size_t)n >= buf_len - pos) return -ENOMEM;
    pos += n;

    return (int)pos;
}

/* ------------------------------------------------------------------ */
/* Socket / HTTP helpers                                                */
/* ------------------------------------------------------------------ */

static int resolve_and_connect(const struct soul_config *soul)
{
    struct zsock_addrinfo hints = {0};
    struct zsock_addrinfo *res = NULL;
    char port_str[8];
    int sock = -1;

    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port_str, sizeof(port_str), "%u", soul->port);

    int rc = zsock_getaddrinfo(soul->endpoint_host, port_str, &hints, &res);
    if (rc != 0) {
        LOG_ERR("DNS resolution failed for %s: %d",
                soul->endpoint_host, rc);
        return -EHOSTUNREACH;
    }

    if (soul->use_tls) {
        sec_tag_t sec_tag_list[] = { LLM_TLS_TAG };

        sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);
        if (sock >= 0) {
            zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST,
                             sec_tag_list, sizeof(sec_tag_list));
            zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME,
                             soul->endpoint_host,
                             strlen(soul->endpoint_host));
            /* For development: skip peer verify if no CA cert loaded */
            int verify = TLS_PEER_VERIFY_NONE;
            zsock_setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY,
                             &verify, sizeof(verify));
        }
    } else {
        sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    }

    if (sock < 0) {
        LOG_ERR("Failed to create socket: %d", errno);
        zsock_freeaddrinfo(res);
        return -errno;
    }

    rc = zsock_connect(sock, res->ai_addr, res->ai_addrlen);
    zsock_freeaddrinfo(res);

    if (rc < 0) {
        LOG_ERR("Connect failed: %d", errno);
        zsock_close(sock);
        return -errno;
    }

    return sock;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int llm_chat(const char *messages_json, const char *tools_json,
             struct llm_response *resp)
{
    if (!messages_json || !resp) return -EINVAL;

    const struct soul_config *soul = soul_get();

    if (!soul_has_api_key()) {
        LOG_ERR("API key not set. Use: claw key <your-api-key>");
        return -EACCES;
    }

    /* Build request body */
    static char req_body[LLM_REQUEST_BUF_LEN];
    int body_len = build_request_body(messages_json, tools_json,
                                      req_body, sizeof(req_body));
    if (body_len < 0) {
        LOG_ERR("Failed to build request body: %d", body_len);
        return body_len;
    }

    /* Connect */
    int sock = resolve_and_connect(soul);
    if (sock < 0) {
        return sock;
    }

    /* Build Authorization header */
    static char auth_header[SOUL_API_KEY_MAX_LEN + 32];
    snprintf(auth_header, sizeof(auth_header),
             "Authorization: Bearer %s\r\n", soul->api_key);

    /* NOTE: Do NOT set Content-Length manually — Zephyr HTTP client adds it
     * automatically from payload_len. A duplicate Content-Length causes HTTP 400. */

    /* Build X-Model-Provider-Id header (only if provider_id is set) */
    static char provider_header[SOUL_PROVIDER_ID_MAX_LEN + 32];
    provider_header[0] = '\0';
    if (soul->provider_id[0] != '\0') {
        snprintf(provider_header, sizeof(provider_header),
                 "X-Model-Provider-Id: %s\r\n", soul->provider_id);
    }

    /* Prepare HTTP request */
    static char content_type[] = "Content-Type: application/json\r\n";
    static char referer_header[] = "HTTP-Referer: https://github.com/LingaoM/zephyrclaw\r\n";
    static char title_header[] = "X-Title: zephyrclaw tests\r\n";

    const char *extra_headers[] = {
        auth_header,
        content_type,
        referer_header,
        title_header,
        soul->provider_id[0] ? provider_header : NULL,
        NULL,
    };

    /* Prepare receive buffer and response struct */
    static uint8_t http_recv_buf[LLM_RESPONSE_BUF_LEN];
    memset(g_recv_buf, 0, sizeof(g_recv_buf));
    g_recv_len = 0;

    struct http_request req = {0};
    req.method         = HTTP_POST;
    req.url            = soul->endpoint_path;
    req.host           = soul->endpoint_host;
    req.protocol       = "HTTP/1.1";
    req.header_fields  = extra_headers;
    req.payload        = req_body;
    req.payload_len    = (size_t)body_len;
    req.response       = http_response_cb;
    req.recv_buf       = http_recv_buf;
    req.recv_buf_len   = sizeof(http_recv_buf);

    LOG_INF("Sending LLM request to %s%s",
            soul->endpoint_host, soul->endpoint_path);

    int rc = http_client_req(sock, &req, LLM_HTTP_TIMEOUT_MS, NULL);
    zsock_close(sock);

    if (rc < 0) {
        LOG_ERR("HTTP request failed: %d", rc);
        return rc;
    }

    resp->http_status = req.internal.response.http_status_code;

    /* Parse the response JSON */
    rc = parse_llm_response(g_recv_buf, resp);
    return rc;
}

int llm_simple_request(const char *system_msg, const char *user_msg,
                       char *out_buf, size_t out_len)
{
    if (!user_msg || !out_buf) return -EINVAL;

    const struct soul_config *soul = soul_get();
    const char *sys = system_msg ? system_msg : soul->system_prompt;

    /* Build minimal messages JSON inline */
    static char msgs[LLM_REQUEST_BUF_LEN];
    int n = snprintf(msgs, sizeof(msgs),
                     "[{\"role\":\"system\",\"content\":\"%s\"},"
                     "{\"role\":\"user\",\"content\":\"%s\"}]",
                     sys, user_msg);
    if (n < 0 || (size_t)n >= sizeof(msgs)) return -ENOMEM;

    struct llm_response resp;
    int rc = llm_chat(msgs, NULL, &resp);
    if (rc < 0) return rc;

    strncpy(out_buf, resp.content, out_len - 1);
    out_buf[out_len - 1] = '\0';
    return 0;
}
