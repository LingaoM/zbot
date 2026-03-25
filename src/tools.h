/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * zbot - Tools Framework
 *
 * The LLM-facing tools are defined in src/tools/<name>/TOOL.c and
 * self-register at boot via SYS_INIT (PRE_KERNEL_1) into a linked list.
 *
 * tools_build_json() serialises every registered tool into the OpenAI
 * function-calling schema sent with each LLM request.
 *
 * tools_execute() walks the list and dispatches by name; it is called
 * from agent.c for every tool_call returned by the LLM.
 *
 * Hardware primitives (GPIO, uptime, heap, …) are NOT registered as
 * LLM tools directly. They are called internally by tool_exec/TOOL.c.
 */

#ifndef ZBOT_TOOLS_H
#define ZBOT_TOOLS_H

#include <stddef.h>
#include <zephyr/init.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOOLS_NAME_MAX_LEN   32
#define TOOLS_RESULT_MAX_LEN 1024

/**
 * @brief Tool handler function signature.
 */
typedef int (*tool_handler_fn)(const char *args_json, char *result, size_t result_len);

/** Tool entry descriptor */
struct tool_entry {
	const char *name;
	const char *description;
	const char *parameters_json_schema;
	tool_handler_fn handler;
};

/** Linked-list node wrapping a tool_entry */
struct tool_node {
	struct tool_entry entry;
	struct tool_node *next;
};

/**
 * @brief Register a tool node (called automatically by TOOL_DEFINE).
 */
void tool_register(struct tool_node *node);

/**
 * @brief Define an LLM-visible tool and register it at boot (PRE_KERNEL_1).
 */
#define TOOL_DEFINE(_var, _name, _desc, _schema, _handler)                    \
	static struct tool_node _var##_node = {                                \
		.entry = {                                                     \
			.name = (_name),                                       \
			.description = (_desc),                                \
			.parameters_json_schema = (_schema),                   \
			.handler = (_handler),                                 \
		},                                                             \
		.next = NULL,                                                  \
	};                                                                     \
	static int _var##_init(void)                                           \
	{                                                                      \
		tool_register(&_var##_node);                                   \
		return 0;                                                      \
	}                                                                      \
	SYS_INIT(_var##_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT)

/**
 * @brief Find a registered tool by name and execute it.
 */
int tools_execute(const char *name, const char *args_json, char *result, size_t res_len);

/**
 * @brief Build the OpenAI-compatible "tools" JSON array for the LLM request.
 */
int tools_build_json(char *buf, size_t buf_len);

/**
 * @brief Print all registered tools to serial console.
 */
void tools_list(void);

#ifdef __cplusplus
}
#endif

#endif /* ZBOT_TOOLS_H */
