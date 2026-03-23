/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZBot - Skill Framework
 *
 * Skills are higher-level, reusable capabilities built on top of Tools.
 * Each skill lives in src/skills/<name>/ and self-registers at boot via
 * SYS_INIT (PRE_KERNEL_1) into a singly-linked list — no linker script
 * modifications required.
 *
 * Each skill has an associated Markdown documentation file (SKILL.md)
 * embedded at build time. The LLM receives only name+description in every
 * request and calls read_skill to fetch the full documentation on demand.
 */

#ifndef ZBOT_SKILL_H
#define ZBOT_SKILL_H

#include <stddef.h>
#include <zephyr/init.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SKILL_NAME_MAX_LEN   32
#define SKILL_RESULT_MAX_LEN 512

/**
 * @brief Skill handler function.
 *
 * @param arg     Optional string argument from caller.
 * @param result  Output buffer for skill result text.
 * @param res_len Size of result buffer.
 * @return 0 on success, negative errno on failure.
 */
typedef int (*skill_fn)(const char *arg, char *result, size_t res_len);

/**
 * @brief Skill entry descriptor.
 */
struct skill_entry {
	const char *name;        /**< Skill name (snake_case) */
	const char *description; /**< Short one-line description for LLM */
	const char *content;     /**< Full Markdown documentation */
	size_t content_len;      /**< Length of content (excluding null terminator) */
	skill_fn handler;        /**< Execution handler */
};

/**
 * @brief Linked-list node wrapping a skill_entry.
 * Each skill statically allocates one of these.
 */
struct skill_node {
	struct skill_entry entry;
	struct skill_node *next;
};

/**
 * @brief Register a skill node into the global list.
 * Called automatically by SKILL_DEFINE via SYS_INIT.
 */
void skill_register(struct skill_node *node);

/**
 * @brief Statically define a skill and register it at boot (PRE_KERNEL_1).
 *
 * Usage in src/skills/<name>/SKILL.c:
 *   SKILL_DEFINE(my_skill, "my_skill", "Short description",
 *                my_skill_md, sizeof(my_skill_md) - 1, my_skill_handler);
 */
#define SKILL_DEFINE(_var, _name, _desc, _content, _content_len, _handler)                        \
	static struct skill_node _var##_node = {                                                   \
		.entry = {                                                                         \
			.name = (_name),                                                           \
			.description = (_desc),                                                    \
			.content = (_content),                                                     \
			.content_len = (_content_len),                                             \
			.handler = (_handler),                                                     \
		},                                                                                 \
		.next = NULL,                                                                      \
	};                                                                                         \
	static int _var##_init(void)                                                               \
	{                                                                                          \
		skill_register(&_var##_node);                                                      \
		return 0;                                                                          \
	}                                                                                          \
	SYS_INIT(_var##_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT)

/**
 * @brief Run a skill by name.
 */
int skill_run(const char *name, const char *arg, char *result, size_t res_len);

/**
 * @brief Print all registered skills to serial console.
 */
void skill_list(void);

/**
 * @brief Get number of registered skills.
 */
int skill_count(void);

/**
 * @brief Get skill by index (for enumeration).
 */
const struct skill_entry *skill_get(int idx);

/**
 * @brief Read skill documentation content by name.
 */
int skill_read_content(const char *name, char *result, size_t res_len);

/**
 * @brief Iterate registered skills, invoking a callback for each entry.
 *
 * The callback receives the skill entry and a user-supplied context pointer.
 * It should write directly into the destination buffer and return the number
 * of bytes written, or a negative value to stop iteration early.
 *
 * @param cb      Callback invoked for each skill.
 * @param ctx     Caller context passed through to the callback.
 */
void skill_foreach(void (*cb)(const struct skill_entry *entry, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* ZBOT_SKILL_H */
