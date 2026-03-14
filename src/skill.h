/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZBot - Skill Framework
 *
 * Skills are higher-level, reusable capabilities built on top of Tools.
 * While Tools are atomic hardware/OS operations, Skills are multi-step
 * workflows that can be triggered by the agent or directly from the shell.
 *
 * Each skill registers itself at startup via skill_register().
 * The agent can list and invoke skills by name.
 */

#ifndef ZBOT_SKILL_H
#define ZBOT_SKILL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SKILL_NAME_MAX_LEN   32
#define SKILL_DESC_MAX_LEN   128
#define SKILL_RESULT_MAX_LEN 512
#define SKILL_MAX_COUNT      16

/**
 * @brief Skill handler function.
 *
 * @param arg     Optional string argument from caller.
 * @param result  Output buffer for skill result text.
 * @param res_len Size of result buffer.
 * @return 0 on success, negative errno on failure.
 */
typedef int (*skill_fn)(const char *arg, char *result, size_t res_len);

/** Skill descriptor */
struct skill_descriptor {
	char name[SKILL_NAME_MAX_LEN];
	char description[SKILL_DESC_MAX_LEN];
	skill_fn handler;
};

/**
 * @brief Register a skill. Must be called before skill_run().
 * @return 0 on success, -ENOMEM if registry is full.
 */
int skill_register(const char *name, const char *description, skill_fn fn);

/**
 * @brief Run a skill by name.
 * @param name    Skill name.
 * @param arg     Argument string (may be NULL).
 * @param result  Output buffer.
 * @param res_len Buffer size.
 * @return 0 on success, -ENOENT if not found.
 */
int skill_run(const char *name, const char *arg, char *result, size_t res_len);

/**
 * @brief List all registered skills (prints to serial).
 */
void skill_list(void);

/**
 * @brief Get skill count.
 */
int skill_count(void);

/**
 * @brief Get skill by index (for JSON schema generation).
 */
const struct skill_descriptor *skill_get(int idx);

/**
 * @brief Register all built-in skills. Call once at init.
 */
void skills_register_builtins(void);

#ifdef __cplusplus
}
#endif

#endif /* ZBOT_SKILL_H */
