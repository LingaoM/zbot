/*
 * Copyright (c) 2026 LingaoMeng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZephyrClaw - Tools Module Implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/heap_listener.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "tools.h"

LOG_MODULE_REGISTER(zephyrclaw_tools, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/* GPIO aliases from nRF7002-DK device tree                            */
/* LED0..LED1 and BUTTON0..BUTTON1                                     */
/* ------------------------------------------------------------------ */

#define LED0_NODE   DT_ALIAS(led0)
#define LED1_NODE   DT_ALIAS(led1)
#define BTN0_NODE   DT_ALIAS(sw0)

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
static const struct gpio_dt_spec g_led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#endif
#if DT_NODE_HAS_STATUS(LED1_NODE, okay)
static const struct gpio_dt_spec g_led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
#endif
#if DT_NODE_HAS_STATUS(BTN0_NODE, okay)
static const struct gpio_dt_spec g_btn0 = GPIO_DT_SPEC_GET(BTN0_NODE, gpios);
#endif

static bool g_gpio_init = false;

static void ensure_gpio_init(void)
{
    if (g_gpio_init) return;

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
    if (device_is_ready(g_led0.port)) {
        gpio_pin_configure_dt(&g_led0, GPIO_OUTPUT_INACTIVE);
    }
#endif
#if DT_NODE_HAS_STATUS(LED1_NODE, okay)
    if (device_is_ready(g_led1.port)) {
        gpio_pin_configure_dt(&g_led1, GPIO_OUTPUT_INACTIVE);
    }
#endif
#if DT_NODE_HAS_STATUS(BTN0_NODE, okay)
    if (device_is_ready(g_btn0.port)) {
        gpio_pin_configure_dt(&g_btn0, GPIO_INPUT);
    }
#endif
    g_gpio_init = true;
}

/* ------------------------------------------------------------------ */
/* Tiny JSON helpers                                                    */
/* ------------------------------------------------------------------ */

/*
 * Extract an integer value from a simple JSON object.
 * Looks for "key": <number>.
 * Returns default_val if not found.
 */
static int json_get_int(const char *json, const char *key, int default_val)
{
    if (!json || !key) return default_val;

    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return default_val;

    pos += strlen(search);
    while (*pos == ' ' || *pos == ':' || *pos == '\t') pos++;
    if (*pos == '\0') return default_val;

    return (int)strtol(pos, NULL, 10);
}

/*
 * Extract a string value from a simple JSON object.
 * Looks for "key": "value".
 */
static int json_get_str(const char *json, const char *key,
                        char *out, size_t out_len)
{
    if (!json || !key || !out) return -EINVAL;

    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *pos = strstr(json, search);
    if (!pos) return -ENOENT;

    pos += strlen(search);
    while (*pos == ' ' || *pos == ':' || *pos == '\t') pos++;
    if (*pos != '"') return -ENOENT;
    pos++; /* skip opening " */

    size_t i = 0;
    while (*pos != '\0' && *pos != '"' && i + 1 < out_len) {
        if (*pos == '\\') pos++; /* skip escape */
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return (int)i;
}

/* ------------------------------------------------------------------ */
/* Tool handlers                                                        */
/* ------------------------------------------------------------------ */

int tool_gpio_read(const char *args_json, char *result, size_t res_len)
{
    ensure_gpio_init();

    char pin_name[16] = {0};
    json_get_str(args_json, "pin", pin_name, sizeof(pin_name));

    int val = -1;

#if DT_NODE_HAS_STATUS(BTN0_NODE, okay)
    if (strcmp(pin_name, "button0") == 0 || strcmp(pin_name, "btn0") == 0) {
        if (device_is_ready(g_btn0.port)) {
            val = gpio_pin_get_dt(&g_btn0);
        }
    }
#endif
#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
    if (strcmp(pin_name, "led0") == 0) {
        if (device_is_ready(g_led0.port)) {
            val = gpio_pin_get_dt(&g_led0);
        }
    }
#endif
#if DT_NODE_HAS_STATUS(LED1_NODE, okay)
    if (strcmp(pin_name, "led1") == 0) {
        if (device_is_ready(g_led1.port)) {
            val = gpio_pin_get_dt(&g_led1);
        }
    }
#endif

    if (val < 0) {
        snprintf(result, res_len,
                 "{\"error\":\"unknown pin '%s'\",\"known_pins\":"
                 "[\"led0\",\"led1\",\"button0\"]}",
                 pin_name);
        return -ENODEV;
    }

    snprintf(result, res_len, "{\"pin\":\"%s\",\"value\":%d}", pin_name, val);
    return 0;
}

int tool_gpio_write(const char *args_json, char *result, size_t res_len)
{
    ensure_gpio_init();

    char pin_name[16] = {0};
    json_get_str(args_json, "pin", pin_name, sizeof(pin_name));
    int value = json_get_int(args_json, "value", -1);

    if (value < 0 || value > 1) {
        snprintf(result, res_len,
                 "{\"error\":\"value must be 0 or 1\"}");
        return -EINVAL;
    }

    int rc = -ENODEV;

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
    if (strcmp(pin_name, "led0") == 0 && device_is_ready(g_led0.port)) {
        rc = gpio_pin_set_dt(&g_led0, value);
    }
#endif
#if DT_NODE_HAS_STATUS(LED1_NODE, okay)
    if (strcmp(pin_name, "led1") == 0 && device_is_ready(g_led1.port)) {
        rc = gpio_pin_set_dt(&g_led1, value);
    }
#endif

    if (rc < 0) {
        snprintf(result, res_len,
                 "{\"error\":\"failed to write '%s': %d\"}",
                 pin_name, rc);
        return rc;
    }

    snprintf(result, res_len,
             "{\"pin\":\"%s\",\"value\":%d,\"status\":\"ok\"}",
             pin_name, value);
    return 0;
}

int tool_get_uptime(const char *args_json, char *result, size_t res_len)
{
    ARG_UNUSED(args_json);
    int64_t uptime_ms = k_uptime_get();
    snprintf(result, res_len,
             "{\"uptime_ms\":%lld,\"uptime_s\":%lld}",
             uptime_ms, uptime_ms / 1000);
    return 0;
}

int tool_get_board_info(const char *args_json, char *result, size_t res_len)
{
    ARG_UNUSED(args_json);
    snprintf(result, res_len,
             "{\"board\":\"nrf7002dk\","
             "\"soc\":\"nRF5340\","
             "\"wifi_chip\":\"nRF7002\","
             "\"rtos\":\"Zephyr\","
             "\"agent\":\"ZephyrClaw\","
             "\"version\":\"0.1.0\"}");
    return 0;
}

int tool_get_heap_info(const char *args_json, char *result, size_t res_len)
{
    ARG_UNUSED(args_json);

    /* Use kernel heap stats via CONFIG_SYS_HEAP_RUNTIME_STATS.
     * k_heap_sys_k_heap is the internal symbol for the system heap;
     * fall back to reporting total pool size if runtime stats unavailable. */
#if defined(CONFIG_SYS_HEAP_RUNTIME_STATS)
    struct sys_memory_stats stats;
    extern struct k_heap z_malloc_heap;
    sys_heap_runtime_stats_get(&z_malloc_heap.heap, &stats);
    snprintf(result, res_len,
             "{\"free_bytes\":%zu,\"allocated_bytes\":%zu,"
             "\"max_allocated_bytes\":%zu}",
             stats.free_bytes,
             stats.allocated_bytes,
             stats.max_allocated_bytes);
#else
    snprintf(result, res_len,
             "{\"note\":\"enable CONFIG_SYS_HEAP_RUNTIME_STATS for stats\","
             "\"heap_pool_size\":%d}",
             CONFIG_HEAP_MEM_POOL_SIZE);
#endif
    return 0;
}

int tool_echo(const char *args_json, char *result, size_t res_len)
{
    char message[128] = {0};
    json_get_str(args_json, "message", message, sizeof(message));
    snprintf(result, res_len, "{\"echo\":\"%s\"}", message);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Tool registry                                                        */
/* ------------------------------------------------------------------ */

static const struct tool_descriptor g_tools[] = {
    {
        .name = "gpio_read",
        .description = "Read the current level of a GPIO pin on the board. "
                       "Available pins: led0, led1, button0.",
        .parameters_json_schema =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"pin\":{\"type\":\"string\","
            "\"description\":\"Pin name: led0, led1, or button0\"}"
            "},\"required\":[\"pin\"]}",
        .handler = tool_gpio_read,
    },
    {
        .name = "gpio_write",
        .description = "Set the level of an output GPIO pin. "
                       "Writable pins: led0, led1.",
        .parameters_json_schema =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"pin\":{\"type\":\"string\","
            "\"description\":\"Pin name: led0 or led1\"},"
            "\"value\":{\"type\":\"integer\","
            "\"description\":\"0 for low/off, 1 for high/on\"}"
            "},\"required\":[\"pin\",\"value\"]}",
        .handler = tool_gpio_write,
    },
    {
        .name = "get_uptime",
        .description = "Get the device uptime in milliseconds and seconds.",
        .parameters_json_schema =
            "{\"type\":\"object\",\"properties\":{}}",
        .handler = tool_get_uptime,
    },
    {
        .name = "get_board_info",
        .description = "Get information about the hardware board and firmware.",
        .parameters_json_schema =
            "{\"type\":\"object\",\"properties\":{}}",
        .handler = tool_get_board_info,
    },
    {
        .name = "get_heap_info",
        .description = "Get the current system heap memory usage statistics.",
        .parameters_json_schema =
            "{\"type\":\"object\",\"properties\":{}}",
        .handler = tool_get_heap_info,
    },
    {
        .name = "echo",
        .description = "Echo a message back. Useful for testing tool calling.",
        .parameters_json_schema =
            "{\"type\":\"object\","
            "\"properties\":{"
            "\"message\":{\"type\":\"string\","
            "\"description\":\"Message to echo back\"}"
            "},\"required\":[\"message\"]}",
        .handler = tool_echo,
    },
};

#define TOOLS_COUNT ARRAY_SIZE(g_tools)

const struct tool_descriptor *tools_get_all(int *count)
{
    if (count) {
        *count = (int)TOOLS_COUNT;
    }
    return g_tools;
}

int tools_build_json(char *buf, size_t buf_len)
{
    size_t pos = 0;
    int n;

    n = snprintf(buf + pos, buf_len - pos, "[");
    if (n < 0 || (size_t)n >= buf_len - pos) return -ENOMEM;
    pos += n;

    for (size_t i = 0; i < TOOLS_COUNT; i++) {
        const struct tool_descriptor *t = &g_tools[i];

        n = snprintf(buf + pos, buf_len - pos,
                     "%s{"
                     "\"type\":\"function\","
                     "\"function\":{"
                     "\"name\":\"%s\","
                     "\"description\":\"%s\","
                     "\"parameters\":%s"
                     "}"
                     "}",
                     i > 0 ? "," : "",
                     t->name,
                     t->description,
                     t->parameters_json_schema);
        if (n < 0 || (size_t)n >= buf_len - pos) return -ENOMEM;
        pos += n;
    }

    n = snprintf(buf + pos, buf_len - pos, "]");
    if (n < 0 || (size_t)n >= buf_len - pos) return -ENOMEM;
    pos += n;

    return (int)pos;
}

int tools_execute(const char *name, const char *args_json,
                  char *result, size_t res_len)
{
    if (!name || !result) return -EINVAL;

    for (size_t i = 0; i < TOOLS_COUNT; i++) {
        if (strcmp(g_tools[i].name, name) == 0) {
            LOG_INF("Executing tool: %s args=%s", name, args_json ? args_json : "{}");
            return g_tools[i].handler(args_json ? args_json : "{}", result, res_len);
        }
    }

    snprintf(result, res_len, "{\"error\":\"unknown tool: %s\"}", name);
    return -ENOENT;
}
