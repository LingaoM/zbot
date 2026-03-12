/*
 * Copyright (c) 2026 LingaoMeng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZephyrClaw - Agent Module Implementation
 *
 * Implements the ReAct (Reason + Act) loop for tool-augmented LLM interaction.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "agent.h"
#include "soul.h"
#include "memory.h"
#include "tools.h"
#include "llm_client.h"

LOG_MODULE_REGISTER(zephyrclaw_agent, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/* Agent state                                                          */
/* ------------------------------------------------------------------ */

static K_MUTEX_DEFINE(g_agent_mutex);
static volatile bool g_busy = false;

/* Static buffers (large, stack-allocated per call would overflow) */
static char g_messages_json[LLM_REQUEST_BUF_LEN];
static char g_tools_json[2048];
static char g_tool_result[TOOLS_RESULT_MAX_LEN];
static char g_summary_prompt[LLM_CONTENT_MAX_LEN];

/* ------------------------------------------------------------------ */
/* Summary generation                                                   */
/* ------------------------------------------------------------------ */

/*
 * After each complete exchange, ask the LLM to condense the conversation
 * into a short summary and persist it to NVS. This summary is injected
 * as context on the next boot.
 */
static void update_summary(void)
{
    const char *existing = memory_get_summary();
    const struct soul_config *soul = soul_get();

    /* Build a standalone messages array for the summary request.
     * Uses soul->system_prompt (same as main requests) so the same
     * model/provider routing applies — avoids "unknown-model" errors. */
    snprintf(g_summary_prompt, sizeof(g_summary_prompt),
             "Please summarise our conversation so far in 2-3 sentences "
             "for future context. Be factual and concise. "
             "Existing summary: %s",
             existing[0] ? existing : "(none)");

    /* Escape quotes in summary prompt for JSON */
    static char msgs_buf[LLM_REQUEST_BUF_LEN];
    int n = snprintf(msgs_buf, sizeof(msgs_buf),
                     "[{\"role\":\"system\",\"content\":\"%s\"},"
                     "{\"role\":\"user\",\"content\":\"%s\"}]",
                     soul->system_prompt,
                     g_summary_prompt);
    if (n < 0 || (size_t)n >= sizeof(msgs_buf)) {
        LOG_WRN("Summary prompt too long, skipping");
        return;
    }

    struct llm_response resp;
    int rc = llm_chat(msgs_buf, NULL, &resp);

    if (rc == 0 && resp.content[0] != '\0') {
        memory_save_summary(resp.content);
        LOG_INF("Summary updated (%zu chars)", strlen(resp.content));
    } else {
        LOG_WRN("Summary generation failed: %d", rc);
    }
}

/* ------------------------------------------------------------------ */
/* Public summary API (used by memory module via callback)              */
/* ------------------------------------------------------------------ */

int agent_request_summary(const char *context, char *out_buf, size_t out_len)
{
    if (!context || !out_buf || out_len == 0) {
        return -EINVAL;
    }

    const struct soul_config *soul = soul_get();

    static char msgs_buf[LLM_REQUEST_BUF_LEN];
    int n = snprintf(msgs_buf, sizeof(msgs_buf),
                     "[{\"role\":\"system\",\"content\":\"%s\"},"
                     "{\"role\":\"user\",\"content\":\""
                     "Please summarise the following conversation in 2-3 sentences "
                     "for future context. Be factual and concise.\\n%s\"}]",
                     soul->system_prompt,
                     context);
    if (n < 0 || (size_t)n >= sizeof(msgs_buf)) {
        LOG_WRN("agent_request_summary: prompt too long");
        return -ENOMEM;
    }

    struct llm_response resp;
    int rc = llm_chat(msgs_buf, NULL, &resp);

    if (rc == 0 && resp.content[0] != '\0') {
        strncpy(out_buf, resp.content, out_len - 1);
        out_buf[out_len - 1] = '\0';
        LOG_INF("agent_request_summary: %zu chars", strlen(out_buf));
        return 0;
    }

    LOG_WRN("agent_request_summary failed: %d", rc);
    return rc != 0 ? rc : -ENODATA;
}

/* ------------------------------------------------------------------ */
/* Core ReAct loop                                                      */
/* ------------------------------------------------------------------ */

static int react_loop(const char *user_input, char *out_buf, size_t out_len)
{
    int rc;

    /* Add user turn to history */
    rc = memory_add_turn("user", user_input);
    if (rc < 0) {
        LOG_ERR("Failed to add user turn: %d", rc);
        return rc;
    }

    /* Build tools JSON once */
    rc = tools_build_json(g_tools_json, sizeof(g_tools_json));
    if (rc < 0) {
        LOG_ERR("Failed to build tools JSON: %d", rc);
        /* Continue without tools */
        g_tools_json[0] = '\0';
    }

    int iterations = 0;

    while (iterations < AGENT_MAX_REACT_ITERATIONS) {
        iterations++;

        /* Build messages from current history + summary */
        rc = memory_build_messages_json(g_messages_json,
                                        sizeof(g_messages_json));
        if (rc < 0) {
            LOG_ERR("Failed to build messages JSON: %d", rc);
            break;
        }

        LOG_INF("ReAct iteration %d / %d",
                iterations, AGENT_MAX_REACT_ITERATIONS);

        /* Call LLM */
        struct llm_response resp;
        rc = llm_chat(g_messages_json,
                      g_tools_json[0] ? g_tools_json : NULL,
                      &resp);

        if (rc < 0) {
            LOG_ERR("LLM call failed: %d", rc);
            snprintf(out_buf, out_len,
                     "[Error: LLM request failed (%d)]", rc);
            return rc;
        }

        /* --- Tool call branch --- */
        if (resp.finish_reason == LLM_FINISH_TOOL_CALL && resp.has_tool_call) {
            LOG_INF("Tool requested: %s(%s)",
                    resp.tool_call.name, resp.tool_call.arguments);

            /* Add assistant's (tool-calling) message to history */
            char assistant_tool_msg[128];
            snprintf(assistant_tool_msg, sizeof(assistant_tool_msg),
                     "[Calling tool: %s]", resp.tool_call.name);
            memory_add_turn("assistant", assistant_tool_msg);

            /* Execute the tool */
            rc = tools_execute(resp.tool_call.name,
                               resp.tool_call.arguments,
                               g_tool_result, sizeof(g_tool_result));

            LOG_INF("Tool result: %s", g_tool_result);

            /* Add tool result as a user message (simulated tool role).
             * Buffer must fit: prefix(20) + name(64) + result(256) */
            char tool_msg[TOOLS_RESULT_MAX_LEN + LLM_TOOL_NAME_MAX_LEN + 24];
            snprintf(tool_msg, sizeof(tool_msg),
                     "[Tool result for %s]: %s",
                     resp.tool_call.name, g_tool_result);
            memory_add_turn("user", tool_msg);

            /* Continue loop — let LLM process tool result */
            continue;
        }

        /* --- Stop / final response --- */
        if (resp.content[0] != '\0') {
            strncpy(out_buf, resp.content, out_len - 1);
            out_buf[out_len - 1] = '\0';

            /* Add assistant response to history */
            memory_add_turn("assistant", resp.content);
            return 0;
        }

        /* Empty response — stop */
        LOG_WRN("Empty LLM response");
        snprintf(out_buf, out_len, "[No response from model]");
        return -ENODATA;
    }

    /* Max iterations reached */
    LOG_WRN("Max ReAct iterations reached");
    snprintf(out_buf, out_len,
             "[Max tool iterations (%d) reached. Last result: %s]",
             AGENT_MAX_REACT_ITERATIONS, g_tool_result);
    return -ELOOP;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int agent_init(void)
{
    LOG_INF("ZephyrClaw agent initialised (max ReAct iterations: %d)",
            AGENT_MAX_REACT_ITERATIONS);
    return 0;
}

int agent_run_sync(const char *input, char *out_buf, size_t out_len)
{
    if (!input || !out_buf) return -EINVAL;

    if (k_mutex_lock(&g_agent_mutex, K_MSEC(100)) != 0) {
        return -EBUSY;
    }

    g_busy = true;
    int rc = react_loop(input, out_buf, out_len);
    g_busy = false;

    k_mutex_unlock(&g_agent_mutex);
    return rc;
}

bool agent_is_busy(void)
{
    return g_busy;
}

/* Async processing via work queue */
struct agent_work_item {
    struct k_work        work;
    char                 input[AGENT_INPUT_MAX_LEN];
    agent_response_cb    cb;
    void                *user_data;
};

static struct agent_work_item g_work_item;

static void agent_work_handler(struct k_work *work)
{
    struct agent_work_item *item =
        CONTAINER_OF(work, struct agent_work_item, work);

    static char out_buf[AGENT_OUTPUT_MAX_LEN];

    g_busy = true;
    int rc = react_loop(item->input, out_buf, sizeof(out_buf));
    g_busy = false;

    if (item->cb) {
        if (rc < 0 && out_buf[0] == '\0') {
            snprintf(out_buf, sizeof(out_buf),
                     "[Agent error: %d]", rc);
        }
        item->cb(out_buf, item->user_data);
    }
}

int agent_submit_input(const char *input, agent_response_cb cb,
                       void *user_data)
{
    if (!input || strlen(input) == 0) return -EINVAL;

    if (g_busy) {
        LOG_WRN("Agent is busy");
        return -EBUSY;
    }

    if (strlen(input) >= AGENT_INPUT_MAX_LEN) {
        LOG_ERR("Input too long (%zu > %d)", strlen(input), AGENT_INPUT_MAX_LEN - 1);
        return -EINVAL;
    }

    strncpy(g_work_item.input, input, AGENT_INPUT_MAX_LEN - 1);
    g_work_item.input[AGENT_INPUT_MAX_LEN - 1] = '\0';
    g_work_item.cb        = cb;
    g_work_item.user_data = user_data;

    k_work_init(&g_work_item.work, agent_work_handler);
    k_work_submit(&g_work_item.work);

    return 0;
}
