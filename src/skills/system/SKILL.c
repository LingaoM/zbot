/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Skill: system
 * Unified system information: board_info, uptime, heap_info, status.
 * arg: JSON string with "action" field.
 */

#include <zephyr/kernel.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "skill.h"
#include "json_util.h"

static int action_board_info(char *result, size_t res_len)
{
	snprintf(result, res_len,
		 "{\"board\":\"" CONFIG_BOARD "\","
		 "\"rtos\":\"Zephyr\","
		 "\"agent\":\"zbot\","
		 "\"version\":\"0.1.0\"}");
	return 0;
}

static int action_uptime(char *result, size_t res_len)
{
	int64_t ms = k_uptime_get();

	snprintf(result, res_len,
		 "{\"uptime_ms\":%lld,\"uptime_s\":%lld}", ms, ms / 1000);
	return 0;
}

static int action_heap_info(char *result, size_t res_len)
{
#if defined(CONFIG_SYS_HEAP_RUNTIME_STATS)
	struct sys_memory_stats stats;
	extern struct k_heap z_malloc_heap;

	sys_heap_runtime_stats_get(&z_malloc_heap.heap, &stats);
	snprintf(result, res_len,
		 "{\"free_bytes\":%zu,\"allocated_bytes\":%zu,"
		 "\"max_allocated_bytes\":%zu}",
		 stats.free_bytes, stats.allocated_bytes, stats.max_allocated_bytes);
#else
	snprintf(result, res_len,
		 "{\"note\":\"enable CONFIG_SYS_HEAP_RUNTIME_STATS\","
		 "\"heap_pool_size\":%d}",
		 CONFIG_HEAP_MEM_POOL_SIZE);
#endif
	return 0;
}

static int action_status(char *result, size_t res_len)
{
	char board[128], uptime[64], heap[128];

	action_board_info(board, sizeof(board));
	action_uptime(uptime, sizeof(uptime));
	action_heap_info(heap, sizeof(heap));

	snprintf(result, res_len,
		 "{\"board\":%s,\"uptime\":%s,\"heap\":%s}",
		 board, uptime, heap);
	return 0;
}

static const char system_md[] = {
#include "skills/system/SKILL.md.inc"
	0x00
};

static int system_handler(const char *arg, char *result, size_t res_len)
{
	char action[16] = {0};

	if (!arg || arg[0] == '\0') {
		snprintf(result, res_len,
			 "{\"error\":\"system: missing 'action' field\","
			 "\"actions\":[\"board_info\",\"uptime\","
			 "\"heap_info\",\"status\"]}");
		return -EINVAL;
	}

	json_get_str(arg, "action", action, sizeof(action));

	if (strcmp(action, "board_info") == 0) {
		return action_board_info(result, res_len);
	}
	if (strcmp(action, "uptime") == 0) {
		return action_uptime(result, res_len);
	}
	if (strcmp(action, "heap_info") == 0) {
		return action_heap_info(result, res_len);
	}
	if (strcmp(action, "status") == 0) {
		return action_status(result, res_len);
	}

	snprintf(result, res_len,
		 "{\"error\":\"unknown action '%s'\","
		 "\"actions\":[\"board_info\",\"uptime\","
		 "\"heap_info\",\"status\"]}",
		 action);
	return -ENOENT;
}

SKILL_DEFINE(system_skill, "system",
	     "System info and status: board_info, uptime, heap_info, status.",
	     system_md, sizeof(system_md) - 1, system_handler);
