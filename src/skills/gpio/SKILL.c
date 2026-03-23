/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Skill: gpio
 * Unified GPIO operations: read, write, blink, sos.
 * arg: JSON string with "action" field plus action-specific fields.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "skill.h"
#include "json_util.h"

/* ------------------------------------------------------------------ */
/* GPIO setup                                                          */
/* ------------------------------------------------------------------ */

#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define BTN0_NODE DT_ALIAS(sw0)

#define HAS_LED0 (DT_NODE_EXISTS(LED0_NODE) && DT_NODE_HAS_STATUS(LED0_NODE, okay))
#define HAS_LED1 (DT_NODE_EXISTS(LED1_NODE) && DT_NODE_HAS_STATUS(LED1_NODE, okay))
#define HAS_BTN0 (DT_NODE_EXISTS(BTN0_NODE) && DT_NODE_HAS_STATUS(BTN0_NODE, okay))

#if HAS_LED0
static const struct gpio_dt_spec g_led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#endif
#if HAS_LED1
static const struct gpio_dt_spec g_led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
#endif
#if HAS_BTN0
static const struct gpio_dt_spec g_btn0 = GPIO_DT_SPEC_GET(BTN0_NODE, gpios);
#endif

static bool g_init;

static void ensure_init(void)
{
	if (g_init) {
		return;
	}
#if HAS_LED0
	if (device_is_ready(g_led0.port)) {
		gpio_pin_configure_dt(&g_led0, GPIO_OUTPUT_INACTIVE);
	}
#endif
#if HAS_LED1
	if (device_is_ready(g_led1.port)) {
		gpio_pin_configure_dt(&g_led1, GPIO_OUTPUT_INACTIVE);
	}
#endif
#if HAS_BTN0
	if (device_is_ready(g_btn0.port)) {
		gpio_pin_configure_dt(&g_btn0, GPIO_INPUT);
	}
#endif
	g_init = true;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static int get_int(const char *json, const char *key, int default_val)
{
	char search[64];
	const char *pos;

	snprintf(search, sizeof(search), "\"%s\"", key);
	pos = strstr(json, search);
	if (!pos) {
		return default_val;
	}
	pos += strlen(search);
	while (*pos == ' ' || *pos == ':' || *pos == '\t') {
		pos++;
	}
	return (int)strtol(pos, NULL, 10);
}

static int set_led(const char *pin, int value)
{
#if HAS_LED0
	if (strcmp(pin, "led0") == 0 && device_is_ready(g_led0.port)) {
		return gpio_pin_set_dt(&g_led0, value);
	}
#endif
#if HAS_LED1
	if (strcmp(pin, "led1") == 0 && device_is_ready(g_led1.port)) {
		return gpio_pin_set_dt(&g_led1, value);
	}
#endif
	return -ENODEV;
}

/* ------------------------------------------------------------------ */
/* Actions                                                             */
/* ------------------------------------------------------------------ */

static int action_read(const char *arg, char *result, size_t res_len)
{
	char pin[16] = {0};
	int val = -1;

	json_get_str(arg, "pin", pin, sizeof(pin));

#if HAS_BTN0
	if ((strcmp(pin, "button0") == 0 || strcmp(pin, "btn0") == 0) &&
	    device_is_ready(g_btn0.port)) {
		val = gpio_pin_get_dt(&g_btn0);
	}
#endif
#if HAS_LED0
	if (strcmp(pin, "led0") == 0 && device_is_ready(g_led0.port)) {
		val = gpio_pin_get_dt(&g_led0);
	}
#endif
#if HAS_LED1
	if (strcmp(pin, "led1") == 0 && device_is_ready(g_led1.port)) {
		val = gpio_pin_get_dt(&g_led1);
	}
#endif

	if (val < 0) {
		snprintf(result, res_len,
			 "{\"error\":\"unknown pin '%s'\","
			 "\"known_pins\":[\"led0\",\"led1\",\"button0\"]}",
			 pin);
		return -ENODEV;
	}
	snprintf(result, res_len, "{\"pin\":\"%s\",\"value\":%d}", pin, val);
	return 0;
}

static int action_write(const char *arg, char *result, size_t res_len)
{
	char pin[16] = {0};
	int value = get_int(arg, "value", -1);
	int rc;

	json_get_str(arg, "pin", pin, sizeof(pin));

	if (value < 0 || value > 1) {
		snprintf(result, res_len, "{\"error\":\"value must be 0 or 1\"}");
		return -EINVAL;
	}

	rc = set_led(pin, value);
	if (rc < 0) {
		snprintf(result, res_len,
			 "{\"error\":\"failed to write '%s': %d\"}", pin, rc);
		return rc;
	}
	snprintf(result, res_len,
		 "{\"pin\":\"%s\",\"value\":%d,\"status\":\"ok\"}", pin, value);
	return 0;
}

static int action_blink(const char *arg, char *result, size_t res_len)
{
	int times = get_int(arg, "count", 3);

	if (times <= 0 || times > 20) {
		times = 3;
	}

	for (int i = 0; i < times; i++) {
		set_led("led0", 1);
		k_msleep(300);
		set_led("led0", 0);
		k_msleep(300);
	}

	snprintf(result, res_len, "{\"action\":\"blink\",\"count\":%d,\"status\":\"ok\"}",
		 times);
	return 0;
}

static int action_sos(const char *arg, char *result, size_t res_len)
{
	ARG_UNUSED(arg);

	/* S = ... O = --- S = ... */
	static const int pattern[] = {200, 200, 200, 600, 600, 600, 200, 200, 200};

	for (int i = 0; i < (int)ARRAY_SIZE(pattern); i++) {
		set_led("led0", 1);
		k_msleep(pattern[i]);
		set_led("led0", 0);
		k_msleep(200);
	}

	snprintf(result, res_len, "{\"action\":\"sos\",\"status\":\"ok\"}");
	return 0;
}

/* ------------------------------------------------------------------ */
/* Embedded Markdown documentation                                     */
/* ------------------------------------------------------------------ */

static const char gpio_md[] = {
#include "skills/gpio/SKILL.md.inc"
	0x00
};

/* ------------------------------------------------------------------ */
/* Skill handler                                                       */
/* ------------------------------------------------------------------ */

static int gpio_handler(const char *arg, char *result, size_t res_len)
{
	char action[16] = {0};

	ensure_init();

	if (!arg || arg[0] == '\0') {
		snprintf(result, res_len,
			 "{\"error\":\"gpio: missing 'action' field\","
			 "\"actions\":[\"read\",\"write\",\"blink\",\"sos\"]}");
		return -EINVAL;
	}

	json_get_str(arg, "action", action, sizeof(action));

	if (strcmp(action, "read") == 0) {
		return action_read(arg, result, res_len);
	}
	if (strcmp(action, "write") == 0) {
		return action_write(arg, result, res_len);
	}
	if (strcmp(action, "blink") == 0) {
		return action_blink(arg, result, res_len);
	}
	if (strcmp(action, "sos") == 0) {
		return action_sos(arg, result, res_len);
	}

	snprintf(result, res_len,
		 "{\"error\":\"unknown action '%s'\","
		 "\"actions\":[\"read\",\"write\",\"blink\",\"sos\"]}",
		 action);
	return -ENOENT;
}

SKILL_DEFINE(gpio_skill, "gpio",
	     "GPIO operations: read pin, write pin, blink LED, SOS pattern.",
	     gpio_md, sizeof(gpio_md) - 1, gpio_handler);
