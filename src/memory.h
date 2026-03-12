/*
 * Copyright (c) 2026 LingaoMeng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZephyrClaw - Memory Module
 *
 * Manages conversation history in RAM using two K_FIFOs over a static node
 * pool, and persists a rolling summary to NVS so context survives reboots.
 *
 * NVS layout:
 *   ID 1  : Session counter (uint32_t)
 *   ID 2  : Conversation summary (string, max MEMORY_SUMMARY_MAX_LEN)
 *   ID 3  : Turn count for current session (uint32_t)
 *
 * History strategy:
 *   - g_free_fifo    : all idle pool nodes (starts full, 10 nodes).
 *   - g_history_fifo : in-use nodes in FIFO order (oldest → newest).
 *   - On add: get from g_free_fifo; if empty → try_compress(); if that
 *     fails → FIFO evict the oldest history node back to free_fifo.
 *   - On boot, the NVS summary is injected as a prior-context message.
 */

#ifndef ZEPHYRCLAW_MEMORY_H
#define ZEPHYRCLAW_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pool / compression parameters */
#define MEMORY_HISTORY_POOL_SIZE  10   /* total static nodes available    */
#define MEMORY_COMPRESS_COUNT      7   /* oldest nodes to summarise       */

/* Per-message limits */
#define MEMORY_MSG_MAX_LEN        512
#define MEMORY_SUMMARY_MAX_LEN    768
#define MEMORY_ROLE_MAX_LEN        16

/**
 * @brief Summary callback type.
 *
 * Called by memory_add_turn() when the pool is full and compression is needed.
 *
 * @param context  Concatenated text of the nodes to be summarised.
 * @param out      Output buffer for the summary text.
 * @param len      Size of the output buffer.
 * @return 0 on success, negative on error.
 */
typedef int (*memory_summary_fn)(const char *context, char *out, size_t len);

/**
 * @brief Register the summary callback.
 *
 * Must be called from main() after both memory and agent are initialised.
 * Avoids a circular include dependency between memory.c and agent.h.
 *
 * @param fn  The callback function (e.g. agent_request_summary).
 */
void memory_set_summary_callback(memory_summary_fn fn);

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
 *   1. The system prompt from soul.
 *   2. A "user" context message containing the NVS summary (if any).
 *   3. All in-RAM turns (head → tail order).
 *
 * @param buf      Output buffer.
 * @param buf_len  Buffer size.
 * @return Number of bytes written (excluding NUL), or negative on error.
 */
int memory_build_messages_json(char *buf, size_t buf_len);

/**
 * @brief Update and persist the conversation summary to NVS.
 *
 * @param summary  New summary string produced by the LLM.
 * @return 0 on success, negative errno on error.
 */
int memory_save_summary(const char *summary);

/**
 * @brief Get the current persisted summary (from NVS).
 *
 * @return Pointer to the summary string (empty string if none).
 */
const char *memory_get_summary(void);

/**
 * @brief Get the number of turns currently in the linked-list history.
 */
int memory_get_turn_count(void);

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

#endif /* ZEPHYRCLAW_MEMORY_H */
