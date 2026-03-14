/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZBot - Config Module Implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi_credentials.h>
#include <string.h>
#include <errno.h>

#include "config.h"

LOG_MODULE_REGISTER(zbot_config, LOG_LEVEL_INF);

#define DEFAULT_ENDPOINT_HOST "openrouter.ai"
#define DEFAULT_ENDPOINT_PATH "/api/v1/chat/completions"
#define DEFAULT_MODEL         "minimax/minimax-m2.5"
#define DEFAULT_MAX_TOKENS    512
#define DEFAULT_TEMP_X100     70

static struct llm_config g_cfg = {
	.use_tls = true,
	.tls_verify = true,
	.port = 443,
	.max_tokens = DEFAULT_MAX_TOKENS,
	.temperature_x100 = DEFAULT_TEMP_X100,
	.endpoint_host = DEFAULT_ENDPOINT_HOST,
	.endpoint_path = DEFAULT_ENDPOINT_PATH,
	.model = DEFAULT_MODEL,
};

/* ------------------------------------------------------------------ */
/* Settings handler — loads all persisted fields at boot              */
/* ------------------------------------------------------------------ */

static int zc_config_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	size_t read_len;
	int rc;

	if (strcmp(name, "apikey") == 0) {
		read_len = MIN(len, sizeof(g_cfg.api_key) - 1);
		rc = read_cb(cb_arg, g_cfg.api_key, read_len);
		if (rc >= 0) {
			g_cfg.api_key[read_len] = '\0';
		}
		return rc < 0 ? rc : 0;
	}

	if (strcmp(name, "host") == 0) {
		read_len = MIN(len, sizeof(g_cfg.endpoint_host) - 1);
		rc = read_cb(cb_arg, g_cfg.endpoint_host, read_len);
		if (rc >= 0) {
			g_cfg.endpoint_host[read_len] = '\0';
		}
		return rc < 0 ? rc : 0;
	}

	if (strcmp(name, "path") == 0) {
		read_len = MIN(len, sizeof(g_cfg.endpoint_path) - 1);
		rc = read_cb(cb_arg, g_cfg.endpoint_path, read_len);
		if (rc >= 0) {
			g_cfg.endpoint_path[read_len] = '\0';
		}
		return rc < 0 ? rc : 0;
	}

	if (strcmp(name, "model") == 0) {
		read_len = MIN(len, sizeof(g_cfg.model) - 1);
		rc = read_cb(cb_arg, g_cfg.model, read_len);
		if (rc >= 0) {
			g_cfg.model[read_len] = '\0';
		}
		return rc < 0 ? rc : 0;
	}

	if (strcmp(name, "provider_id") == 0) {
		read_len = MIN(len, sizeof(g_cfg.provider_id) - 1);
		rc = read_cb(cb_arg, g_cfg.provider_id, read_len);
		if (rc >= 0) {
			g_cfg.provider_id[read_len] = '\0';
		}
		return rc < 0 ? rc : 0;
	}

	if (strcmp(name, "use_tls") == 0 && len == sizeof(uint8_t)) {
		uint8_t v;

		rc = read_cb(cb_arg, &v, sizeof(v));
		if (rc >= 0) {
			g_cfg.use_tls = (bool)v;
		}
		return rc < 0 ? rc : 0;
	}

	if (strcmp(name, "tls_verify") == 0 && len == sizeof(uint8_t)) {
		uint8_t v;

		rc = read_cb(cb_arg, &v, sizeof(v));
		if (rc >= 0) {
			g_cfg.tls_verify = (bool)v;
		}
		return rc < 0 ? rc : 0;
	}

	if (strcmp(name, "port") == 0 && len == sizeof(uint16_t)) {
		rc = read_cb(cb_arg, &g_cfg.port, sizeof(g_cfg.port));
		return rc < 0 ? rc : 0;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(zc_config, "zbot", NULL, zc_config_set, NULL, NULL);

/* ------------------------------------------------------------------ */
/* Init                                                               */
/* ------------------------------------------------------------------ */

void config_init(void)
{
	/* settings_subsys_init() and settings_load_subtree("zbot") are called
	 * by memory_init() before config_init(). Our handler above will have
	 * already overwritten the defaults with any persisted values. */

	if (g_cfg.api_key[0] != '\0') {
		printk("[config] API key loaded from flash.\n");
	} else {
		printk("[config] No API key stored. Use: zbot key <key>\n");
	}

	LOG_INF("Config init. Endpoint: %s%s model: %s", g_cfg.endpoint_host,
		g_cfg.endpoint_path, g_cfg.model);

	/* Auto-connect WiFi if credentials were saved in wifi_credentials */
	if (!wifi_credentials_is_empty()) {
		printk("[config] WiFi credentials found — auto-connecting...\n");
		config_wifi_auto_connect();
	}
}

/* ------------------------------------------------------------------ */
/* Getters / setters                                                  */
/* ------------------------------------------------------------------ */

const struct llm_config *config_get(void)
{
	return &g_cfg;
}

int config_set_api_key(const char *key)
{
	int rc;

	if (!key || strlen(key) >= CONFIG_API_KEY_MAX_LEN) {
		return -EINVAL;
	}

	strncpy(g_cfg.api_key, key, sizeof(g_cfg.api_key) - 1);
	g_cfg.api_key[sizeof(g_cfg.api_key) - 1] = '\0';

	rc = settings_save_one("zbot/apikey", g_cfg.api_key, strlen(g_cfg.api_key) + 1);
	if (rc < 0) {
		LOG_ERR("Failed to persist api_key: %d", rc);
	}

	return rc;
}

int config_set_endpoint_host(const char *host)
{
	int rc;

	if (!host || strlen(host) >= CONFIG_ENDPOINT_MAX_LEN) {
		return -EINVAL;
	}

	strncpy(g_cfg.endpoint_host, host, sizeof(g_cfg.endpoint_host) - 1);
	g_cfg.endpoint_host[sizeof(g_cfg.endpoint_host) - 1] = '\0';

	rc = settings_save_one("zbot/host", g_cfg.endpoint_host,
			       strlen(g_cfg.endpoint_host) + 1);
	if (rc < 0) {
		LOG_ERR("Failed to persist host: %d", rc);
	}

	return rc;
}

int config_set_endpoint_path(const char *path)
{
	int rc;

	if (!path || strlen(path) >= CONFIG_ENDPOINT_MAX_LEN) {
		return -EINVAL;
	}

	strncpy(g_cfg.endpoint_path, path, sizeof(g_cfg.endpoint_path) - 1);
	g_cfg.endpoint_path[sizeof(g_cfg.endpoint_path) - 1] = '\0';

	rc = settings_save_one("zbot/path", g_cfg.endpoint_path,
			       strlen(g_cfg.endpoint_path) + 1);
	if (rc < 0) {
		LOG_ERR("Failed to persist path: %d", rc);
	}

	return rc;
}

int config_set_model(const char *model)
{
	int rc;

	if (!model || strlen(model) >= CONFIG_MODEL_MAX_LEN) {
		return -EINVAL;
	}

	strncpy(g_cfg.model, model, sizeof(g_cfg.model) - 1);
	g_cfg.model[sizeof(g_cfg.model) - 1] = '\0';

	rc = settings_save_one("zbot/model", g_cfg.model, strlen(g_cfg.model) + 1);
	if (rc < 0) {
		LOG_ERR("Failed to persist model: %d", rc);
	}

	return rc;
}

int config_set_provider_id(const char *provider_id)
{
	int rc;

	if (!provider_id || strlen(provider_id) >= CONFIG_PROVIDER_ID_MAX_LEN) {
		return -EINVAL;
	}

	strncpy(g_cfg.provider_id, provider_id, sizeof(g_cfg.provider_id) - 1);
	g_cfg.provider_id[sizeof(g_cfg.provider_id) - 1] = '\0';

	rc = settings_save_one("zbot/provider_id", g_cfg.provider_id,
			       strlen(g_cfg.provider_id) + 1);
	if (rc < 0) {
		LOG_ERR("Failed to persist provider_id: %d", rc);
	}

	return rc;
}

int config_set_tls(bool use_tls, uint16_t port)
{
	uint8_t v = (uint8_t)use_tls;
	int rc;

	g_cfg.use_tls = use_tls;
	g_cfg.port = port;

	rc = settings_save_one("zbot/use_tls", &v, sizeof(v));
	if (rc < 0) {
		LOG_ERR("Failed to persist use_tls: %d", rc);
		return rc;
	}

	rc = settings_save_one("zbot/port", &g_cfg.port, sizeof(g_cfg.port));
	if (rc < 0) {
		LOG_ERR("Failed to persist port: %d", rc);
	}

	return rc;
}

int config_set_tls_verify(bool tls_verify)
{
	uint8_t v = (uint8_t)tls_verify;
	int rc;

	g_cfg.tls_verify = tls_verify;

	rc = settings_save_one("zbot/tls_verify", &v, sizeof(v));
	if (rc < 0) {
		LOG_ERR("Failed to persist tls_verify: %d", rc);
	}

	return rc;
}

bool config_has_api_key(void)
{
	return g_cfg.api_key[0] != '\0';
}

int config_delete_api_key(void)
{
	memset(g_cfg.api_key, 0, sizeof(g_cfg.api_key));

	return settings_delete("zbot/apikey");
}

int config_reset(void)
{
	static const struct llm_config defaults = {
		.use_tls = true,
		.tls_verify = true,
		.port = 443,
		.max_tokens = DEFAULT_MAX_TOKENS,
		.temperature_x100 = DEFAULT_TEMP_X100,
		.endpoint_host = DEFAULT_ENDPOINT_HOST,
		.endpoint_path = DEFAULT_ENDPOINT_PATH,
		.model = DEFAULT_MODEL,
	};
	int rc;
	int ret = 0;

	g_cfg = defaults;

	rc = settings_delete("zbot/apikey");
	if (rc < 0) {
		ret = rc;
	}
	rc = settings_delete("zbot/host");
	if (rc < 0) {
		ret = rc;
	}
	rc = settings_delete("zbot/path");
	if (rc < 0) {
		ret = rc;
	}
	rc = settings_delete("zbot/model");
	if (rc < 0) {
		ret = rc;
	}
	rc = settings_delete("zbot/provider_id");
	if (rc < 0) {
		ret = rc;
	}
	rc = settings_delete("zbot/use_tls");
	if (rc < 0) {
		ret = rc;
	}
	rc = settings_delete("zbot/tls_verify");
	if (rc < 0) {
		ret = rc;
	}
	rc = settings_delete("zbot/port");
	if (rc < 0) {
		ret = rc;
	}

	return ret;
}

void config_print_status(void)
{
	printk("=== ZBot Config ===\n");
	printk("  Endpoint : %s:%u%s\n", g_cfg.endpoint_host, g_cfg.port, g_cfg.endpoint_path);
	printk("  Model    : %s\n", g_cfg.model);
	printk("  Provider : %s\n", g_cfg.provider_id[0] ? g_cfg.provider_id : "(not set)");
	printk("  TLS      : %s\n", g_cfg.use_tls ? "yes" : "no");
	printk("  TLS Vfy  : %s\n", g_cfg.tls_verify ? "yes" : "no");
	printk("  API Key  : %s\n", g_cfg.api_key[0] ? "*** (set)" : "(not set)");
	printk("  Max Tok  : %d\n", g_cfg.max_tokens);
	printk("  Temp     : %d.%02d\n", g_cfg.temperature_x100 / 100,
	       g_cfg.temperature_x100 % 100);
	printk("  WiFi cred: %s\n", wifi_credentials_is_empty() ? "(not saved)" : "(saved)");
}

/* ------------------------------------------------------------------ */
/* WiFi helpers (backed by Zephyr wifi_credentials subsystem)         */
/* ------------------------------------------------------------------ */

int config_wifi_connect(const char *ssid, const char *pass)
{
	struct net_if *iface = net_if_get_wifi_sta();
	struct wifi_connect_req_params params = {0};
	enum wifi_security_type sec;
	int rc;

	if (!ssid || ssid[0] == '\0') {
		return -EINVAL;
	}

	if (!iface) {
		LOG_ERR("No default network interface");
		return -ENODEV;
	}

	sec = (pass && pass[0] != '\0') ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;

	/* Persist via wifi_credentials subsystem (settings+NVS backend) */
	rc = wifi_credentials_set_personal(ssid, strlen(ssid), sec, NULL, 0, pass ? pass : "",
					   pass ? strlen(pass) : 0, 0, 0, 0);
	if (rc < 0) {
		LOG_ERR("Failed to save WiFi credentials: %d", rc);
		return rc;
	}

	/* Issue connect request immediately */
	params.ssid = (const uint8_t *)ssid;
	params.ssid_length = strlen(ssid);
	params.security = sec;
	if (sec == WIFI_SECURITY_TYPE_PSK) {
		params.psk = (const uint8_t *)pass;
		params.psk_length = strlen(pass);
	}
	params.channel = WIFI_CHANNEL_ANY;
	params.band = WIFI_FREQ_BAND_UNKNOWN;
	params.mfp = WIFI_MFP_OPTIONAL;

	return net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
}

int config_wifi_disconnect(void)
{
	struct net_if *iface = net_if_get_wifi_sta();

	if (!iface) {
		return -ENODEV;
	}

	return net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
}

static void wifi_connect_handler(struct k_work *work)
{
	struct net_if *iface = net_if_get_wifi_sta();
	int rc;

	if (!iface) {
		return;
	}

	rc = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);
	if (rc) {
		LOG_ERR("WIFI Request Connection failed (err %d)", rc);
	}
}

static K_WORK_DELAYABLE_DEFINE(wifi_connect, wifi_connect_handler);

void config_wifi_auto_connect(void)
{
	k_work_reschedule(&wifi_connect, K_SECONDS(1));
}
