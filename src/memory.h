/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZBot - Memory Module
 *
 * Manages conversation history in RAM using a k_mem_slab pool and a
 * sys_slist_t, and persists a rolling summary via Zephyr settings subsystem.
 *
 * Settings keys:
 *   "zbot/summary" : conversation summary (string)
 *
 * History strategy:
 *   - g_node_slab    : k_mem_slab of MEMORY_HISTORY_POOL_SIZE nodes.
 *   - g_history_list : in-use nodes in FIFO order (oldest → newest).
 *   - On add: alloc from slab; if full → try_compress(fn) with up to
 *     MEMORY_COMPRESS_COUNT oldest nodes (fewer is ok, zero returns -ENODATA);
 *     if compress fails → FIFO evict oldest node back to slab.
 *   - On boot, the persisted summary is injected as a prior-context message.
 */

#ifndef ZBOT_MEMORY_H
#define ZBOT_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pool / compression parameters */
#define MEMORY_HISTORY_POOL_SIZE 10 /* total static nodes available    */
#define MEMORY_COMPRESS_COUNT    6  /* oldest nodes to summarise       */

/* Per-message limits */
#define MEMORY_MSG_MAX_LEN     512
#define MEMORY_SUMMARY_MAX_LEN 768
#define MEMORY_ROLE_MAX_LEN    16

/**
 * @brief Initialise memory module and mount NVS.
 *
 * Loads the persisted summary from NVS into RAM. Must be called once at boot
 * after the flash subsystem is ready.
 *
 * @return 0 on success, negative errno on error.
 */
int memory_init(void);

/**
 * @brief Append a new turn to the in-RAM history.
 *
 * If the pool is full, triggers compression (or FIFO eviction on failure).
 *
 * @param role     "user" or "assistant".
 * @param content  Message text.
 * @return 0 on success, -ENOMEM if content is too long.
 */
int memory_add_turn(const char *role, const char *content);

/**
 * @brief Build a JSON messages array for the LLM request.
 *
 * The array begins with:
 *   1. The system prompt from agent.
 *   2. A "user" context message containing the NVS summary (if any).
 *   3. All in-RAM turns (head → tail order).
 *
 * @param buf      Output buffer.
 * @param buf_len  Buffer size.
 * @return Number of bytes written (excluding NUL), or negative on error.
 */
int memory_build_messages_json(char *buf, size_t buf_len);

/**
 * @brief Get the current persisted summary (from NVS).
 *
 * @return Pointer to the summary string, or NULL if none.
 */
const char *memory_get_summary(void);

/**
 * @brief Clear in-RAM history (does not wipe NVS summary).
 */
void memory_clear_history(void);

/**
 * @brief Wipe both RAM history and NVS summary.
 * @return 0 on success.
 */
int memory_wipe_all(void);

/**
 * @brief Print history to serial for debugging.
 */
void memory_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* ZBOT_MEMORY_H */
