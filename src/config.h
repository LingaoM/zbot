/*
 * Copyright (c) 2026 LingaoMeng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZephyrClaw - Config Module
 *
 * Runtime LLM configuration (RAM) and persistent storage (settings).
 * Persistent keys (subtree "zc"):
 *   "zc/apikey"   : API key (optional) — loaded at boot, applied to RAM config
 *   "zc/wifi/ssid": WiFi SSID
 *   "zc/wifi/pass": WiFi passphrase
 * Note: "zc/summary" is managed by memory.c under the same settings subtree.
 */

#ifndef ZEPHYRCLAW_CONFIG_H
#define ZEPHYRCLAW_CONFIG_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Field size limits */
#define CONFIG_API_KEY_MAX_LEN     256
#define CONFIG_ENDPOINT_MAX_LEN    128
#define CONFIG_MODEL_MAX_LEN       64
#define CONFIG_PROVIDER_ID_MAX_LEN 64
#define CONFIG_WIFI_SSID_MAX_LEN   33  /* IEEE 802.11: max 32 bytes + NUL */
#define CONFIG_WIFI_PASS_MAX_LEN   65  /* WPA2: max 63 chars + NUL */

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

/**
 * @brief Save WiFi credentials to flash and trigger a connection attempt.
 *
 * Persists SSID and passphrase under "zc/wifi/ssid" and "zc/wifi/pass",
 * then calls wifi_connect() on the default interface.
 *
 * @param ssid  Network SSID (max CONFIG_WIFI_SSID_MAX_LEN-1 chars).
 * @param pass  Passphrase (max CONFIG_WIFI_PASS_MAX_LEN-1 chars).
 *              Pass NULL or empty string for open networks.
 * @return 0 on success, negative errno on failure.
 */
int config_wifi_connect(const char *ssid, const char *pass);

/** @brief Disconnect from current WiFi network. */
int config_wifi_disconnect(void);

/**
 * @brief Attempt connection using previously saved credentials.
 *
 * Called automatically on boot by config_init() if credentials exist.
 *
 * @return 0 if connection was attempted, -ENOENT if no credentials stored.
 */
int config_wifi_auto_connect(void);

/**
 * @brief Get saved WiFi SSID (empty string if not saved).
 *
 * @param buf  Output buffer.
 * @param len  Buffer size.
 */
void config_wifi_get_ssid(char *buf, size_t len);

/** @brief Print current config to serial (redacts API key and WiFi password). */
void config_print_status(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYRCLAW_CONFIG_H */
