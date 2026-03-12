/*
 * Copyright (c) 2026 LingaoMeng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZephyrClaw - Memory Module Implementation
 *
 * NVS IDs:
 *   1 = session_counter  (uint32_t)
 *   2 = summary          (char[MEMORY_SUMMARY_MAX_LEN])
 *   3 = turn_count       (uint32_t)
 *
 * History strategy:
 *   g_free_fifo  : pool of available nodes (initially all 10)
 *   g_history_fifo: FIFO of in-use nodes, oldest → newest
 *
 *   memory_add_turn():
 *     1. Try to get a free node from g_free_fifo.
 *     2. If empty → try_compress() (dequeues oldest MEMORY_COMPRESS_COUNT
 *        nodes from history, summarises them, puts N-1 back to free, and
 *        re-queues one compressed node at the head of history, then retries).
 *     3. If compress fails → FIFO evict the oldest node (put it back to free).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/kvss/nvs.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

#include "memory.h"
#include "soul.h"

LOG_MODULE_REGISTER(zephyrclaw_memory, LOG_LEVEL_INF);

/* NVS partition */
#define NVS_PARTITION        storage_partition
#define NVS_PARTITION_DEVICE FIXED_PARTITION_DEVICE(NVS_PARTITION)
#define NVS_PARTITION_OFFSET FIXED_PARTITION_OFFSET(NVS_PARTITION)
#define NVS_PARTITION_SIZE   FIXED_PARTITION_SIZE(NVS_PARTITION)

/* NVS entry IDs */
#define NVS_ID_SESSION_COUNTER 1
#define NVS_ID_SUMMARY         2
#define NVS_ID_TURN_COUNT      3

/* Shared with config.c via extern */
struct nvs_fs g_nvs;
bool g_nvs_ready = false;

/* ------------------------------------------------------------------ */
/* Node definition                                                      */
/* ------------------------------------------------------------------ */

struct memory_node {
    void *fifo_reserved; /* K_FIFO requires first word for linkage */
    char  role[MEMORY_ROLE_MAX_LEN];
    char  content[MEMORY_MSG_MAX_LEN];
};

/* ------------------------------------------------------------------ */
/* Static pool + FIFOs                                                  */
/* ------------------------------------------------------------------ */

static struct memory_node g_node_pool[MEMORY_HISTORY_POOL_SIZE];

K_FIFO_DEFINE(g_free_fifo);    /* available nodes          */
K_FIFO_DEFINE(g_history_fifo); /* in-use nodes, FIFO order */

static int g_history_count; /* nodes currently in g_history_fifo */

/* Cached summary from NVS */
static char g_summary[MEMORY_SUMMARY_MAX_LEN];

/* Session counter */
static uint32_t g_session_counter;

/* Summary callback (set by main via memory_set_summary_callback) */
static memory_summary_fn g_summary_cb;

/* ------------------------------------------------------------------ */
/* NVS helpers                                                          */
/* ------------------------------------------------------------------ */

static int nvs_init_fs(void)
{
    struct flash_pages_info info;
    int rc;

    g_nvs.flash_device = NVS_PARTITION_DEVICE;
    if (!device_is_ready(g_nvs.flash_device)) {
        LOG_ERR("Flash device not ready");
        return -ENODEV;
    }

    g_nvs.offset = NVS_PARTITION_OFFSET;

    rc = flash_get_page_info_by_offs(g_nvs.flash_device, g_nvs.offset, &info);
    if (rc) {
        LOG_ERR("flash_get_page_info_by_offs failed: %d", rc);
        return rc;
    }

    g_nvs.sector_size  = info.size;
    g_nvs.sector_count = MIN(3u, (uint32_t)(NVS_PARTITION_SIZE / info.size));
    if (g_nvs.sector_count < 2) {
        g_nvs.sector_count = 2;
    }

    rc = nvs_mount(&g_nvs);
    if (rc) {
        LOG_ERR("nvs_mount failed: %d", rc);
        return rc;
    }

    LOG_INF("NVS mounted: sector_size=%u sector_count=%u",
            g_nvs.sector_size, g_nvs.sector_count);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Compression                                                          */
/* ------------------------------------------------------------------ */

/*
 * Dequeue the oldest MEMORY_COMPRESS_COUNT nodes from g_history_fifo,
 * summarise them, return N-1 to g_free_fifo, re-insert one compressed
 * node at the front of g_history_fifo (via a temporary FIFO splice).
 *
 * Returns 0 on success, negative on failure (nodes are restored).
 */
static int try_compress(void)
{
    if (!g_summary_cb) {
        LOG_WRN("No summary callback, cannot compress");
        return -ENOTSUP;
    }

    if (g_history_count < MEMORY_COMPRESS_COUNT) {
        return -EINVAL;
    }

    /* Dequeue the oldest MEMORY_COMPRESS_COUNT nodes */
    struct memory_node *old[MEMORY_COMPRESS_COUNT];

    for (int i = 0; i < MEMORY_COMPRESS_COUNT; i++) {
        old[i] = k_fifo_get(&g_history_fifo, K_NO_WAIT);
        if (!old[i]) {
            /* Shouldn't happen, but restore what we took */
            for (int j = 0; j < i; j++) {
                k_fifo_put(&g_free_fifo, old[j]);
            }
            return -ENODATA;
        }
        g_history_count--;
    }

    /* Build context string */
    static char ctx_buf[MEMORY_COMPRESS_COUNT *
                        (MEMORY_MSG_MAX_LEN + MEMORY_ROLE_MAX_LEN + 8)];
    size_t pos = 0;

    for (int i = 0; i < MEMORY_COMPRESS_COUNT; i++) {
        int n = snprintf(ctx_buf + pos, sizeof(ctx_buf) - pos,
                         "%s: %s\n", old[i]->role, old[i]->content);
        if (n > 0) {
            pos += (size_t)n;
        }
    }
    ctx_buf[pos] = '\0';

    /* Call summary callback */
    static char summary_out[MEMORY_SUMMARY_MAX_LEN];
    int rc = g_summary_cb(ctx_buf, summary_out, sizeof(summary_out));
    if (rc < 0) {
        LOG_WRN("Summary callback failed (%d), restoring nodes", rc);
        /* Put them back to free — we will FIFO evict instead */
        for (int i = 0; i < MEMORY_COMPRESS_COUNT; i++) {
            k_fifo_put(&g_free_fifo, old[i]);
        }
        return rc;
    }

    /* Reuse old[0] as the compressed node, free the rest */
    strncpy(old[0]->role, "user", MEMORY_ROLE_MAX_LEN - 1);
    old[0]->role[MEMORY_ROLE_MAX_LEN - 1] = '\0';
    snprintf(old[0]->content, MEMORY_MSG_MAX_LEN,
             "[Compressed: %s]", summary_out);
    old[0]->content[MEMORY_MSG_MAX_LEN - 1] = '\0';

    for (int i = 1; i < MEMORY_COMPRESS_COUNT; i++) {
        k_fifo_put(&g_free_fifo, old[i]);
    }

    /*
     * Re-insert the compressed node at the FRONT of g_history_fifo.
     * K_FIFO is FIFO-only, so drain the remainder into a temp FIFO,
     * put the compressed node first, then re-queue the remainder.
     */
    struct k_fifo tmp;
    k_fifo_init(&tmp);

    /* Drain the rest of history into tmp */
    struct memory_node *n;

    while ((n = k_fifo_get(&g_history_fifo, K_NO_WAIT)) != NULL) {
        k_fifo_put(&tmp, n);
    }

    /* The compressed node goes first */
    k_fifo_put(&g_history_fifo, old[0]);
    g_history_count++;

    /* Re-queue the rest */
    while ((n = k_fifo_get(&tmp, K_NO_WAIT)) != NULL) {
        k_fifo_put(&g_history_fifo, n);
        g_history_count++;
    }

    LOG_INF("Compressed %d nodes → 1 (pool count: %d/%d)",
            MEMORY_COMPRESS_COUNT, g_history_count, MEMORY_HISTORY_POOL_SIZE);
    return 0;
}

/* ------------------------------------------------------------------ */
/* JSON escape helper                                                   */
/* ------------------------------------------------------------------ */

static int json_escape(const char *src, char *dst, size_t dst_len)
{
    size_t j = 0;

    for (size_t i = 0; src[i] != '\0' && j + 2 < dst_len; i++) {
        unsigned char c = (unsigned char)src[i];

        if (c == '"') {
            dst[j++] = '\\'; dst[j++] = '"';
        } else if (c == '\\') {
            dst[j++] = '\\'; dst[j++] = '\\';
        } else if (c == '\n') {
            dst[j++] = '\\'; dst[j++] = 'n';
        } else if (c == '\r') {
            dst[j++] = '\\'; dst[j++] = 'r';
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
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void memory_set_summary_callback(memory_summary_fn fn)
{
    g_summary_cb = fn;
}

int memory_init(void)
{
    int rc;

    /* Populate g_free_fifo with all pool nodes */
    for (int i = 0; i < MEMORY_HISTORY_POOL_SIZE; i++) {
        memset(&g_node_pool[i], 0, sizeof(g_node_pool[i]));
        k_fifo_put(&g_free_fifo, &g_node_pool[i]);
    }

    g_history_count   = 0;
    g_session_counter = 0;
    g_summary_cb      = NULL;
    memset(g_summary, 0, sizeof(g_summary));

    rc = nvs_init_fs();
    if (rc) {
        LOG_WRN("NVS init failed (%d), operating without persistence", rc);
        return rc;
    }
    g_nvs_ready = true;

    /* Load session counter */
    rc = nvs_read(&g_nvs, NVS_ID_SESSION_COUNTER,
                  &g_session_counter, sizeof(g_session_counter));
    if (rc < 0) {
        g_session_counter = 0;
    }
    g_session_counter++;
    nvs_write(&g_nvs, NVS_ID_SESSION_COUNTER,
              &g_session_counter, sizeof(g_session_counter));

    /* Load persisted summary */
    rc = nvs_read(&g_nvs, NVS_ID_SUMMARY, g_summary, sizeof(g_summary));
    if (rc > 0) {
        g_summary[MEMORY_SUMMARY_MAX_LEN - 1] = '\0';
        LOG_INF("Loaded summary from NVS (%d bytes)", rc);
    } else {
        g_summary[0] = '\0';
        LOG_INF("No prior summary in NVS (session #%u)", g_session_counter);
    }

    return 0;
}

int memory_add_turn(const char *role, const char *content)
{
    int i, j;

    if (!role || !content) {
        return -EINVAL;
    }

    /* Try to get a free node */
    struct memory_node *node = k_fifo_get(&g_free_fifo, K_NO_WAIT);

    if (!node) {
        /* Pool empty: try to compress */
        int rc = try_compress();
        if (rc < 0) {
            /* Compress failed: FIFO evict the oldest node */
            LOG_WRN("Compression failed (%d), FIFO evicting oldest", rc);
            node = k_fifo_get(&g_history_fifo, K_NO_WAIT);
            if (node) {
                g_history_count--;
                /* node is now recycled below */
            } else {
                LOG_ERR("History FIFO unexpectedly empty");
                return -ENOMEM;
            }
        } else {
            /* Compression freed nodes; grab one */
            node = k_fifo_get(&g_free_fifo, K_NO_WAIT);
            if (!node) {
                LOG_ERR("No free node after compression (unexpected)");
                return -ENOMEM;
            }
        }
    }

    strncpy(node->role, role, MEMORY_ROLE_MAX_LEN - 1);
    node->role[MEMORY_ROLE_MAX_LEN - 1] = '\0';
    for (i = 0, j = 0; i < MEMORY_MSG_MAX_LEN - 1; i++) {
        if (content[i] == '\"') continue;
        if (content[i] == '\0') break;

        node->content[j++] = content[i];
    }
    node->content[j] = '\0';

    k_fifo_put(&g_history_fifo, node);
    g_history_count++;

    /* Persist turn count */
    if (g_nvs_ready) {
        uint32_t tc = (uint32_t)g_history_count;
        nvs_write(&g_nvs, NVS_ID_TURN_COUNT, &tc, sizeof(tc));
    }

    return 0;
}

int memory_build_messages_json(char *buf, size_t buf_len)
{
    if (!buf || buf_len < 8) {
        return -EINVAL;
    }

    const struct soul_config *soul = soul_get();
    size_t pos = 0;
    int n;

    n = snprintf(buf + pos, buf_len - pos, "[");
    if (n < 0 || (size_t)n >= buf_len - pos) goto overflow;
    pos += n;

    /* 1. System message */
    char escaped[SOUL_SYSTEM_PROMPT_MAX * 2];
    json_escape(soul->system_prompt, escaped, sizeof(escaped));

    n = snprintf(buf + pos, buf_len - pos,
                 "{\"role\":\"system\",\"content\":\"%s\"}", escaped);
    if (n < 0 || (size_t)n >= buf_len - pos) goto overflow;
    pos += n;

    /* 2. Prior context summary */
    if (g_summary[0] != '\0') {
        char esc_summary[MEMORY_SUMMARY_MAX_LEN * 2];
        json_escape(g_summary, esc_summary, sizeof(esc_summary));

        n = snprintf(buf + pos, buf_len - pos,
                     ",{\"role\":\"user\","
                     "\"content\":\"[Context from prior sessions] %s\"}",
                     esc_summary);
        if (n < 0 || (size_t)n >= buf_len - pos) goto overflow;
        pos += n;

        n = snprintf(buf + pos, buf_len - pos,
                     ",{\"role\":\"assistant\","
                     "\"content\":\"Understood. I recall the prior context.\"}");
        if (n < 0 || (size_t)n >= buf_len - pos) goto overflow;
        pos += n;
    }

    /*
     * 3. In-RAM history.
     *
     * K_FIFO is destructive — we must drain, snapshot, and re-queue.
     * Use a temporary array (stack) since MEMORY_HISTORY_POOL_SIZE is small.
     */
    struct memory_node *snap[MEMORY_HISTORY_POOL_SIZE];
    int snap_count = 0;

    struct memory_node *cur;

    while ((cur = k_fifo_get(&g_history_fifo, K_NO_WAIT)) != NULL) {
        snap[snap_count++] = cur;
    }

    for (int i = 0; i < snap_count; i++) {
        char esc_content[MEMORY_MSG_MAX_LEN * 2];
        json_escape(snap[i]->content, esc_content, sizeof(esc_content));

        n = snprintf(buf + pos, buf_len - pos,
                     ",{\"role\":\"%s\",\"content\":\"%s\"}",
                     snap[i]->role, esc_content);

        /* Re-queue regardless of overflow */
        k_fifo_put(&g_history_fifo, snap[i]);

        if (n < 0 || (size_t)n >= buf_len - pos) goto overflow;
        pos += n;
    }

    n = snprintf(buf + pos, buf_len - pos, "]");
    if (n < 0 || (size_t)n >= buf_len - pos) goto overflow;
    pos += n;

    return (int)pos;

overflow:
    LOG_ERR("messages JSON buffer overflow");
    return -ENOMEM;
}

int memory_save_summary(const char *summary)
{
    if (!summary) {
        return -EINVAL;
    }

    strncpy(g_summary, summary, MEMORY_SUMMARY_MAX_LEN - 1);
    g_summary[MEMORY_SUMMARY_MAX_LEN - 1] = '\0';

    if (!g_nvs_ready) {
        LOG_WRN("NVS not ready, summary saved to RAM only");
        return 0;
    }

    ssize_t rc = nvs_write(&g_nvs, NVS_ID_SUMMARY,
                           g_summary, strlen(g_summary) + 1);
    if (rc < 0) {
        LOG_ERR("nvs_write summary failed: %zd", rc);
        return (int)rc;
    }

    LOG_INF("Summary saved to NVS (%zu bytes)", strlen(g_summary));
    return 0;
}

const char *memory_get_summary(void)
{
    return g_summary;
}

int memory_get_turn_count(void)
{
    return g_history_count;
}

void memory_clear_history(void)
{
    struct memory_node *node;

    while ((node = k_fifo_get(&g_history_fifo, K_NO_WAIT)) != NULL) {
        k_fifo_put(&g_free_fifo, node);
    }
    g_history_count = 0;
    LOG_INF("In-RAM history cleared");
}

int memory_wipe_all(void)
{
    memory_clear_history();
    memset(g_summary, 0, sizeof(g_summary));

    if (g_nvs_ready) {
        nvs_delete(&g_nvs, NVS_ID_SUMMARY);
        nvs_delete(&g_nvs, NVS_ID_TURN_COUNT);
        LOG_INF("NVS summary wiped");
    }
    return 0;
}

void memory_dump(void)
{
    printk("=== ZephyrClaw Memory Dump ===\n");
    printk("  Session     : %u\n", g_session_counter);
    printk("  Turns (RAM) : %d / %d\n", g_history_count, MEMORY_HISTORY_POOL_SIZE);
    printk("  Summary     : %s\n", g_summary[0] ? g_summary : "(none)");
    printk("  History:\n");

    struct memory_node *snap[MEMORY_HISTORY_POOL_SIZE];
    int snap_count = 0;
    struct memory_node *node;

    while ((node = k_fifo_get(&g_history_fifo, K_NO_WAIT)) != NULL) {
        snap[snap_count++] = node;
    }
    for (int i = 0; i < snap_count; i++) {
        printk("    [%d] %s: %.80s\n", i, snap[i]->role, snap[i]->content);
        k_fifo_put(&g_history_fifo, snap[i]);
    }
}
