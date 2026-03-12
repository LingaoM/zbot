/*
 * Copyright (c) 2026 LingaoMeng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZephyrClaw - Agent Module
 *
 * The Agent is the core reasoning loop (ReAct pattern):
 *
 *   1. Receive user input
 *   2. Add to memory history
 *   3. Build messages JSON (system + summary + history)
 *   4. Call LLM with tools schema
 *   5. If finish_reason == tool_call:
 *        a. Execute the requested tool
 *        b. Add tool result to history as "tool" role message
 *        c. Go to step 3 (loop until stop or max iterations)
 *   6. Return final assistant response
 *   7. Request summary update from LLM and persist to NVS
 *
 * The agent runs in its own Zephyr thread. User input is pushed via
 * agent_submit_input(). The response is delivered via a callback.
 */

#ifndef ZEPHYRCLAW_AGENT_H
#define ZEPHYRCLAW_AGENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum ReAct loop iterations before forcing a stop */
#define AGENT_MAX_REACT_ITERATIONS 10

#define AGENT_INPUT_MAX_LEN    512
#define AGENT_OUTPUT_MAX_LEN   1024

/**
 * @brief Response callback — called from the agent thread when a
 *        response is ready.
 *
 * @param response  Null-terminated response string.
 * @param user_data Opaque pointer passed to agent_submit_input().
 */
typedef void (*agent_response_cb)(const char *response, void *user_data);

/**
 * @brief Initialise the agent (call after soul, memory, tools, skills init).
 * @return 0 on success.
 */
int agent_init(void);

/**
 * @brief Submit a user message for processing.
 *
 * Thread-safe. The agent processes the message asynchronously and calls
 * @p cb when the response is ready.
 *
 * @param input    User input string.
 * @param cb       Response callback function.
 * @param user_data Passed through to callback.
 * @return 0 on success, -EBUSY if agent is already processing, -EINVAL on bad input.
 */
int agent_submit_input(const char *input, agent_response_cb cb,
                       void *user_data);

/**
 * @brief Check if the agent is currently processing a request.
 */
bool agent_is_busy(void);

/**
 * @brief Run one complete ReAct cycle synchronously (blocking).
 *
 * Suitable for use from the shell thread when you want a simple
 * blocking call.
 *
 * @param input    User input.
 * @param out_buf  Output buffer for final response.
 * @param out_len  Output buffer size.
 * @return 0 on success, negative on error.
 */
int agent_run_sync(const char *input, char *out_buf, size_t out_len);

#ifdef __cplusplus
}
#endif

/**
 * @brief Request a summary of the provided conversation context.
 *
 * Used by the memory module (via callback) when the history pool is full.
 * Sends a standalone LLM request and writes the result to out_buf.
 *
 * @param context  String containing the conversation turns to summarise.
 * @param out_buf  Output buffer for the summary text.
 * @param out_len  Output buffer size.
 * @return 0 on success, negative on error.
 */
int agent_request_summary(const char *context, char *out_buf, size_t out_len);

#endif /* ZEPHYRCLAW_AGENT_H */
