/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * tool_exec — dispatch LLM tool calls to registered skills.
 *
 * The LLM calls: tool_exec({"tool": "<name>", "args": {...}})
 *
 * This handler extracts "tool" and "args", then calls skill_run(name, args).
 * All sub-tools (gpio_read, gpio_write, get_uptime, …) are registered as
 * skills via SKILL_DEFINE and live in src/skills/<name>/.
 */

#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "tools.h"
#include "skill.h"
#include "json_util.h"

/*
 * Extract the raw "args" sub-object from {"tool":"...","args":{...}}.
 * Returns pointer to '{', sets *out_len; NULL if "args" key is absent.
 */
static const char *extract_args(const char *json, size_t *out_len)
{
	const char *p;
	int depth;

	p = strstr(json, "\"args\"");
	if (!p) {
		return NULL;
	}
	p += 6;
	while (*p == ' ' || *p == ':' || *p == '\t') {
		p++;
	}
	if (*p != '{') {
		return NULL;
	}

	const char *start = p;

	depth = 0;
	do {
		if (*p == '{') {
			depth++;
		} else if (*p == '}') {
			depth--;
		}
		p++;
	} while (*p != '\0' && depth > 0);

	if (out_len) {
		*out_len = (size_t)(p - start);
	}
	return start;
}

static int tool_exec_handler(const char *args_json, char *result, size_t res_len)
{
	char name[SKILL_NAME_MAX_LEN] = {0};
	const char *args_ptr;
	size_t args_len;
	char args_buf[192];

	if (json_get_str(args_json, "tool", name, sizeof(name)) < 0) {
		snprintf(result, res_len,
			 "{\"error\":\"tool_exec: missing 'tool' field\"}");
		return -EINVAL;
	}

	args_ptr = extract_args(args_json, &args_len);
	if (args_ptr && args_len > 0 && args_len < sizeof(args_buf)) {
		memcpy(args_buf, args_ptr, args_len);
		args_buf[args_len] = '\0';
	} else {
		args_buf[0] = '{';
		args_buf[1] = '}';
		args_buf[2] = '\0';
	}

	return skill_run(name, args_buf, result, res_len);
}

TOOL_DEFINE(tool_exec, "tool_exec",
	    "Execute a hardware or system operation using a specific skill.",
	    "{\"type\":\"object\","
	    "\"properties\":{"
	    "\"tool\":{\"type\":\"string\","
	    "\"description\":\"The skill name to execute.\"},"
	    "\"args\":{\"type\":\"object\","
	    "\"description\":\"Arguments for the selected skill. The structure depends on the skill.\"}"
	    "},\"required\":[\"tool\", \"args\"]}",
	    tool_exec_handler);
