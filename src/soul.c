/*
 * Copyright (c) 2026 LingaoMeng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZephyrClaw - Soul Module Implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <errno.h>

#include "soul.h"

LOG_MODULE_REGISTER(zephyrclaw_soul, LOG_LEVEL_INF);

/* Default system prompt — defines the agent's identity */
#define DEFAULT_SYSTEM_PROMPT \
    "You are ZephyrClaw, an open-source embedded AI agent running on a " \
    "Nordic nRF7002-DK development board powered by Zephyr RTOS. " \
    "You are concise, helpful, and hardware-aware. " \
    "You can control GPIOs, read sensors, and manage the device. " \
    "When using tools, always reason step-by-step before acting. " \
    "Keep responses short and suitable for a serial terminal."

#define DEFAULT_ENDPOINT_HOST "xxx"
#define DEFAULT_ENDPOINT_PATH "/v1/chat/completions"
#define DEFAULT_MODEL         "gpt-5.2"
#define DEFAULT_PROVIDER_ID   "azure_openai"
#define DEFAULT_MAX_TOKENS    512
#define DEFAULT_TEMP_X100     70   /* 0.7 */

/* Single global soul instance — credentials live only in RAM */
static struct soul_config g_soul;

void soul_init(void)
{
    memset(&g_soul, 0, sizeof(g_soul));

    strncpy(g_soul.endpoint_host, DEFAULT_ENDPOINT_HOST,
            SOUL_ENDPOINT_MAX_LEN - 1);
    strncpy(g_soul.endpoint_path, DEFAULT_ENDPOINT_PATH,
            SOUL_ENDPOINT_MAX_LEN - 1);
    strncpy(g_soul.model, DEFAULT_MODEL, SOUL_MODEL_MAX_LEN - 1);
    strncpy(g_soul.provider_id, DEFAULT_PROVIDER_ID,
            SOUL_PROVIDER_ID_MAX_LEN - 1);
    strncpy(g_soul.system_prompt, DEFAULT_SYSTEM_PROMPT,
            SOUL_SYSTEM_PROMPT_MAX - 1);

    g_soul.use_tls          = true;
    g_soul.port             = 443;
    g_soul.max_tokens       = DEFAULT_MAX_TOKENS;
    g_soul.temperature_x100 = DEFAULT_TEMP_X100;

    LOG_INF("Soul initialised. Endpoint: %s%s model: %s",
            g_soul.endpoint_host, g_soul.endpoint_path, g_soul.model);
}

int soul_set_api_key(const char *key)
{
    if (!key) {
        return -EINVAL;
    }
    if (strlen(key) >= SOUL_API_KEY_MAX_LEN) {
        LOG_ERR("API key too long (max %d chars)", SOUL_API_KEY_MAX_LEN - 1);
        return -EINVAL;
    }
    strncpy(g_soul.api_key, key, SOUL_API_KEY_MAX_LEN - 1);
    g_soul.api_key[SOUL_API_KEY_MAX_LEN - 1] = '\0';
    LOG_INF("API key set (%zu chars)", strlen(key));
    return 0;
}

int soul_set_endpoint_host(const char *host)
{
    if (!host || strlen(host) >= SOUL_ENDPOINT_MAX_LEN) {
        return -EINVAL;
    }
    strncpy(g_soul.endpoint_host, host, SOUL_ENDPOINT_MAX_LEN - 1);
    return 0;
}

int soul_set_endpoint_path(const char *path)
{
    if (!path || strlen(path) >= SOUL_ENDPOINT_MAX_LEN) {
        return -EINVAL;
    }
    strncpy(g_soul.endpoint_path, path, SOUL_ENDPOINT_MAX_LEN - 1);
    return 0;
}

int soul_set_model(const char *model)
{
    if (!model || strlen(model) >= SOUL_MODEL_MAX_LEN) {
        return -EINVAL;
    }
    strncpy(g_soul.model, model, SOUL_MODEL_MAX_LEN - 1);
    return 0;
}

int soul_set_provider_id(const char *provider_id)
{
    if (!provider_id || strlen(provider_id) >= SOUL_PROVIDER_ID_MAX_LEN) {
        return -EINVAL;
    }
    strncpy(g_soul.provider_id, provider_id, SOUL_PROVIDER_ID_MAX_LEN - 1);
    return 0;
}

int soul_set_system_prompt(const char *prompt)
{
    if (!prompt || strlen(prompt) >= SOUL_SYSTEM_PROMPT_MAX) {
        return -EINVAL;
    }
    strncpy(g_soul.system_prompt, prompt, SOUL_SYSTEM_PROMPT_MAX - 1);
    return 0;
}

void soul_set_tls(bool use_tls, uint16_t port)
{
    g_soul.use_tls = use_tls;
    g_soul.port    = port;
}

bool soul_has_api_key(void)
{
    return g_soul.api_key[0] != '\0';
}

const struct soul_config *soul_get(void)
{
    return &g_soul;
}

void soul_print_status(void)
{
    printk("=== ZephyrClaw Soul Status ===\n");
    printk("  Endpoint : %s:%u%s\n",
           g_soul.endpoint_host, g_soul.port, g_soul.endpoint_path);
    printk("  Model    : %s\n", g_soul.model);
    printk("  Provider : %s\n",
           g_soul.provider_id[0] ? g_soul.provider_id : "(not set)");
    printk("  TLS      : %s\n", g_soul.use_tls ? "yes" : "no");
    printk("  API Key  : %s\n",
           g_soul.api_key[0] ? "*** (set)" : "(not set)");
    printk("  Max Tok  : %d\n", g_soul.max_tokens);
    printk("  Temp     : %d.%02d\n",
           g_soul.temperature_x100 / 100,
           g_soul.temperature_x100 % 100);
}
