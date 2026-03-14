/*
 * Copyright (c) 2026 LingaoMeng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZephyrClaw - Config Module Implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <string.h>
#include <errno.h>

#include "config.h"

LOG_MODULE_REGISTER(zephyrclaw_config, LOG_LEVEL_INF);

#define DEFAULT_ENDPOINT_HOST "api.openai.com"
#define DEFAULT_ENDPOINT_PATH "/v1/chat/completions"
#define DEFAULT_MODEL         "gpt-5.2"
#define DEFAULT_PROVIDER_ID   "azure_openai"
#define DEFAULT_MAX_TOKENS    512
#define DEFAULT_TEMP_X100     70

static struct llm_config g_cfg = {
	.use_tls = true,
	.port = 443,
	.max_tokens = DEFAULT_MAX_TOKENS,
	.temperature_x100 = DEFAULT_TEMP_X100,
	.endpoint_host = DEFAULT_ENDPOINT_HOST,
	.endpoint_path = DEFAULT_ENDPOINT_PATH,
	.model = DEFAULT_MODEL,
	.provider_id = DEFAULT_PROVIDER_ID,
};

/* Saved WiFi credentials (RAM copy, loaded from settings on boot) */
static char g_wifi_ssid[CONFIG_WIFI_SSID_MAX_LEN];
static char g_wifi_pass[CONFIG_WIFI_PASS_MAX_LEN];

/* ------------------------------------------------------------------ */
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

	if (strcmp(name, "wifi/ssid") == 0) {
		read_len = MIN(len, sizeof(g_wifi_ssid) - 1);
		rc = read_cb(cb_arg, g_wifi_ssid, read_len);
		if (rc >= 0) {
			g_wifi_ssid[read_len] = '\0';
		}
		return rc < 0 ? rc : 0;
	}

	if (strcmp(name, "wifi/pass") == 0) {
		read_len = MIN(len, sizeof(g_wifi_pass) - 1);
		rc = read_cb(cb_arg, g_wifi_pass, read_len);
		if (rc >= 0) {
			g_wifi_pass[read_len] = '\0';
		}
		return rc < 0 ? rc : 0;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(zc_config, "zc", NULL, zc_config_set, NULL, NULL);

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

void config_init(void)
{
	/* settings_subsys_init() and settings_load_subtree("zc") are called by
	 * memory_init() before config_init(). Our handler zc_config_set() will
	 * have populated api_key and wifi credentials from flash at that point. */

	if (g_cfg.api_key[0] != '\0') {
		printk("[config] API key loaded from flash.\n");
	} else {
		printk("[config] No API key stored. Use: claw key <key>\n");
		printk("[config] To persist:             claw key-save\n");
	}

	LOG_INF("Config init. Endpoint: %s%s model: %s", g_cfg.endpoint_host,
		g_cfg.endpoint_path, g_cfg.model);

	/* Auto-connect WiFi if credentials were saved previously */
	if (g_wifi_ssid[0] != '\0') {
		printk("[config] Saved WiFi SSID found: %s — connecting...\n", g_wifi_ssid);
		int rc = config_wifi_auto_connect();

		if (rc < 0) {
			LOG_WRN("WiFi auto-connect request failed: %d", rc);
		}
	}
}

const struct llm_config *config_get(void)
{
	return &g_cfg;
}

int config_set_api_key(const char *key)
{
	if (!key || strlen(key) >= CONFIG_API_KEY_MAX_LEN) {
		return -EINVAL;
        }

	strncpy(g_cfg.api_key, key, sizeof(g_cfg.api_key) - 1);
	g_cfg.api_key[sizeof(g_cfg.api_key) - 1] = '\0';

	return 0;
}

int config_set_endpoint_host(const char *host)
{
	if (!host || strlen(host) >= CONFIG_ENDPOINT_MAX_LEN) {
		return -EINVAL;
	}

	strncpy(g_cfg.endpoint_host, host, sizeof(g_cfg.endpoint_host) - 1);

	return 0;
}

int config_set_endpoint_path(const char *path)
{
	if (!path || strlen(path) >= CONFIG_ENDPOINT_MAX_LEN) {
		return -EINVAL;
	}

	strncpy(g_cfg.endpoint_path, path, sizeof(g_cfg.endpoint_path) - 1);

	return 0;
}

int config_set_model(const char *model)
{
	if (!model || strlen(model) >= CONFIG_MODEL_MAX_LEN) {
		return -EINVAL;
	}

	strncpy(g_cfg.model, model, sizeof(g_cfg.model) - 1);

	return 0;
}

int config_set_provider_id(const char *provider_id)
{
	if (!provider_id || strlen(provider_id) >= CONFIG_PROVIDER_ID_MAX_LEN) {
		return -EINVAL;
	}

	strncpy(g_cfg.provider_id, provider_id, sizeof(g_cfg.provider_id) - 1);

	return 0;
}

void config_set_tls(bool use_tls, uint16_t port)
{
	g_cfg.use_tls = use_tls;
	g_cfg.port = port;
}

bool config_has_api_key(void)
{
	return g_cfg.api_key[0] != '\0';
}

int config_save_api_key(const char *key)
{
	int rc;

	if (!key) {
		return -EINVAL;
	}

	rc = settings_save_one("zc/apikey", key, strlen(key) + 1);
	if (rc < 0) {
		LOG_ERR("Failed to save API key: %d", rc);
	}

	return rc;
}

int config_delete_api_key(void)
{
	memset(g_cfg.api_key, 0, sizeof(g_cfg.api_key));

	return settings_delete("zc/apikey");
}

void config_print_status(void)
{
	printk("=== ZephyrClaw Config ===\n");
	printk("  Endpoint : %s:%u%s\n", g_cfg.endpoint_host, g_cfg.port, g_cfg.endpoint_path);
	printk("  Model    : %s\n", g_cfg.model);
	printk("  Provider : %s\n", g_cfg.provider_id[0] ? g_cfg.provider_id : "(not set)");
	printk("  TLS      : %s\n", g_cfg.use_tls ? "yes" : "no");
	printk("  API Key  : %s\n", g_cfg.api_key[0] ? "*** (set)" : "(not set)");
	printk("  Max Tok  : %d\n", g_cfg.max_tokens);
	printk("  Temp     : %d.%02d\n", g_cfg.temperature_x100 / 100,
	       g_cfg.temperature_x100 % 100);
	printk("  WiFi SSID: %s\n", g_wifi_ssid[0] ? g_wifi_ssid : "(not saved)");
}

/* ------------------------------------------------------------------ */
/* WiFi helpers                                                        */
/* ------------------------------------------------------------------ */

static int do_wifi_connect(const char *ssid, const char *pass)
{
	struct net_if *iface = net_if_get_default();
	struct wifi_connect_req_params params = {0};

	if (!iface) {
		LOG_ERR("No default network interface");
		return -ENODEV;
	}

	params.ssid = (const uint8_t *)ssid;
	params.ssid_length = strlen(ssid);
	params.security = (pass && pass[0] != '\0') ? WIFI_SECURITY_TYPE_PSK
						     : WIFI_SECURITY_TYPE_NONE;
	if (params.security == WIFI_SECURITY_TYPE_PSK) {
		params.psk = (const uint8_t *)pass;
		params.psk_length = strlen(pass);
	}
	params.channel = WIFI_CHANNEL_ANY;
	params.band = WIFI_FREQ_BAND_UNKNOWN;
	params.mfp = WIFI_MFP_OPTIONAL;

	return net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));
}

int config_wifi_connect(const char *ssid, const char *pass)
{
	int rc;

	if (!ssid || ssid[0] == '\0' || strlen(ssid) >= CONFIG_WIFI_SSID_MAX_LEN) {
		return -EINVAL;
	}
	if (pass && strlen(pass) >= CONFIG_WIFI_PASS_MAX_LEN) {
		return -EINVAL;
	}

	/* Persist credentials first */
	rc = settings_save_one("zc/wifi/ssid", ssid, strlen(ssid) + 1);
	if (rc < 0) {
		LOG_ERR("Failed to save WiFi SSID: %d", rc);
		return rc;
	}

	const char *p = (pass && pass[0] != '\0') ? pass : "";

	rc = settings_save_one("zc/wifi/pass", p, strlen(p) + 1);
	if (rc < 0) {
		LOG_ERR("Failed to save WiFi pass: %d", rc);
		return rc;
	}

	/* Update RAM copy */
	strncpy(g_wifi_ssid, ssid, sizeof(g_wifi_ssid) - 1);
	g_wifi_ssid[sizeof(g_wifi_ssid) - 1] = '\0';
	strncpy(g_wifi_pass, p, sizeof(g_wifi_pass) - 1);
	g_wifi_pass[sizeof(g_wifi_pass) - 1] = '\0';

	return do_wifi_connect(ssid, pass);
}

int config_wifi_disconnect(void)
{
	struct net_if *iface = net_if_get_default();

	if (!iface) {
		return -ENODEV;
	}

	return net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
}

int config_wifi_auto_connect(void)
{
	if (g_wifi_ssid[0] == '\0') {
		return -ENOENT;
	}

	return do_wifi_connect(g_wifi_ssid, g_wifi_pass);
}

void config_wifi_get_ssid(char *buf, size_t len)
{
	strncpy(buf, g_wifi_ssid, len - 1);
	buf[len - 1] = '\0';
}
