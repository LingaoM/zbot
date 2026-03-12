/*
 * Copyright (c) 2026 LingaoMeng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZephyrClaw - Persistent Config Module
 *
 * Stores the LLM API key to NVS (ID 12) so it survives reboots.
 * WiFi credentials are managed entirely by Zephyr's wifi_credentials
 * subsystem — use "wifi cred add" / "wifi cred list" shell commands.
 */

#ifndef ZEPHYRCLAW_CONFIG_H
#define ZEPHYRCLAW_CONFIG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_API_KEY_MAX_LEN 256

/**
 * @brief Load API key from NVS into soul. Call after memory_init().
 * Prints a hint to the user if no key is stored.
 */
void config_init(void);

/**
 * @brief Save the current soul API key to NVS flash.
 */
int config_save_api_key(const char *key);

/**
 * @brief Delete the stored API key from NVS.
 */
int config_delete_api_key(void);

/**
 * @brief Check if an API key is stored in NVS.
 */
bool config_has_api_key(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYRCLAW_CONFIG_H */
