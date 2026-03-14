/*
 * Copyright (c) 2026 LingaoMeng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZephyrClaw - Memory Module Implementation
 *
 * Persistence uses Zephyr settings subsystem (subtree "zc"):
 *   "zc/summary"  : conversation summary (string)
 *
 * History strategy:
 *   g_node_slab   : k_mem_slab pool of MEMORY_HISTORY_POOL_SIZE nodes
 *   g_history_list: in-use nodes, oldest → newest
 *
 *   memory_add_turn():
 *     1. Try k_mem_slab_alloc from g_node_slab.
 *     2. If empty → try_compress(fn) — iterates up to MEMORY_COMPRESS_COUNT
 *        oldest history nodes in-place (no copy-out array), calls fn, then
 *        frees those nodes back to the slab.
 *     3. If compress fails → FIFO evict the oldest node (slab free + realloc).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/slist.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "memory.h"
#include "agent.h"

LOG_MODULE_REGISTER(zephyrclaw_memory, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

struct memory_node {
	sys_snode_t node;
	char role[MEMORY_ROLE_MAX_LEN];
	char content[MEMORY_MSG_MAX_LEN];
};

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

K_MEM_SLAB_DEFINE(g_node_slab, sizeof(struct memory_node), MEMORY_HISTORY_POOL_SIZE, 4);

static sys_slist_t g_history_list = SYS_SLIST_STATIC_INIT(&g_history_list);

/* Cached summary loaded from settings */
static char g_summary[MEMORY_SUMMARY_MAX_LEN];

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

static int zc_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	size_t read_len;
	int rc;

	if (strcmp(name, "summary") == 0) {
		read_len = MIN(len, sizeof(g_summary) - 1);
		rc = read_cb(cb_arg, g_summary, read_len);
		if (rc >= 0) {
			g_summary[read_len] = '\0';
		}

		return rc < 0 ? rc : 0;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(zc, "zc", NULL, zc_settings_set, NULL, NULL);

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

static int json_escape(const char *src, char *dst, size_t dst_len)
{
	size_t j = 0;

	for (size_t i = 0; src[i] != '\0' && j + 2 < dst_len; i++) {
		unsigned char c = (unsigned char)src[i];

		if (c == '"') {
			dst[j++] = '\\';
			dst[j++] = '"';
		} else if (c == '\\') {
			dst[j++] = '\\';
			dst[j++] = '\\';
		} else if (c == '\n') {
			dst[j++] = '\\';
			dst[j++] = 'n';
		} else if (c == '\r') {
			dst[j++] = '\\';
			dst[j++] = 'r';
		} else if (c < 0x20) {
			dst[j++] = ' ';
		} else {
			dst[j++] = c;
		}
	}
	dst[j] = '\0';
	return (int)j;
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

/*
 * history_format_fn - agent_format_fn_t callback defined in memory.c.
 *
 * Peeks up to MEMORY_COMPRESS_COUNT oldest nodes from g_history_list,
 * fills roles_out/contents_out with their string pointers, and returns
 * the actual count.  After agent_request_summary() succeeds, try_compress()
 * dequeues and frees exactly that many nodes from the slab.
 */
static int history_format_fn(const char **roles_out, const char **contents_out, int max_count)
{
	struct memory_node *cur;
	int count = 0;

	SYS_SLIST_FOR_EACH_CONTAINER(&g_history_list, cur, node) {
		roles_out[count] = cur->role;
		contents_out[count] = cur->content;
		if (++count >= max_count) {
			break;
		}
	}

	return count;
}

/*
 * try_compress - summarise oldest history nodes via agent_request_summary.
 *
 * Calls agent_request_summary(history_format_fn, ...) which invokes
 * history_format_fn to collect roles/contents in-place.  On success,
 * dequeues and frees those nodes from the slab.
 *
 * Returns 0 on success, -ENODATA if history is empty, or the error from
 * agent_request_summary on failure (nodes remain in history).
 */
static int try_compress(void)
{
	struct memory_node *node;
	sys_snode_t *n;
	int rc, i;

	rc = agent_request_summary(history_format_fn, g_summary, sizeof(g_summary));
	if (rc < 0) {
		LOG_WRN("Compress failed (%d)", rc);
		return rc;
	}

	settings_save_one("zc/summary", g_summary, strlen(g_summary) + 1);

	for (i = 0; i < MEMORY_COMPRESS_COUNT; i++) {
		n = sys_slist_get(&g_history_list);
		if (!n) {
			break;
		}

		node = CONTAINER_OF(n, struct memory_node, node);
		k_mem_slab_free(&g_node_slab, (void *)node);
	}

	LOG_INF("Compressed %d nodes → g_summary", i);

	return 0;
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

int memory_init(void)
{
	int rc;

	rc = settings_subsys_init();
	if (rc) {
		LOG_ERR("settings_subsys_init failed: %d", rc);
		return rc;
	}

	rc = settings_load_subtree("zc");
	if (rc) {
		LOG_WRN("settings_load_subtree failed: %d", rc);
	}

	if (g_summary[0] != '\0') {
		LOG_INF("Loaded summary from settings");
	} else {
		LOG_INF("No prior summary");
	}

	return 0;
}

int memory_add_turn(const char *role, const char *content)
{
	struct memory_node *node;
	sys_snode_t *n;
	int rc, i, j;

	if (!role || !content) {
		return -EINVAL;
	}

	if (k_mem_slab_alloc(&g_node_slab, (void **)&node, K_NO_WAIT) != 0) {
		rc = try_compress();
		if (rc < 0) {
			LOG_WRN("Compression failed (%d), FIFO evicting oldest", rc);
			n = sys_slist_get(&g_history_list);
			if (!n) {
				LOG_ERR("History list unexpectedly empty");
				return -ENOMEM;
			}

			node = CONTAINER_OF(n, struct memory_node, node);
			k_mem_slab_free(&g_node_slab, (void *)node);
		}

		if (k_mem_slab_alloc(&g_node_slab, (void **)&node, K_NO_WAIT) != 0) {
			LOG_ERR("No free node after compression (unexpected)");
			return -ENOMEM;
		}
	}

	strncpy(node->role, role, MEMORY_ROLE_MAX_LEN - 1);
	node->role[MEMORY_ROLE_MAX_LEN - 1] = '\0';
	for (i = 0, j = 0; i < MEMORY_MSG_MAX_LEN - 1; i++) {
		if (content[i] == '\"') {
			continue;
		}
		if (content[i] == '\0') {
			break;
		}
		node->content[j++] = content[i];
	}
	node->content[j] = '\0';

	sys_slist_append(&g_history_list, &node->node);
	return 0;
}

int memory_build_messages_json(char *buf, size_t buf_len)
{
	struct memory_node *cur;
	size_t pos;
	int n;

	if (!buf || buf_len < 8) {
		return -EINVAL;
	}

	pos = 0;

	n = snprintf(buf + pos, buf_len - pos, "[{\"role\":\"system\",\"content\":\"");
	if (n < 0 || (size_t)n >= buf_len - pos) {
		goto overflow;
	}

	pos += n;

	/* 1. System message — escape directly into buf */
	n = json_escape(agent_get_system_prompt(), buf + pos, buf_len - pos);
	if ((size_t)n >= buf_len - pos) {
		goto overflow;
	}

	pos += n;

	n = snprintf(buf + pos, buf_len - pos, "\"}");
	if (n < 0 || (size_t)n >= buf_len - pos) {
		goto overflow;
	}

	pos += n;

	/* 2. Prior context summary */
	if (g_summary[0] != '\0') {
		n = snprintf(buf + pos, buf_len - pos,
			     ",{\"role\":\"user\","
			     "\"content\":\"[Context from prior sessions] ");
		if (n < 0 || (size_t)n >= buf_len - pos) {
			goto overflow;
		}

		pos += n;

		n = json_escape(g_summary, buf + pos, buf_len - pos);
		if ((size_t)n >= buf_len - pos) {
			goto overflow;
		}

		pos += n;

		n = snprintf(buf + pos, buf_len - pos,
			     "\"},{ \"role\":\"assistant\","
			     "\"content\":\"Understood. I recall the prior context.\"}");
		if (n < 0 || (size_t)n >= buf_len - pos) {
			goto overflow;
		}

		pos += n;
	}

	/* 3. In-RAM history */
	SYS_SLIST_FOR_EACH_CONTAINER(&g_history_list, cur, node) {
		n = snprintf(buf + pos, buf_len - pos, ",{\"role\":\"%s\",\"content\":\"",
			     cur->role);
		if (n < 0 || (size_t)n >= buf_len - pos) {
			goto overflow;
		}

		pos += n;

		n = json_escape(cur->content, buf + pos, buf_len - pos);
		if ((size_t)n >= buf_len - pos) {
			goto overflow;
		}

		pos += n;

		n = snprintf(buf + pos, buf_len - pos, "\"}");
		if (n < 0 || (size_t)n >= buf_len - pos) {
			goto overflow;
		}

		pos += n;
	}

	n = snprintf(buf + pos, buf_len - pos, "]");
	if (n < 0 || (size_t)n >= buf_len - pos) {
		goto overflow;
	}

	pos += n;
	return (int)pos;

overflow:
	LOG_ERR("messages JSON buffer overflow");
	return -ENOMEM;
}

const char *memory_get_summary(void)
{
	return g_summary[0] != '\0' ? g_summary : NULL;
}

void memory_clear_history(void)
{
	struct memory_node *node;
	sys_snode_t *n;

	while ((n = sys_slist_get(&g_history_list)) != NULL) {
		node = CONTAINER_OF(n, struct memory_node, node);
		k_mem_slab_free(&g_node_slab, (void *)node);
	}

	LOG_INF("In-RAM history cleared");
}

int memory_wipe_all(void)
{
	memory_clear_history();
	memset(g_summary, 0, sizeof(g_summary));
	settings_delete("zc/summary");
	LOG_INF("Summary wiped");
	return 0;
}

void memory_dump(void)
{
	struct memory_node *node;
	int i = 0;

	printk("=== ZephyrClaw Memory Dump ===\n");
	printk("  Summary     : %s\n", g_summary[0] ? g_summary : "(none)");
	printk("  History:\n");

	SYS_SLIST_FOR_EACH_CONTAINER(&g_history_list, node, node) {
		printk("    [%d] %s: %.80s\n", i++, node->role, node->content);
	}
}
