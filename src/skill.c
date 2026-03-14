/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZBot - Skill Framework Implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "skill.h"
#include "tools.h"
#include "memory.h"

LOG_MODULE_REGISTER(zbot_skill, LOG_LEVEL_INF);

static struct skill_descriptor g_skills[SKILL_MAX_COUNT];
static int g_skill_count = 0;

/* Skill: blink_led — blink LED0 N times */
static int skill_blink_led(const char *arg, char *result, size_t res_len)
{
	int times = 3;
	if (arg && arg[0] != '\0') {
		times = (int)strtol(arg, NULL, 10);
		if (times <= 0 || times > 20) {
			times = 3;
		}
	}

	char tool_result[TOOLS_RESULT_MAX_LEN];

	for (int i = 0; i < times; i++) {
		tools_execute("gpio_write", "{\"pin\":\"led0\",\"value\":1}", tool_result,
			      sizeof(tool_result));
		k_msleep(300);
		tools_execute("gpio_write", "{\"pin\":\"led0\",\"value\":0}", tool_result,
			      sizeof(tool_result));
		k_msleep(300);
	}

	snprintf(result, res_len, "Blinked led0 %d times.", times);
	return 0;
}

/* Skill: sos — blink SOS pattern on LED0 */
static int skill_sos(const char *arg, char *result, size_t res_len)
{
	ARG_UNUSED(arg);

	char tr[TOOLS_RESULT_MAX_LEN];

	/* S = ... O = --- S = ... */
	int pattern[] = {200, 200, 200, 600, 600, 600, 200, 200, 200};
	int count = ARRAY_SIZE(pattern);

	for (int i = 0; i < count; i++) {
		tools_execute("gpio_write", "{\"pin\":\"led0\",\"value\":1}", tr, sizeof(tr));
		k_msleep(pattern[i]);
		tools_execute("gpio_write", "{\"pin\":\"led0\",\"value\":0}", tr, sizeof(tr));
		k_msleep(200);
	}

	snprintf(result, res_len, "SOS pattern transmitted on led0.");
	return 0;
}

/* Skill: system_status — collect board status summary */
static int skill_system_status(const char *arg, char *result, size_t res_len)
{
	ARG_UNUSED(arg);

	char uptime[TOOLS_RESULT_MAX_LEN] = {0};
	char heap[TOOLS_RESULT_MAX_LEN] = {0};
	char board[TOOLS_RESULT_MAX_LEN] = {0};

	tools_execute("get_uptime", "{}", uptime, sizeof(uptime));
	tools_execute("get_heap_info", "{}", heap, sizeof(heap));
	tools_execute("get_board_info", "{}", board, sizeof(board));

	snprintf(result, res_len, "Board: %s | Uptime: %s | Heap: %s", board, uptime, heap);
	return 0;
}

/* Skill: clear_memory — wipe conversation history */
static int skill_clear_memory(const char *arg, char *result, size_t res_len)
{
	ARG_UNUSED(arg);
	memory_wipe_all();
	snprintf(result, res_len, "Conversation history and NVS summary wiped.");
	return 0;
}

int skill_register(const char *name, const char *description, skill_fn fn)
{
	if (!name || !description || !fn) {
		return -EINVAL;
	}
	if (g_skill_count >= SKILL_MAX_COUNT) {
		return -ENOMEM;
	}

	struct skill_descriptor *s = &g_skills[g_skill_count];
	strncpy(s->name, name, SKILL_NAME_MAX_LEN - 1);
	strncpy(s->description, description, SKILL_DESC_MAX_LEN - 1);
	s->handler = fn;
	g_skill_count++;

	LOG_DBG("Registered skill: %s", name);
	return 0;
}

int skill_run(const char *name, const char *arg, char *result, size_t res_len)
{
	if (!name || !result) {
		return -EINVAL;
	}

	for (int i = 0; i < g_skill_count; i++) {
		if (strcmp(g_skills[i].name, name) == 0) {
			LOG_INF("Running skill: %s (arg=%s)", name, arg ? arg : "");
			return g_skills[i].handler(arg, result, res_len);
		}
	}

	snprintf(result, res_len, "Skill '%s' not found.", name);
	return -ENOENT;
}

void skill_list(void)
{
	printk("=== zbot Skills (%d registered) ===\n", g_skill_count);
	for (int i = 0; i < g_skill_count; i++) {
		printk("  %-20s %s\n", g_skills[i].name, g_skills[i].description);
	}
}

int skill_count(void)
{
	return g_skill_count;
}

const struct skill_descriptor *skill_get(int idx)
{
	if (idx < 0 || idx >= g_skill_count) {
		return NULL;
	}
	return &g_skills[idx];
}

void skills_register_builtins(void)
{
	skill_register("blink_led", "Blink LED0 N times (arg: number of blinks)", skill_blink_led);

	skill_register("sos", "Blink SOS morse code pattern on LED0", skill_sos);

	skill_register("system_status", "Report board uptime, heap usage, and hardware info",
		       skill_system_status);

	skill_register("clear_memory", "Wipe conversation history and NVS summary",
		       skill_clear_memory);

	LOG_INF("Built-in skills registered (%d total)", g_skill_count);
}
