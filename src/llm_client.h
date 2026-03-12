/*
 * Copyright (c) 2026 LingaoMeng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZephyrClaw - LLM Client Module
 *
 * Sends HTTP POST requests to an OpenAI-compatible Chat Completions API
 * using Zephyr's HTTP client. The API key is taken from the soul at
 * call-time — it is never stored in flash.
 *
 * Response format: the module parses the JSON response and extracts
 * the assistant's message content and any tool_calls.
 */

#ifndef ZEPHYRCLAW_LLM_CLIENT_H
#define ZEPHYRCLAW_LLM_CLIENT_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum sizes */
#define LLM_REQUEST_BUF_LEN   4096
#define LLM_RESPONSE_BUF_LEN  4096
#define LLM_CONTENT_MAX_LEN   2048
#define LLM_TOOL_NAME_MAX_LEN 64
#define LLM_TOOL_ARGS_MAX_LEN 512

/** finish_reason values */
#define LLM_FINISH_STOP       0
#define LLM_FINISH_TOOL_CALL  1
#define LLM_FINISH_LENGTH     2
#define LLM_FINISH_ERROR     -1

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
    struct llm_tool_call tool_call;
    bool has_tool_call;

    /* finish_reason: LLM_FINISH_* */
    int finish_reason;

    /* HTTP status code received */
    int http_status;
};

/**
 * @brief Send a chat completion request to the LLM.
 *
 * Builds a JSON payload from the provided messages array and tools array,
 * sends it via HTTPS to the endpoint configured in the soul, and parses
 * the response into @p resp.
 *
 * @param messages_json  JSON array of message objects (from memory module).
 * @param tools_json     JSON array of tool descriptors (from tools module),
 *                       or NULL to disable tool calling.
 * @param resp           Output: parsed response.
 * @return 0 on success, negative errno on network/parse error.
 */
int llm_chat(const char *messages_json, const char *tools_json,
             struct llm_response *resp);

/**
 * @brief Send a simple single-turn request (no tools, no history).
 *
 * Convenience wrapper for tasks like summary generation.
 *
 * @param system_msg  System prompt override (or NULL to use soul default).
 * @param user_msg    User message text.
 * @param out_buf     Output buffer for the assistant's reply text.
 * @param out_len     Output buffer size.
 * @return 0 on success, negative on error.
 */
int llm_simple_request(const char *system_msg, const char *user_msg,
                       char *out_buf, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYRCLAW_LLM_CLIENT_H */
