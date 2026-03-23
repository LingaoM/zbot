/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * read_skill — fetch full Markdown documentation for a skill
 *
 * Allows the LLM to retrieve complete skill documentation on demand
 * without embedding it in every request.
 */

#include <stdio.h>
#include <errno.h>

#include "tools.h"
#include "skill.h"
#include "json_util.h"

static int read_skill_handler(const char *args_json, char *result, size_t res_len)
{
	char name[SKILL_NAME_MAX_LEN] = {0};

	if (json_get_str(args_json, "name", name, sizeof(name)) < 0 || name[0] == '\0') {
		snprintf(result, res_len,
			 "{\"error\":\"read_skill: missing 'name' field\"}");
		return -EINVAL;
	}

	return skill_read_content(name, result, res_len);
}

TOOL_DEFINE(read_skill, "read_skill",
	    "Read the full Markdown documentation of a skill before running it.",
	    "{\"type\":\"object\","
	    "\"properties\":{"
	    "\"name\":{\"type\":\"string\","
	    "\"description\":\"Skill name to read\"}"
	    "},\"required\":[\"name\"]}",
	    read_skill_handler);
