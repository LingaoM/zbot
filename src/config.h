/*
 * Copyright (c) 2026 LingaoMeng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZephyrClaw - Config Module
 *
 * Runtime LLM configuration (RAM) and persistent storage (settings).
 * Persistent keys (subtree "zc"):
 *   "zc/summary" : conversation summary (string)
 *   "zc/apikey"  : API key — loaded at boot, applied to RAM config
 */

#ifndef ZEPHYRCLAW_CONFIG_H
#define ZEPHYRCLAW_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Field size limits */
#define CONFIG_API_KEY_MAX_LEN     256
#define CONFIG_ENDPOINT_MAX_LEN    128
#define CONFIG_MODEL_MAX_LEN       64
#define CONFIG_PROVIDER_ID_MAX_LEN 64

/**
 * @brief Runtime LLM configuration (RAM only, except api_key which can be
 *        persisted via config_save_api_key()).
 */
struct llm_config {
	char api_key[CONFIG_API_KEY_MAX_LEN];
	char endpoint_host[CONFIG_ENDPOINT_MAX_LEN];
	char endpoint_path[CONFIG_ENDPOINT_MAX_LEN];
	char model[CONFIG_MODEL_MAX_LEN];
	char provider_id[CONFIG_PROVIDER_ID_MAX_LEN];
	bool use_tls;
	uint16_t port;
	int max_tokens;
	int temperature_x100; /* stored as int, e.g. 70 = 0.7 */
};

/**
 * @brief Initialise config with defaults, then load persisted api_key.
 *        Must be called after memory_init() (which calls settings_subsys_init).
 */
void config_init(void);

/** @brief Get read-only pointer to the current runtime config. */
const struct llm_config *config_get(void);

/** @brief Set API key (RAM only). */
int config_set_api_key(const char *key);

/** @brief Set LLM endpoint host. */
int config_set_endpoint_host(const char *host);

/** @brief Set API path. */
int config_set_endpoint_path(const char *path);

/** @brief Set model name. */
int config_set_model(const char *model);

/** @brief Set X-Model-Provider-Id header value. */
int config_set_provider_id(const char *provider_id);

/** @brief Set TLS mode and port. */
void config_set_tls(bool use_tls, uint16_t port);

/** @brief Check whether an API key is set in RAM. */
bool config_has_api_key(void);

/** @brief Persist the current API key to flash. */
int config_save_api_key(const char *key);

/** @brief Delete the persisted API key from flash. */
int config_delete_api_key(void);

/** @brief Print current config to serial (redacts API key). */
void config_print_status(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYRCLAW_CONFIG_H */
