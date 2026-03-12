/*
 * Copyright (c) 2026 LingaoMeng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZephyrClaw - Soul Module
 *
 * The Soul defines the Agent's identity: its system prompt, personality,
 * behavioral constraints, and runtime credentials (API key / endpoint).
 * Credentials are NEVER stored in flash — they live only in RAM and must
 * be provided via the Shell each boot.
 */

#ifndef ZEPHYRCLAW_SOUL_H
#define ZEPHYRCLAW_SOUL_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum lengths */
#define SOUL_API_KEY_MAX_LEN        256
#define SOUL_ENDPOINT_MAX_LEN       128
#define SOUL_MODEL_MAX_LEN          64
#define SOUL_SYSTEM_PROMPT_MAX      1024
#define SOUL_PROVIDER_ID_MAX_LEN    64

/**
 * @brief Agent soul / identity configuration (runtime, RAM-only).
 *
 * All credential fields are zero-initialised and must be set at runtime
 * via the Shell before any LLM call is made.
 */
struct soul_config {
    /* LLM API key — set via shell, never persisted to flash */
    char api_key[SOUL_API_KEY_MAX_LEN];

    /* OpenAI-compatible endpoint, e.g. "api.openai.com" */
    char endpoint_host[SOUL_ENDPOINT_MAX_LEN];

    /* API path, e.g. "/v1/chat/completions" */
    char endpoint_path[SOUL_ENDPOINT_MAX_LEN];

    /* Model name, e.g. "gpt-4o-mini" */
    char model[SOUL_MODEL_MAX_LEN];

    /* Whether TLS should be used */
    bool use_tls;

    /* Port (default 443 for TLS, 80 for plain) */
    uint16_t port;

    /* X-Model-Provider-Id header value (e.g. "azure_openai") */
    char provider_id[SOUL_PROVIDER_ID_MAX_LEN];

    /* System prompt defining the agent's personality */
    char system_prompt[SOUL_SYSTEM_PROMPT_MAX];

    /* Maximum tokens per response */
    int max_tokens;

    /* Temperature [0.0 .. 2.0] * 100 (stored as int to avoid float) */
    int temperature_x100;
};

/**
 * @brief Initialise the soul with sensible defaults.
 *
 * Clears credentials, sets default endpoint to api.openai.com and
 * populates the default system prompt.
 */
void soul_init(void);

/**
 * @brief Set the API key (RAM only, not persisted).
 * @param key  Null-terminated API key string.
 * @return 0 on success, -EINVAL if key is NULL or too long.
 */
int soul_set_api_key(const char *key);

/**
 * @brief Set the LLM endpoint host.
 * @param host  Hostname string, e.g. "api.openai.com".
 */
int soul_set_endpoint_host(const char *host);

/**
 * @brief Set the API path.
 * @param path  Path string, e.g. "/v1/chat/completions".
 */
int soul_set_endpoint_path(const char *path);

/**
 * @brief Set the model name.
 * @param model  Model identifier string.
 */
int soul_set_model(const char *model);

/**
 * @brief Set the X-Model-Provider-Id header value.
 * @param provider_id  Provider identifier string, e.g. "azure_openai".
 */
int soul_set_provider_id(const char *provider_id);

/**
 * @brief Set the system prompt.
 * @param prompt  System prompt string.
 */
int soul_set_system_prompt(const char *prompt);

/**
 * @brief Set TLS mode and port.
 */
void soul_set_tls(bool use_tls, uint16_t port);

/**
 * @brief Check whether the soul has a valid API key set.
 * @return true if API key is non-empty.
 */
bool soul_has_api_key(void);

/**
 * @brief Get a read-only pointer to the current soul configuration.
 */
const struct soul_config *soul_get(void);

/**
 * @brief Print current soul status to shell (redacts API key).
 */
void soul_print_status(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYRCLAW_SOUL_H */
