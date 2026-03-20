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
#include "json_util.h"

LOG_MODULE_REGISTER(zbot_agent, LOG_LEVEL_INF);

struct tool_turn_ctx {
	size_t pos;

	int call_count;

	const char *content;

	struct {
		const struct llm_tool_call *call;
		char results[LLM_MAX_TOOL_CALLS][TOOLS_RESULT_MAX_LEN];
	} react;
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

		n = json_escape(ctx->prior, buf + pos, buf_len - pos);
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

		n = json_escape(ctx->contents[i], buf + pos, buf_len - pos);
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
	 *   {"role":"assistant","content":"...","tool_calls":[{...}, ...]}
	 */
	n = snprintf(buf + pos, buf_len - pos, ",{\"role\":\"assistant\",\"content\":\"");
	if (n < 0 || (size_t)n >= buf_len - pos) {
		return -ENOMEM;
	}
	pos += (size_t)n;

	if (tc->content) {
		n = json_escape(tc->content, buf + pos, buf_len - pos);
		if ((size_t)n >= buf_len - pos) {
			return -ENOMEM;
		}
		pos += (size_t)n;
	}

	n = snprintf(buf + pos, buf_len - pos, "\",\"tool_calls\":[");
	if (n < 0 || (size_t)n >= buf_len - pos) {
		return -ENOMEM;
	}
	pos += (size_t)n;

	for (int i = 0; i < tc->call_count; i++) {
		const struct llm_tool_call *call = &tc->react.call[i];

		n = snprintf(buf + pos, buf_len - pos,
			     "%s{\"id\":\"%s\","
			     "\"type\":\"function\","
			     "\"function\":{\"name\":\"%s\",\"arguments\":\"",
			     i > 0 ? "," : "", call->id, call->name);
		if (n < 0 || (size_t)n >= buf_len - pos) {
			return -ENOMEM;
		}
		pos += (size_t)n;

		n = json_escape(call->arguments, buf + pos, buf_len - pos);
		if ((size_t)n >= buf_len - pos) {
			return -ENOMEM;
		}
		pos += (size_t)n;

		n = snprintf(buf + pos, buf_len - pos, "\"}}");
		if (n < 0 || (size_t)n >= buf_len - pos) {
			return -ENOMEM;
		}
		pos += (size_t)n;
	}

	n = snprintf(buf + pos, buf_len - pos, "]}");
	if (n < 0 || (size_t)n >= buf_len - pos) {
		return -ENOMEM;
	}
	pos += (size_t)n;

	/*
	 * Append one tool result message per tool call:
	 *   {"role":"tool","tool_call_id":"...","content":"..."}
	 */
	for (int i = 0; i < tc->call_count; i++) {
		n = snprintf(buf + pos, buf_len - pos,
			     ",{\"role\":\"tool\","
			     "\"tool_call_id\":\"%s\","
			     "\"content\":\"",
			     tc->react.call[i].id);
		if (n < 0 || (size_t)n >= buf_len - pos) {
			return -ENOMEM;
		}
		pos += (size_t)n;

		n = json_escape(tc->react.results[i], buf + pos, buf_len - pos);
		if ((size_t)n >= buf_len - pos) {
			return -ENOMEM;
		}
		pos += (size_t)n;

		n = snprintf(buf + pos, buf_len - pos, "\"}");
		if (n < 0 || (size_t)n >= buf_len - pos) {
			return -ENOMEM;
		}
		pos += (size_t)n;
	}

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
		if (resp.finish_reason == LLM_FINISH_TOOL_CALL && resp.tool_call_count > 0) {
			if (resp.content[0] != '\0') {
				LOG_INF("assistant: %s", resp.content);
			}

			tc.call_count = resp.tool_call_count;

			/* Copy calls and execute each tool */
			for (int i = 0; i < resp.tool_call_count; i++) {
				rc = tools_execute(resp.tool_calls[i].name,
						   resp.tool_calls[i].arguments,
						   tc.react.results[i], sizeof(tc.react.results[i]));
				if (rc < 0) {
					LOG_WRN("tool_call[%d] %s failed: %d", i,
						resp.tool_calls[i].name, rc);
				}
			}

			tc.react.call = resp.tool_calls;
			tc.content = resp.content;
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

	snprintf(out_buf, out_len, "[Max tool iterations (%d) reached]",
		 AGENT_MAX_REACT_ITERATIONS);

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
