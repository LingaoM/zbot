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
#include <string.h>
#include <errno.h>

#include "config.h"

LOG_MODULE_REGISTER(zephyrclaw_config, LOG_LEVEL_INF);

#define DEFAULT_ENDPOINT_HOST "xxx"
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

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(zc_config, "zc", NULL, zc_config_set, NULL, NULL);

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

void config_init(void)
{
	/* settings_subsys_init() already called by memory_init().
	 * settings_load_subtree("zc") already called by memory_init().
	 * Our handler zc_config_set() will have populated api_key if stored. */
	if (g_cfg.api_key[0] != '\0') {
		printk("[config] API key loaded from flash.\n");
	} else {
		printk("[config] No API key stored. Use: claw key <key>\n");
		printk("[config] To persist:             claw key-save\n");
	}

	LOG_INF("Config init. Endpoint: %s%s model: %s", g_cfg.endpoint_host,
		g_cfg.endpoint_path, g_cfg.model);
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
}
