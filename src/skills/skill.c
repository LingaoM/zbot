/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZBot - Skill Framework Implementation
 *
 * Skills self-register at boot via SYS_INIT (PRE_KERNEL_1) into a
 * singly-linked list. This file provides the runtime API for iterating
 * and executing skills.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "skill.h"

LOG_MODULE_REGISTER(zbot_skill, LOG_LEVEL_INF);

static struct skill_node *skill_list_head;

void skill_register(struct skill_node *node)
{
	node->next = skill_list_head;
	skill_list_head = node;
}

int skill_run(const char *name, const char *arg, char *result, size_t res_len)
{
	if (!name || !result) {
		return -EINVAL;
	}

	for (struct skill_node *n = skill_list_head; n; n = n->next) {
		if (strcmp(n->entry.name, name) == 0) {
			LOG_INF("Running skill: %s (arg=%s)", name, arg ? arg : "");
			return n->entry.handler(arg, result, res_len);
		}
	}

	snprintf(result, res_len, "Skill '%s' not found.", name);
	return -ENOENT;
}

void skill_list(void)
{
	int count = 0;

	for (struct skill_node *n = skill_list_head; n; n = n->next) {
		count++;
	}

	printk("=== zbot Skills (%d registered) ===\n", count);

	for (struct skill_node *n = skill_list_head; n; n = n->next) {
		printk("  %-20s %s\n", n->entry.name, n->entry.description);
	}
}

int skill_count(void)
{
	int count = 0;

	for (struct skill_node *n = skill_list_head; n; n = n->next) {
		count++;
	}
	return count;
}

const struct skill_entry *skill_get(int idx)
{
	int i = 0;

	for (struct skill_node *n = skill_list_head; n; n = n->next) {
		if (i == idx) {
			return &n->entry;
		}
		i++;
	}
	return NULL;
}

int skill_read_content(const char *name, char *result, size_t res_len)
{
	if (!name || !result) {
		return -EINVAL;
	}

	for (struct skill_node *n = skill_list_head; n; n = n->next) {
		if (strcmp(n->entry.name, name) == 0) {
			if (n->entry.content && n->entry.content_len > 0) {
				size_t copy_len = n->entry.content_len;

				if (copy_len >= res_len) {
					copy_len = res_len - 1;
				}
				memcpy(result, n->entry.content, copy_len);
				result[copy_len] = '\0';
			} else {
				snprintf(result, res_len, "# %s\n\n%s\n", n->entry.name,
					 n->entry.description);
			}
			return 0;
		}
	}

	snprintf(result, res_len, "{\"error\":\"skill '%s' not found\"}", name);
	return -ENOENT;
}

void skill_foreach(void (*cb)(const struct skill_entry *entry, void *ctx), void *ctx)
{
	for (struct skill_node *n = skill_list_head; n; n = n->next) {
		cb(&n->entry, ctx);
	}
}
