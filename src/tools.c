/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * zbot - Tools Framework Implementation
 *
 * LLM-visible tools self-register via TOOL_DEFINE / SYS_INIT (PRE_KERNEL_1)
 * into a singly-linked list. This file only provides the list head and the
 * three framework functions: tool_register, tools_execute, tools_build_json.
 *
 * Hardware primitives live inside the individual TOOL.c files and are
 * not exposed to this layer.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "tools.h"

LOG_MODULE_REGISTER(zbot_tools, LOG_LEVEL_INF);

static struct tool_node *tool_list_head;

void tool_register(struct tool_node *node)
{
	node->next = tool_list_head;
	tool_list_head = node;
}

int tools_execute(const char *name, const char *args_json, char *result, size_t res_len)
{
	if (!name || !result) {
		return -EINVAL;
	}

	printk("%s %s\n", name, args_json);

	for (struct tool_node *n = tool_list_head; n; n = n->next) {
		if (strcmp(n->entry.name, name) == 0) {
			return n->entry.handler(args_json ? args_json : "{}", result, res_len);
		}
	}

	snprintf(result, res_len, "{\"error\":\"unknown tool: %s\"}", name);
	return -ENOENT;
}

int tools_build_json(char *buf, size_t buf_len)
{
	size_t pos = 0;
	int n;
	bool first = true;

	n = snprintf(buf + pos, buf_len - pos, "[");
	if (n < 0 || (size_t)n >= buf_len - pos) {
		return -ENOMEM;
	}
	pos += n;

	for (struct tool_node *node = tool_list_head; node; node = node->next) {
		const struct tool_entry *e = &node->entry;

		n = snprintf(buf + pos, buf_len - pos,
			     "%s{"
			     "\"type\":\"function\","
			     "\"function\":{"
			     "\"name\":\"%s\","
			     "\"description\":\"%s\","
			     "\"parameters\":%s"
			     "}"
			     "}",
			     first ? "" : ",",
			     e->name, e->description, e->parameters_json_schema);
		if (n < 0 || (size_t)n >= buf_len - pos) {
			return -ENOMEM;
		}
		pos += n;
		first = false;
	}

	n = snprintf(buf + pos, buf_len - pos, "]");
	if (n < 0 || (size_t)n >= buf_len - pos) {
		return -ENOMEM;
	}
	pos += n;

	return (int)pos;
}

void tools_list(void)
{
	int count = 0;

	for (struct tool_node *n = tool_list_head; n; n = n->next) {
		count++;
	}

	printk("=== zbot Tools (%d registered) ===\n", count);

	for (struct tool_node *n = tool_list_head; n; n = n->next) {
		printk("  %-20s %s\n", n->entry.name, n->entry.description);
	}
}
