/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZBot - Agent Module Implementation
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
#include "config.h"
#include "llm_client.h"

LOG_MODULE_REGISTER(zbot_agent, LOG_LEVEL_INF);

/* Static buffers (large, stack-allocated per call would overflow) */
static char g_tool_result[TOOLS_RESULT_MAX_LEN];

/*
 * Transient context for a single tool-call iteration.
 *
 * When the LLM returns finish_reason == tool_call, messages_cb receives
 * a pointer to this struct so it can append two extra messages after the
 * persistent history:
 *
 *   { "role": "assistant", "tool_calls": [ { "id": ..., "function": { ... } } ] }
 *   { "role": "tool",      "tool_call_id": ..., "content": <result> }
 *
 * On a normal (stop) turn the pointer is NULL and messages_cb just calls
 * memory_build_messages_json() as before.
 */
struct tool_turn_ctx {
	size_t pos;
	const struct llm_tool_call *call; /* tool call from the last LLM response */
	const char *result;               /* JSON result string from tools_execute */
};

struct summary_ctx {
	const char *roles[MEMORY_COMPRESS_COUNT];
	const char *contents[MEMORY_COMPRESS_COUNT];
	const char *prior;
	int count;
};

static int summary_messages_cb(char *buf, size_t buf_len, void *args)
{
	const struct llm_config *cfg = config_get();
	struct summary_ctx *ctx = args;
	size_t pos = 0;
	int n;

	n = snprintf(buf + pos, buf_len - pos,
		     "{\"model\":\"%s\","
		     "\"max_completion_tokens\":%d,"
		     "\"messages\":",
		     cfg->model, cfg->max_tokens);
	if (n < 0 || (size_t)n >= buf_len - pos) {
		return -ENOMEM;
	}

	pos += (size_t)n;

	n = snprintf(buf + pos, buf_len - pos, "[{\"role\":\"system\",\"content\":\"%s\"},",
		     AGENT_SYSTEM_PROMPT);
	if (n < 0 || (size_t)n >= buf_len - pos) {
		return -ENOMEM;
	}

	pos += (size_t)n;

	if (ctx->prior != NULL) {
		n = snprintf(buf + pos, buf_len - pos,
			     "{\"role\":\"user\",\"content\":\"[Prior summary] ");
		if (n < 0 || (size_t)n >= buf_len - pos) {
			return -ENOMEM;
		}
		pos += (size_t)n;

		n = zbot_json_escape(ctx->prior, buf + pos, buf_len - pos);
		if ((size_t)n >= buf_len - pos) {
			return -ENOMEM;
		}
		pos += (size_t)n;

		n = snprintf(buf + pos, buf_len - pos,
			     "\"},"
			     "{\"role\":\"assistant\",\"content\":\"Understood.\"},");
		if (n < 0 || (size_t)n >= buf_len - pos) {
			return -ENOMEM;
		}
		pos += (size_t)n;
	}

	n = snprintf(buf + pos, buf_len - pos, "{\"role\":\"user\",\"content\":\"%s\\n",
		     (ctx->prior != NULL)
			     ? "Update the prior summary to include the following new turns. "
			       "Keep the result under 3 sentences, factual and concise."
			     : "Summarise the following conversation in 2-3 sentences "
			       "for future context. Be factual and concise.");
	if (n < 0 || pos + (size_t)n >= buf_len) {
		return -ENOMEM;
	}

	pos += (size_t)n;

	for (int i = 0; i < ctx->count; i++) {
		n = snprintf(buf + pos, buf_len - pos, "%s: ", ctx->roles[i]);
		if (n < 0 || (size_t)n >= buf_len - pos) {
			return -ENOMEM;
		}
		pos += (size_t)n;

		n = zbot_json_escape(ctx->contents[i], buf + pos, buf_len - pos);
		if ((size_t)n >= buf_len - pos) {
			return -ENOMEM;
		}
		pos += (size_t)n;

		n = snprintf(buf + pos, buf_len - pos, "\\n");
		if (n < 0 || (size_t)n >= buf_len - pos) {
			return -ENOMEM;
		}
		pos += (size_t)n;
	}

	n = snprintf(buf + pos, buf_len - pos, "\"}]");
	if (n < 0 || pos + (size_t)n >= buf_len) {
		return -ENOMEM;
	}

	pos += (size_t)n;

	return (int)pos;
}

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
	struct tool_turn_ctx *tc = args;
	size_t pos;
	int n;

	/*
	 * First tool-call iteration: build the base history and cache the
	 * position so subsequent iterations can skip the rebuild.
	 */
	if (tc->pos == 0) {
		n = memory_build_messages_json(buf, buf_len);
		if (n < 0) {
			return n;
		}

		tc->pos = (size_t)n;
		return n;
	}

	/*
	 * Strip the closing ']' written by memory_build_messages_json so we
	 * can append more elements, then re-add it at the end.
	 */
	pos = tc->pos;
	if (pos == 0 || buf[pos - 1] != ']') {
		return -EINVAL;
	}
	pos--; /* overwrite the trailing ']' */

	/*
	 * Append the assistant tool_call message:
	 *   {"role":"assistant","tool_calls":[{"id":"...","type":"function",
	 *    "function":{"name":"...","arguments":"..."}}]}
	 *
	 * arguments is a JSON object — escape directly into buf when embedding
	 * as a JSON string value.
	 */
	n = snprintf(buf + pos, buf_len - pos,
		     ",{\"role\":\"assistant\","
		     "\"tool_calls\":[{"
		     "\"id\":\"%s\","
		     "\"type\":\"function\","
		     "\"function\":{\"name\":\"%s\",\"arguments\":\"",
		     tc->call->id, tc->call->name);
	if (n < 0 || (size_t)n >= buf_len - pos) {
		return -ENOMEM;
	}
	pos += (size_t)n;

	n = zbot_json_escape(tc->call->arguments, buf + pos, buf_len - pos);
	if ((size_t)n >= buf_len - pos) {
		return -ENOMEM;
	}
	pos += (size_t)n;

	n = snprintf(buf + pos, buf_len - pos, "\"}}]}");
	if (n < 0 || (size_t)n >= buf_len - pos) {
		return -ENOMEM;
	}
	pos += (size_t)n;

	/*
	 * Append the tool result message:
	 *   {"role":"tool","tool_call_id":"...","content":"..."}
	 *
	 * result is also a JSON object — escape directly into buf.
	 */
	n = snprintf(buf + pos, buf_len - pos,
		     ",{\"role\":\"tool\","
		     "\"tool_call_id\":\"%s\","
		     "\"content\":\"",
		     tc->call->id);
	if (n < 0 || (size_t)n >= buf_len - pos) {
		return -ENOMEM;
	}
	pos += (size_t)n;

	n = zbot_json_escape(tc->result, buf + pos, buf_len - pos);
	if ((size_t)n >= buf_len - pos) {
		return -ENOMEM;
	}
	pos += (size_t)n;

	n = snprintf(buf + pos, buf_len - pos, "\"}");
	if (n < 0 || (size_t)n >= buf_len - pos) {
		return -ENOMEM;
	}
	pos += (size_t)n;

	/* Re-add closing ']' */
	n = snprintf(buf + pos, buf_len - pos, "]");
	if (n < 0 || (size_t)n >= buf_len - pos) {
		return -ENOMEM;
	}
	pos += (size_t)n;

	tc->pos = pos;

	return (int)pos;
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
	struct tool_turn_ctx tc = {0};
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

		LOG_DBG("ReAct iteration %d / %d", iterations, AGENT_MAX_REACT_ITERATIONS);

		rc = llm_chat(messages_cb, tools_cb, &resp, &tc);
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

			/* Pass tool context to messages_cb on the next iteration */
			tc.call = &resp.tool_call;
			tc.result = g_tool_result;
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
	LOG_INF("zbot agent initialised (max ReAct iterations: %d)", AGENT_MAX_REACT_ITERATIONS);

	return 0;
}

static void agent_work_handler(struct k_work *work);

static struct agent_work_item {
	struct k_work work;
	char input[AGENT_INPUT_MAX_LEN];
	struct agent_response_ctx ctx;
} g_work_item = {
	.work = Z_WORK_INITIALIZER(agent_work_handler),
};

bool agent_is_busy(void)
{
	return k_work_busy_get(&g_work_item.work) & (K_WORK_RUNNING | K_WORK_QUEUED);
}

static void agent_work_handler(struct k_work *work)
{
	struct agent_work_item *item = CONTAINER_OF(work, struct agent_work_item, work);
	int rc;

	rc = react_loop(item->input, item->ctx.output, item->ctx.output_length);

	if (item->ctx.cb) {
		item->ctx.cb(rc, &item->ctx);
	}
}

int agent_submit_input(const char *input, const struct agent_response_ctx *ctx)
{
	if (!input || strlen(input) == 0 || !ctx || !ctx->output) {
		return -EINVAL;
	}

	if (agent_is_busy()) {
		LOG_WRN("Agent is busy");
		return -EBUSY;
	}

	if (strlen(input) >= AGENT_INPUT_MAX_LEN) {
		LOG_ERR("Input too long (%zu > %d)", strlen(input), AGENT_INPUT_MAX_LEN - 1);
		return -EINVAL;
	}

	strncpy(g_work_item.input, input, AGENT_INPUT_MAX_LEN - 1);
	g_work_item.input[AGENT_INPUT_MAX_LEN - 1] = '\0';

	g_work_item.ctx = *ctx;

	k_work_submit(&g_work_item.work);

	return 0;
}
