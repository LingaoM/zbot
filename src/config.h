/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZBot - Config Module
 *
 * Runtime LLM configuration persisted to NVS via Zephyr settings subsystem.
 * Every config_set_*() call writes through to flash immediately.
 * Settings are loaded back at boot inside config_init().
 *
 * Persistent keys (subtree "zbot"):
 *   "zbot/apikey"      : API key
 *   "zbot/host"        : LLM endpoint host
 *   "zbot/path"        : LLM endpoint path
 *   "zbot/model"       : model name
 *   "zbot/provider_id" : X-Model-Provider-Id header value
 *   "zbot/use_tls"     : TLS enabled flag (uint8_t)
 *   "zbot/tls_verify"  : TLS peer verification flag (uint8_t)
 *   "zbot/port"        : TCP port (uint16_t)
 *
 * Note: "zbot/summary" is managed by memory.c under the same subtree.
 * WiFi credentials are stored by the Zephyr wifi_credentials subsystem.
 */

#ifndef ZBOT_CONFIG_H
#define ZBOT_CONFIG_H

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

/**
 * @brief Runtime LLM configuration — all fields persisted to NVS.
 */
struct llm_config {
	char api_key[CONFIG_API_KEY_MAX_LEN];
	char endpoint_host[CONFIG_ENDPOINT_MAX_LEN];
	char endpoint_path[CONFIG_ENDPOINT_MAX_LEN];
	char model[CONFIG_MODEL_MAX_LEN];
	char provider_id[CONFIG_PROVIDER_ID_MAX_LEN];
	bool use_tls;
	bool tls_verify;
	uint16_t port;
	int max_tokens;
	int temperature_x100; /* stored as int, e.g. 70 = 0.7 */
};

/**
 * @brief Initialise config with defaults, then reload all persisted values
 *        from NVS.  Must be called after settings_subsys_init().
 */
void config_init(void);

/** @brief Get read-only pointer to the current runtime config. */
const struct llm_config *config_get(void);

/** @brief Set API key and persist to flash. */
int config_set_api_key(const char *key);

/** @brief Set LLM endpoint host and persist to flash. */
int config_set_endpoint_host(const char *host);

/** @brief Set API path and persist to flash. */
int config_set_endpoint_path(const char *path);

/** @brief Set model name and persist to flash. */
int config_set_model(const char *model);

/** @brief Set X-Model-Provider-Id header value and persist to flash. */
int config_set_provider_id(const char *provider_id);

/** @brief Set TLS mode and port and persist to flash. */
int config_set_tls(bool use_tls, uint16_t port);

/** @brief Set TLS peer verification and persist to flash. */
int config_set_tls_verify(bool tls_verify);

/** @brief Check whether an API key is set in RAM. */
bool config_has_api_key(void);

/** @brief Delete the API key from RAM and flash. */
int config_delete_api_key(void);

/** @brief Reset all config to defaults and delete all persisted keys from flash. */
int config_reset(void);

/**
 * @brief Save WiFi credentials via wifi_credentials subsystem and connect.
 *
 * @param ssid  Network SSID.
 * @param pass  Passphrase, or NULL/empty for open networks.
 * @return 0 on success, negative errno on failure.
 */
int config_wifi_connect(const char *ssid, const char *pass);

/** @brief Disconnect from current WiFi network. */
int config_wifi_disconnect(void);

/**
 * @brief Auto-connect using all credentials stored in wifi_credentials.
 *
 * Issues NET_REQUEST_WIFI_CONNECT_STORED on the default interface.
 * Called automatically on boot by config_init() if credentials exist.
 */
void config_wifi_auto_connect(void);

/** @brief Print current config to serial (redacts API key). */
void config_print_status(void);

#ifdef __cplusplus
}
#endif

#endif /* ZBOT_CONFIG_H */
