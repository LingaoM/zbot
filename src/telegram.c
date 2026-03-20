/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZBot - Telegram Bot Module
 *
 * Uses the Telegram Bot API (getUpdates long-poll) to receive messages and
 * forwards them to the agent.  The bot token is read from the zbot config
 * subsystem ("zbot/tg_token").
 *
 * Network notes:
 *   - api.telegram.org only accepts HTTPS (port 443).
 *   - TLS peer verification is skipped by default to avoid bundling yet
 *     another CA certificate.  Set CONFIG_ZBOT_TELEGRAM_TLS_VERIFY=y to
 *     require it (and supply the relevant CA cert).
 *
 * Thread model:
 *   A dedicated Zephyr thread polls getUpdates with a 30-second timeout.
 *   When a message arrives it is forwarded to agent_submit_input() and the
 *   reply is sent back via sendMessage.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/http/client.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>

#include "telegram.h"
#include "config.h"
#include "agent.h"
#include "json_util.h"

LOG_MODULE_REGISTER(zbot_telegram, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

#define TG_HOST        "api.telegram.org"
#define TG_PORT        443
#define TG_HTTP_TO_MS  40000   /* HTTP timeout: 30s poll + 10s margin */

#define TG_RX_BUF_LEN  4096
#define TG_TX_BUF_LEN  2048
#define TG_MSG_MAX_LEN 512     /* max Telegram message text           */
#define TG_SEND_MAX_LEN 1024   /* max reply text sent to Telegram     */

/* Telegram polling thread */
#define TG_STACK_SIZE  8192
#define TG_THREAD_PRIO 5

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */

static K_THREAD_STACK_DEFINE(g_tg_stack, TG_STACK_SIZE);
static struct k_thread g_tg_thread;
static volatile bool g_tg_running;
static volatile bool g_tg_stop;

/* Semaphore used to block the caller of telegram_stop() until the thread exits */
static K_SEM_DEFINE(g_tg_stopped_sem, 0, 1);

/* Semaphore used to synchronise agent response delivery */
static K_SEM_DEFINE(g_agent_done_sem, 0, 1);

struct tg_agent_work {
	int rc;
	char response[AGENT_OUTPUT_MAX_LEN];
};

static struct tg_agent_work g_agent_work;

/* ------------------------------------------------------------------ */
/* Simple JSON helpers (subset needed for Telegram responses)         */
/* ------------------------------------------------------------------ */

/*
 * Extract a JSON integer value following "key": <integer>.
 * Returns true on success.
 */
static bool tg_json_get_int(const char *json, const char *key, int64_t *out)
{
	char search[80];
	const char *pos;

	snprintf(search, sizeof(search), "\"%s\"", key);
	pos = strstr(json, search);
	if (!pos) {
		return false;
	}
	pos += strlen(search);
	while (*pos == ' ' || *pos == ':' || *pos == '\t') {
		pos++;
	}
	if (*pos == '-' || (*pos >= '0' && *pos <= '9')) {
		*out = (int64_t)strtoll(pos, NULL, 10);
		return true;
	}
	return false;
}

/* ------------------------------------------------------------------ */
/* HTTPS helpers                                                       */
/* ------------------------------------------------------------------ */

static int tg_connect(void)
{
	struct zsock_addrinfo hints = {0};
	struct zsock_addrinfo *res = NULL;
	int sock;
	int verify;
	int rc;

	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	rc = zsock_getaddrinfo(TG_HOST, "443", &hints, &res);
	if (rc != 0) {
		LOG_ERR("DNS resolution failed for " TG_HOST ": %d", rc);
		return -EHOSTUNREACH;
	}

	sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);
	if (sock < 0) {
		LOG_ERR("TLS socket create failed: errno=%d", -errno);
		zsock_freeaddrinfo(res);
		return -errno;
	}

	/* Skip peer certificate verification — avoids bundling CA cert.
	 * To enable: supply CA cert and use TLS_PEER_VERIFY_REQUIRED. */
	verify = TLS_PEER_VERIFY_NONE;
	zsock_setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
	zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, TG_HOST, sizeof(TG_HOST) - 1);

	rc = zsock_connect(sock, res->ai_addr, res->ai_addrlen);
	zsock_freeaddrinfo(res);
	if (rc < 0) {
		LOG_ERR("Connect to " TG_HOST " failed: %d", -errno);
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

/*
 * Perform an HTTPS GET request to <path> on api.telegram.org.
 * Response body is written into rx_buf (NUL-terminated).
 * Returns 0 on success, negative errno on failure.
 */
static int tg_get(const char *path, char *rx_buf, size_t rx_len)
{
	static const char *content_type = "Content-Type: application/json\r\n";
	struct http_request req = {0};
	int sock;
	int rc;

	const char *extra_headers[] = {content_type, NULL};

	sock = tg_connect();
	if (sock < 0) {
		return sock;
	}

	memset(rx_buf, 0, rx_len);

	req.method       = HTTP_GET;
	req.url          = path;
	req.host         = TG_HOST;
	req.protocol     = "HTTP/1.1";
	req.header_fields = extra_headers;
	req.response     = http_response_cb;
	req.recv_buf     = rx_buf;
	req.recv_buf_len = rx_len - 1;

	rc = http_client_req(sock, &req, TG_HTTP_TO_MS, NULL);
	zsock_close(sock);

	return rc < 0 ? rc : 0;
}

/*
 * Perform an HTTPS POST request to <path> with JSON body.
 */
static int tg_post(const char *path, const char *body, char *rx_buf, size_t rx_len)
{
	static const char *content_type = "Content-Type: application/json\r\n";
	struct http_request req = {0};
	int sock;
	int rc;

	const char *extra_headers[] = {content_type, NULL};

	sock = tg_connect();
	if (sock < 0) {
		return sock;
	}

	memset(rx_buf, 0, rx_len);

	req.method        = HTTP_POST;
	req.url           = path;
	req.host          = TG_HOST;
	req.protocol      = "HTTP/1.1";
	req.header_fields = extra_headers;
	req.response      = http_response_cb;
	req.payload       = body;
	req.payload_len   = strlen(body);
	req.recv_buf      = rx_buf;
	req.recv_buf_len  = rx_len - 1;

	rc = http_client_req(sock, &req, TG_HTTP_TO_MS, NULL);
	zsock_close(sock);

	return rc < 0 ? rc : 0;
}

/* ------------------------------------------------------------------ */
/* Telegram API helpers                                                */
/* ------------------------------------------------------------------ */

/*
 * Build the URL path for a Bot API method.
 * Example: /bot<token>/getUpdates
 */
static void tg_build_path(const char *method, char *buf, size_t len)
{
	const struct llm_config *cfg = config_get();

	snprintf(buf, len, "/bot%s/%s", cfg->tg_token, method);
}

/*
 * Send a text message to chat_id.
 */
static int tg_send_message(int64_t chat_id, const char *text)
{
	static char path[CONFIG_TG_TOKEN_MAX_LEN + 32];
	static char body[TG_TX_BUF_LEN];
	static char rx[TG_RX_BUF_LEN];
	int n;

	tg_build_path("sendMessage", path, sizeof(path));

	char escaped[TG_SEND_MAX_LEN];

	json_escape(text, escaped, sizeof(escaped));

	n = snprintf(body, sizeof(body),
		     "{\"chat_id\":%" PRId64 ",\"text\":\"%s\"}", chat_id, escaped);
	if (n < 0 || (size_t)n >= sizeof(body)) {
		return -ENOMEM;
	}

	int rc = tg_post(path, body, rx, sizeof(rx));

	if (rc < 0) {
		LOG_ERR("sendMessage to chat %" PRId64 " failed: %d", chat_id, rc);
	} else if (strstr(rx, "\"ok\":true") != NULL) {
		LOG_INF("sendMessage to chat %" PRId64 " OK", chat_id);
	} else {
		LOG_WRN("sendMessage to chat %" PRId64 ": unexpected response: %.80s", chat_id, rx);
		rc = -EIO;
	}

	return rc;
}

/* ------------------------------------------------------------------ */
/* Agent response callback                                             */
/* ------------------------------------------------------------------ */

static void tg_agent_cb(int err, struct agent_response_ctx *ctx)
{
	struct tg_agent_work *work = ctx->user_data;

	work->rc = err;
	k_sem_give(&g_agent_done_sem);
}

/* ------------------------------------------------------------------ */
/* Main polling loop                                                   */
/* ------------------------------------------------------------------ */

static void tg_poll_loop(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	static char rx[TG_RX_BUF_LEN];
	static char path[CONFIG_TG_TOKEN_MAX_LEN + 96];
	static char msg_text[TG_MSG_MAX_LEN];
	int64_t offset = 0;

	LOG_INF("Telegram polling started");
	g_tg_running = true;

	while (!g_tg_stop) {
		char base[CONFIG_TG_TOKEN_MAX_LEN + 32];
		int64_t update_id;
		int64_t chat_id;
		const char *pos;
		int rc;

		/* Build getUpdates path with timeout=30 and offset */
		tg_build_path("getUpdates", base, sizeof(base));

		snprintf(path, sizeof(path), "%s?timeout=30&offset=%" PRId64, base, offset);

		rc = tg_get(path, rx, sizeof(rx));

		if (g_tg_stop) {
			break;
		}

		if (rc < 0) {
			continue;
		}

		/* Iterate over update objects in the "result" array */
		pos = strstr(rx, "\"result\"");
		if (!pos) {
			LOG_DBG("No 'result' in response");
			continue;
		}

		/* Find the opening '[' of the result array */
		pos = strchr(pos, '[');
		if (!pos) {
			continue;
		}
		pos++; /* skip '[' */

		/* Walk each '{' object */
		while (!g_tg_stop) {
			/* Find next object */
			const char *obj = strchr(pos, '{');

			if (!obj) {
				break;
			}

			/* Find matching '}' — track nesting depth */
			const char *obj_end = obj + 1;
			int depth = 1;

			while (*obj_end && depth > 0) {
				if (*obj_end == '{') {
					depth++;
				} else if (*obj_end == '}') {
					depth--;
				}
				obj_end++;
			}

			/* Extract update_id and advance offset */
			if (!tg_json_get_int(obj, "update_id", &update_id)) {
				pos = obj_end;
				continue;
			}
			if (update_id >= offset) {
				offset = update_id + 1;
			}

			/* Extract message text and chat id */
			const char *msg_pos = strstr(obj, "\"message\"");

			if (msg_pos && msg_pos < obj_end) {
				bool has_text = json_get_str(msg_pos, "text",
							     msg_text, sizeof(msg_text)) > 0;
				/* chat id is inside the "chat" sub-object */
				const char *chat_pos = strstr(msg_pos, "\"chat\"");
				bool has_chat = chat_pos && chat_pos < obj_end &&
						tg_json_get_int(chat_pos, "id", &chat_id);

				if (has_text && has_chat) {
					LOG_INF("Telegram message from chat %" PRId64 ": %s",
						chat_id, msg_text);

					/* Submit to agent */
					struct agent_response_ctx ctx = {
						.output = g_agent_work.response,
						.output_length = sizeof(g_agent_work.response),
						.cb = tg_agent_cb,
						.user_data = &g_agent_work,
					};

					k_sem_reset(&g_agent_done_sem);

					rc = agent_submit_input(msg_text, &ctx);
					if (rc == 0) {
						/* Wait up to 60 s for response */
						if (k_sem_take(&g_agent_done_sem,
							       K_SECONDS(60)) == 0) {
							tg_send_message(chat_id,
								g_agent_work.rc == 0
								? g_agent_work.response
								: "Error processing request.");
						} else {
							tg_send_message(chat_id,
								"Request timed out.");
						}
					} else if (rc == -EBUSY) {
						tg_send_message(chat_id,
							"Agent is busy, please try again.");
					}
				}
			}

			pos = obj_end;
		}
	}

	LOG_INF("Telegram polling stopped");
	g_tg_running = false;
	k_sem_give(&g_tg_stopped_sem);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */
int telegram_start(void)
{
	if (g_tg_running) {
		return -EALREADY;
	}

	if (!config_has_tg_token()) {
		LOG_ERR("Telegram token not set. Use: zbot telegram token <token>");
		return -ENODEV;
	}

	g_tg_stop = false;
	k_sem_reset(&g_tg_stopped_sem);

	k_thread_create(&g_tg_thread, g_tg_stack, K_THREAD_STACK_SIZEOF(g_tg_stack),
			tg_poll_loop, NULL, NULL, NULL,
			TG_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&g_tg_thread, "tg_poll");

	return 0;
}

void telegram_stop(void)
{
	if (!g_tg_running) {
		return;
	}

	g_tg_stop = true;
	/* Wait for thread to exit (max 10 s) */
	k_sem_take(&g_tg_stopped_sem, K_SECONDS(10));
}

bool telegram_is_running(void)
{
	return g_tg_running;
}
