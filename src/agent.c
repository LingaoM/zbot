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
#include "memory.h"
#include "tools.h"
#include "llm_client.h"

LOG_MODULE_REGISTER(zephyrclaw_agent, LOG_LEVEL_INF);

static const char g_system_prompt[] =
	"You are ZephyrClaw, an open-source embedded AI agent running on a "
	"Nordic nRF7002-DK development board powered by Zephyr RTOS. "
	"You are concise, helpful, and hardware-aware. "
	"You can control GPIOs, read sensors, and manage the device. "
	"When using tools, always reason step-by-step before acting. "
	"Keep responses short and suitable for a serial terminal.";

const char *agent_get_system_prompt(void)
{
	return g_system_prompt;
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

static K_MUTEX_DEFINE(g_agent_mutex);
static volatile bool g_busy = false;

/* Static buffers (large, stack-allocated per call would overflow) */
static char g_tool_result[TOOLS_RESULT_MAX_LEN];

struct summary_ctx {
	const char *roles[MEMORY_COMPRESS_COUNT];
	const char *contents[MEMORY_COMPRESS_COUNT];
	const char *prior;
	int count;
};

static int summary_messages_cb(char *buf, size_t buf_len, void *args)
{
	struct summary_ctx *ctx = args;
	size_t pos = 0;
	int n;

	n = snprintf(buf + pos, buf_len - pos,
		     "[{\"role\":\"system\",\"content\":\"%s\"},", g_system_prompt);
	if (n < 0 || (size_t)n >= buf_len - pos) {
		goto too_long;
	}

	pos += (size_t)n;

	if (ctx->prior != NULL) {
		n = snprintf(buf + pos, buf_len - pos,
			     "{\"role\":\"user\",\"content\":\"[Prior summary] %s\"},"
			     "{\"role\":\"assistant\",\"content\":\"Understood.\"},",
			     ctx->prior);
		if (n < 0 || pos + (size_t)n >= buf_len) {
			goto too_long;
		}

		pos += (size_t)n;
	}

	n = snprintf(buf + pos, buf_len - pos,
		     "{\"role\":\"user\",\"content\":\"%s\\n",
		     (ctx->prior != NULL)
			     ? "Update the prior summary to include the following new turns. "
			       "Keep the result under 3 sentences, factual and concise."
			     : "Summarise the following conversation in 2-3 sentences "
			       "for future context. Be factual and concise.");
	if (n < 0 || pos + (size_t)n >= buf_len) {
		goto too_long;
	}

	pos += (size_t)n;

	for (int i = 0; i < ctx->count; i++) {
		n = snprintf(buf + pos, buf_len - pos, "%s: %s\\n",
			     ctx->roles[i], ctx->contents[i]);
		if (n < 0 || pos + (size_t)n >= buf_len) {
			goto too_long;
		}

		pos += (size_t)n;
	}

	n = snprintf(buf + pos, buf_len - pos, "\"}]");
	if (n < 0 || pos + (size_t)n >= buf_len) {
		goto too_long;
	}

	pos += (size_t)n;
	return (int)pos;

too_long:
	return -ENOMEM;
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

int agent_request_summary(agent_format_fn_t format_fn, char *out_buf, size_t out_len)
{
	struct summary_ctx ctx;
	struct llm_response resp;
	int count, rc;

	if (!format_fn || !out_buf || out_len == 0) {
		return -EINVAL;
	}

	count = format_fn(ctx.roles, ctx.contents, MEMORY_COMPRESS_COUNT);
	if (count < 0) {
		return count;
	}

	if (count == 0) {
		return -ENODATA;
	}

	ctx.count = count;
	ctx.prior = memory_get_summary();

	rc = llm_chat(summary_messages_cb, NULL, &resp, &ctx);
	if (rc == 0 && resp.content[0] != '\0') {
		strncpy(out_buf, resp.content, out_len - 1);
		out_buf[out_len - 1] = '\0';
		LOG_INF("agent_request_summary: %zu chars", strlen(out_buf));
		return 0;
	}

	LOG_WRN("agent_request_summary failed: %d", rc);
	return rc != 0 ? rc : -ENODATA;
}

static int messages_cb(char *buf, size_t buf_len, void *args)
{
	ARG_UNUSED(args);
	return memory_build_messages_json(buf, buf_len);
}

static int tools_cb(char *buf, size_t buf_len, void *args)
{
	ARG_UNUSED(args);
	return tools_build_json(buf, buf_len);
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

static int react_loop(const char *user_input, char *out_buf, size_t out_len)
{
	struct llm_response resp;
	int iterations = 0;
	int rc;

	/* Add user turn to history */
	rc = memory_add_turn("user", user_input);
	if (rc < 0) {
		LOG_ERR("Failed to add user turn: %d", rc);
		return rc;
	}

	while (iterations < AGENT_MAX_REACT_ITERATIONS) {
		iterations++;

		LOG_INF("ReAct iteration %d / %d", iterations, AGENT_MAX_REACT_ITERATIONS);

		/* Call LLM — messages and tools JSON built directly into req_body */
		rc = llm_chat(messages_cb, tools_cb, &resp, NULL);

		if (rc < 0) {
			LOG_ERR("LLM call failed: %d", rc);
			snprintf(out_buf, out_len, "[Error: LLM request failed (%d)]", rc);
			return rc;
		}

		/* --- Tool call branch --- */
		if (resp.finish_reason == LLM_FINISH_TOOL_CALL && resp.has_tool_call) {
			LOG_INF("Tool requested: %s(%s)", resp.tool_call.name,
				resp.tool_call.arguments);

			/* Execute the tool */
			rc = tools_execute(resp.tool_call.name, resp.tool_call.arguments,
					   g_tool_result, sizeof(g_tool_result));

			LOG_INF("Tool result: %s", g_tool_result);

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

	snprintf(out_buf, out_len, "[Max tool iterations (%d) reached. Last result: %s]",
		 AGENT_MAX_REACT_ITERATIONS, g_tool_result);

	return -ELOOP;
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

int agent_init(void)
{
	LOG_INF("ZephyrClaw agent initialised (max ReAct iterations: %d)",
		AGENT_MAX_REACT_ITERATIONS);

	return 0;
}

int agent_run_sync(const char *input, char *out_buf, size_t out_len)
{
	int rc;

	if (!input || !out_buf) {
		return -EINVAL;
	}

	if (k_mutex_lock(&g_agent_mutex, K_MSEC(100)) != 0) {
		return -EBUSY;
	}

	g_busy = true;
	rc = react_loop(input, out_buf, out_len);
	g_busy = false;

	k_mutex_unlock(&g_agent_mutex);
	return rc;
}

bool agent_is_busy(void)
{
	return g_busy;
}

struct agent_work_item {
	struct k_work work;
	char input[AGENT_INPUT_MAX_LEN];
	agent_response_cb cb;
	void *user_data;
};

static struct agent_work_item g_work_item;

static void agent_work_handler(struct k_work *work)
{
	struct agent_work_item *item = CONTAINER_OF(work, struct agent_work_item, work);
	static char out_buf[AGENT_OUTPUT_MAX_LEN];
	int rc;

	g_busy = true;
	rc = react_loop(item->input, out_buf, sizeof(out_buf));
	g_busy = false;

	if (item->cb) {
		if (rc < 0 && out_buf[0] == '\0') {
			snprintf(out_buf, sizeof(out_buf), "[Agent error: %d]", rc);
		}
		item->cb(out_buf, item->user_data);
	}
}

int agent_submit_input(const char *input, agent_response_cb cb, void *user_data)
{
	if (!input || strlen(input) == 0) {
		return -EINVAL;
	}

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
	g_work_item.cb = cb;
	g_work_item.user_data = user_data;

	k_work_init(&g_work_item.work, agent_work_handler);
	k_work_submit(&g_work_item.work);

	return 0;
}
