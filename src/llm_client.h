/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZBot - LLM Client Module
 *
 * Sends HTTP POST requests to an OpenAI-compatible Chat Completions API
 * using Zephyr's HTTP client. The API key is taken from config at
 * call-time — it is never stored in flash.
 *
 * Response format: the module parses the JSON response and extracts
 * the assistant's message content and any tool_calls.
 */

#ifndef ZBOT_LLM_CLIENT_H
#define ZBOT_LLM_CLIENT_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum sizes */
#define LLM_REQUEST_BUF_LEN   8172
#define LLM_RESPONSE_BUF_LEN  4096

#define LLM_CONTENT_MAX_LEN   512
#define LLM_TOOL_NAME_MAX_LEN 64
#define LLM_TOOL_ARGS_MAX_LEN 256

/** finish_reason values */
#define LLM_FINISH_STOP      0
#define LLM_FINISH_TOOL_CALL 1
#define LLM_FINISH_LENGTH    2
#define LLM_FINISH_ERROR     -1

#define LLM_MAX_TOOL_CALLS   8

/** A parsed tool call from the LLM response */
struct llm_tool_call {
	char id[32];
	char name[LLM_TOOL_NAME_MAX_LEN];
	char arguments[LLM_TOOL_ARGS_MAX_LEN];
};

/** Parsed LLM response */
struct llm_response {
	/* Text content of the assistant message (may be empty on tool call) */
	char content[LLM_CONTENT_MAX_LEN];

	/* Tool call (valid when finish_reason == LLM_FINISH_TOOL_CALL) */
	struct llm_tool_call tool_calls[LLM_MAX_TOOL_CALLS];
	int tool_call_count;

	/* finish_reason: LLM_FINISH_* */
	int finish_reason;

	/* HTTP status code received */
	int http_status;
};

/**
 * @brief Callback to fill the messages JSON array into @p buf.
 * @param args  Caller-supplied context pointer passed through from llm_chat().
 * @return bytes written (>0), or negative errno on error.
 */
typedef int (*llm_messages_cb_t)(char *buf, size_t buf_len, void *args);

/**
 * @brief Callback to fill the tools JSON array into @p buf.
 * @param args  Caller-supplied context pointer passed through from llm_chat().
 * @return bytes written (>0), 0 if no tools, or negative errno on error.
 */
typedef int (*llm_tools_cb_t)(char *buf, size_t buf_len, void *args);

/**
 * @brief Initialise the LLM client — registers TLS CA certificate.
 *
 * Must be called once at boot before the first llm_chat() call.
 */
void llm_client_init(void);

/**
 * @brief Send a chat completion request to the LLM.
 *
 * Calls @p messages_cb and @p tools_cb to write JSON directly into the
 * request buffer, then sends the request and parses the response.
 *
 * @param messages_cb  Callback that writes the messages JSON array.
 * @param tools_cb     Callback that writes the tools JSON array, or NULL.
 * @param resp         Output: parsed response.
 * @param args         Opaque pointer forwarded to both callbacks.
 * @return 0 on success, negative errno on error.
 */
int llm_chat(llm_messages_cb_t messages_cb, llm_tools_cb_t tools_cb, struct llm_response *resp,
	     void *args);

#ifdef __cplusplus
}
#endif

#endif /* ZBOT_LLM_CLIENT_H */
