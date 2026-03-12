/*
 * Copyright (c) 2026 LingaoMeng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZephyrClaw - Persistent Config Module
 *
 * Only manages the LLM API key in NVS (ID 12).
 * WiFi is handled by Zephyr's wifi_credentials subsystem.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/kvss/nvs.h>
#include <string.h>
#include <errno.h>

#include "config.h"
#include "soul.h"

LOG_MODULE_REGISTER(zephyrclaw_config, LOG_LEVEL_INF);

#define NVS_ID_API_KEY 12

/* Shared NVS filesystem from memory.c */
extern struct nvs_fs g_nvs;
extern bool         g_nvs_ready;

void config_init(void)
{
    if (!g_nvs_ready) {
        printk("[config] NVS not ready.\n");
        return;
    }

    char api_key[CONFIG_API_KEY_MAX_LEN] = {0};
    ssize_t rc = nvs_read(&g_nvs, NVS_ID_API_KEY,
                          api_key, sizeof(api_key));
    if (rc > 0) {
        api_key[sizeof(api_key) - 1] = '\0';
        soul_set_api_key(api_key);
        memset(api_key, 0, sizeof(api_key));
        printk("[config] API key loaded from flash.\n");
    } else {
        printk("[config] No API key stored. Use: claw key <key>\n");
        printk("[config] To persist:             claw key-save\n");
    }
}

int config_save_api_key(const char *key)
{
    if (!g_nvs_ready) return -ENODEV;
    if (!key) return -EINVAL;

    ssize_t rc = nvs_write(&g_nvs, NVS_ID_API_KEY,
                           key, strlen(key) + 1);
    if (rc < 0) {
        LOG_ERR("Failed to save API key: %zd", rc);
        return (int)rc;
    }
    LOG_INF("API key saved to NVS");
    return 0;
}

int config_delete_api_key(void)
{
    if (!g_nvs_ready) return -ENODEV;
    return nvs_delete(&g_nvs, NVS_ID_API_KEY);
}

bool config_has_api_key(void)
{
    if (!g_nvs_ready) return false;
    char buf[4];
    return nvs_read(&g_nvs, NVS_ID_API_KEY, buf, sizeof(buf)) > 0;
}
