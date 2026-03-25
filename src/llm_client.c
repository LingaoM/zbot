/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * zbot - LLM Client Implementation
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

#include "certs/openrouter/ca_certificate.h"

#include "llm_client.h"
#include "config.h"
#include "json_util.h"

LOG_MODULE_REGISTER(zbot_llm, LOG_LEVEL_INF);

/* HTTP request timeout (ms) */
#define LLM_HTTP_TIMEOUT_MS 30000

/*
 * Extract a JSON string value from a buffer.
 * Handles simple cases needed for LLM responses.
 */
/*
 * Parse the OpenAI chat completion response JSON.
 * Handles both regular text responses and tool_call responses.
 */
static int parse_llm_response(const char *json, struct llm_response *resp)
{
	char finish_reason[32] = {0};
	bool has_tool_calls_field = false;
	const char *tc_pos;
	const char *val;
	const char *fn_pos;

	memset(resp, 0, sizeof(*resp));
	resp->finish_reason = LLM_FINISH_ERROR;

	if (!json || json[0] == '\0') {
		LOG_ERR("Empty response body");
		return -ENODATA;
	}

	/* Check for error response */
	if (strstr(json, "\"error\"")) {
		char err_msg[128] = {0};

		json_get_str(json, "message", err_msg, sizeof(err_msg));
		LOG_ERR("LLM API error: %s", err_msg);
		return -EIO;
	}

	/* Extract finish_reason from choices[0].
	 * Some providers return finish_reason=null when tool_calls are present,
	 * so also check for the presence of a "tool_calls" array. */
	json_get_str(json, "finish_reason", finish_reason, sizeof(finish_reason));

	/* Detect tool_calls: must have the field AND it must not be null/empty.
	 * "tool_calls\":null" and "tool_calls\":[]" mean no tool call. */
	tc_pos = strstr(json, "\"tool_calls\"");
	if (tc_pos) {
		/* Skip past the key to the value */
		val = tc_pos + strlen("\"tool_calls\"");
		while (*val == ' ' || *val == ':' || *val == '\t') {
			val++;
		}
		/* Only treat as a real tool call if value starts with '[' (array)
		 * and the array is not empty '[]' */
		if (*val == '[' && *(val + 1) != ']') {
			has_tool_calls_field = true;
		}
	}

	if (strcmp(finish_reason, "tool_calls") == 0 || has_tool_calls_field) {
		resp->finish_reason = LLM_FINISH_TOOL_CALL;
	} else if (strcmp(finish_reason, "length") == 0) {
		resp->finish_reason = LLM_FINISH_LENGTH;
	} else {
		resp->finish_reason = LLM_FINISH_STOP;
	}

	/* Extract content */
	json_get_str(json, "content", resp->content, LLM_CONTENT_MAX_LEN);

	/* Parse tool_calls array if present */
	if (resp->finish_reason == LLM_FINISH_TOOL_CALL) {
		tc_pos = strstr(json, "\"tool_calls\"");
		if (tc_pos) {
			/* Skip to the opening '[' of the array */
			const char *arr = tc_pos + strlen("\"tool_calls\"");

			while (*arr == ' ' || *arr == ':' || *arr == '\t') {
				arr++;
			}
			if (*arr == '[') {
				arr++; /* skip '[' */
			}

			/* Iterate over each object '{' in the array */
			while (*arr != '\0' && *arr != ']' &&
			       resp->tool_call_count < LLM_MAX_TOOL_CALLS) {
				/* Find next '{' */
				while (*arr != '\0' && *arr != '{' && *arr != ']') {
					arr++;
				}
				if (*arr != '{') {
					break;
				}

				struct llm_tool_call *tc =
					&resp->tool_calls[resp->tool_call_count];

				json_get_str(arr, "id", tc->id, sizeof(tc->id));

				fn_pos = strstr(arr, "\"function\"");

				/* Find the closing '}' of this object to limit next search */
				const char *obj_end = arr + 1;
				int depth = 1;

				while (*obj_end != '\0' && depth > 0) {
					if (*obj_end == '{') {
						depth++;
					} else if (*obj_end == '}') {
						depth--;
					}
					obj_end++;
				}

				if (fn_pos && fn_pos < obj_end) {
					json_get_str(fn_pos, "name", tc->name,
							 LLM_TOOL_NAME_MAX_LEN);
					json_get_str(fn_pos, "arguments", tc->arguments,
							 LLM_TOOL_ARGS_MAX_LEN);
				}

				resp->tool_call_count++;
				arr = obj_end;
			}
		}
	}

	return 0;
}

static int build_request_body(llm_messages_cb_t messages_cb, llm_tools_cb_t tools_cb, char *buf,
			      size_t buf_len, void *args)
{
	const size_t tools_key_len = 9; /* ",\"tools\":" */
	size_t pos = 0;
	int tools_n;
	int n;

	n = messages_cb(buf + pos, buf_len - pos, args);
	if (n <= 0) {
		return n < 0 ? n : -EINVAL;
	}

	pos += n;

	if (tools_cb != NULL) {
		if (buf_len - pos < tools_key_len + 2) {
			return -ENOMEM;
		}

		tools_n = tools_cb(buf + pos + tools_key_len, buf_len - pos - tools_key_len, args);
		if (tools_n > 0) {
			memcpy(buf + pos, ",\"tools\":", tools_key_len);
			pos += tools_key_len + tools_n;

			n = snprintf(buf + pos, buf_len - pos, ",\"tool_choice\":\"auto\"");
			if (n < 0 || (size_t)n >= buf_len - pos) {
				return -ENOMEM;
			}

			pos += n;
		} else if (tools_n < 0) {
			return tools_n;
		}
	}

	n = snprintf(buf + pos, buf_len - pos, "}");
	if (n < 0 || (size_t)n >= buf_len - pos) {
		return -ENOMEM;
	}

	pos += n;
	return (int)pos;
}

void llm_client_init(void)
{
	int rc;

	rc = tls_credential_add(CA_CERTIFICATE_TAG, TLS_CREDENTIAL_CA_CERTIFICATE,
				ca_openrouter_certificate, sizeof(ca_openrouter_certificate));
	if (rc < 0 && rc != -EEXIST) {
		LOG_ERR("Failed to add CA certificate: %d", rc);
	}
}

static int resolve_and_connect(const struct llm_config *cfg)
{
	struct zsock_addrinfo hints = {0};
	struct zsock_addrinfo *res = NULL;
	char port_str[8];
	int sock = -1;
	int rc;

	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	snprintf(port_str, sizeof(port_str), "%u", cfg->port);

	rc = zsock_getaddrinfo(cfg->endpoint_host, port_str, &hints, &res);
	if (rc != 0) {
		LOG_ERR("DNS resolution failed for %s: %d", cfg->endpoint_host, rc);
		return -EHOSTUNREACH;
	}

	if (cfg->use_tls) {
		sec_tag_t sec_tag_list[] = {CA_CERTIFICATE_TAG};
		int verify;

		sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);
		if (sock < 0) {
			LOG_ERR("TLS socket create failed: errno=%d", -errno);
			zsock_freeaddrinfo(res);
			return -errno;
		}

		rc = zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST,
				      sec_tag_list, sizeof(sec_tag_list));
		if (rc < 0) {
			LOG_ERR("TLS_SEC_TAG_LIST failed: errno=%d", -errno);
			zsock_freeaddrinfo(res);
			return -errno;
		}

		verify = cfg->tls_verify ? TLS_PEER_VERIFY_REQUIRED : TLS_PEER_VERIFY_NONE;
		rc = zsock_setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
		if (rc < 0) {
			LOG_ERR("TLS_PEER_VERIFY failed: errno=%d", -errno);
			zsock_freeaddrinfo(res);
			return -errno;
		}

		rc = zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME,
				      cfg->endpoint_host,
				      strlen(cfg->endpoint_host));
		if (rc < 0) {
			LOG_ERR("TLS_HOSTNAME failed: errno=%d", -errno);
			zsock_freeaddrinfo(res);
			return -errno;
		}
	} else {
		sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	}

	if (sock < 0) {
		LOG_ERR("Failed to create socket: %d", -errno);
		zsock_freeaddrinfo(res);
		return -errno;
	}

	rc = zsock_connect(sock, res->ai_addr, res->ai_addrlen);
	zsock_freeaddrinfo(res);

	if (rc < 0) {
		LOG_ERR("Connect failed: %d", -errno);
		zsock_close(sock);
		return -errno;
	}

	return sock;
}

static int http_response_cb(struct http_response *rsp, enum http_final_call final_data,
			    void *user_data)
{
	ARG_UNUSED(rsp);
	ARG_UNUSED(final_data);
	ARG_UNUSED(user_data);

	return 0;
}

int llm_chat(llm_messages_cb_t messages_cb, llm_tools_cb_t tools_cb, struct llm_response *resp,
	     void *args)
{
	static const char *referer_header = "HTTP-Referer: https://github.com/LingaoM/zbot\r\n";
	static const char *content_type = "Content-Type: application/json\r\n";
	static const char *title_header = "X-Title: zbot tests\r\n";
	static char provider_header[CONFIG_PROVIDER_ID_MAX_LEN + 32];
	static char auth_header[CONFIG_API_KEY_MAX_LEN + 32];
	static char rsp_body[LLM_RESPONSE_BUF_LEN];
	static char req_body[LLM_REQUEST_BUF_LEN];
	const struct llm_config *cfg;
	struct http_request req = {0};
	int body_len;
	int sock;
	int rc;

	if (!messages_cb || !resp) {
		return -EINVAL;
	}

	cfg = config_get();

	if (!config_has_api_key()) {
		LOG_ERR("API key not set. Use: zbot key <your-api-key>");
		return -EACCES;
	}

	/* Build request body */
	body_len = build_request_body(messages_cb, tools_cb, req_body, sizeof(req_body), args);
	if (body_len < 0) {
		LOG_ERR("Failed to build request body: %d", body_len);
		return body_len;
	}

	/* Connect */
	sock = resolve_and_connect(cfg);
	if (sock < 0) {
		return sock;
	}

	/* Build Authorization header */
	snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s\r\n", cfg->api_key);

	/* NOTE: Do NOT set Content-Length manually — Zephyr HTTP client adds it
	 * automatically from payload_len. A duplicate Content-Length causes HTTP 400. */

	/* Build X-Model-Provider-Id header (only if provider_id is set) */
	provider_header[0] = '\0';
	if (cfg->provider_id[0] != '\0') {
		snprintf(provider_header, sizeof(provider_header), "X-Model-Provider-Id: %s\r\n",
			 cfg->provider_id);
	}

	/* Prepare HTTP request */
	const char *extra_headers[] = {
		auth_header,
		content_type,
		referer_header,
		title_header,
		cfg->provider_id[0] ? provider_header : NULL,
		NULL,
	};

	/* Prepare receive buffer and response struct */
	memset(rsp_body, 0, sizeof(rsp_body));

	req.method = HTTP_POST;
	req.url = cfg->endpoint_path;
	req.host = cfg->endpoint_host;
	req.protocol = "HTTP/1.1";
	req.header_fields = extra_headers;
	req.response = http_response_cb;
	req.payload = req_body;
	req.payload_len = (size_t)body_len;
	req.recv_buf = rsp_body;
	req.recv_buf_len = sizeof(rsp_body) - 1;

	LOG_DBG("Sending LLM request to %s%s", cfg->endpoint_host, cfg->endpoint_path);

	rc = http_client_req(sock, &req, LLM_HTTP_TIMEOUT_MS, NULL);
	zsock_close(sock);

	if (rc < 0) {
		LOG_ERR("HTTP request failed: %d", rc);
		return rc;
	}

	resp->http_status = req.internal.response.http_status_code;

	/* Parse the response JSON */
	rc = parse_llm_response(rsp_body, resp);
	return rc;
}
