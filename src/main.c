/*
 * Copyright (c) 2026 LingaoMeng
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZephyrClaw - Main Entry Point
 *
 * Boot sequence:
 *   1. Initialise config (LLM connection defaults)
 *   2. Initialise memory (NVS mount, load persisted summary)
 *   3. Register built-in skills
 *   4. Initialise agent
 *   5. Wait for WiFi (managed by WPA supplicant via shell/auto-connect)
 *   6. Print welcome banner + instructions
 *   7. Idle — all interaction is via Shell commands (claw ...)
 *
 * IMPORTANT: The API key must be set at runtime via the shell:
 *     claw key sk-...
 * It is never stored in flash to prevent accidental key exposure.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>

#include "config.h"
#include "memory.h"
#include "agent.h"
#include "skill.h"
#include "tools.h"

LOG_MODULE_REGISTER(zephyrclaw_main, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

#define WIFI_MGMT_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_mgmt_event_callback g_wifi_cb;
static K_SEM_DEFINE(g_wifi_connected_sem, 0, 1);
static volatile bool g_wifi_connected = false;

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
		const struct wifi_status *status = (const struct wifi_status *)cb->info;
		if (status->status == 0) {
			LOG_INF("WiFi connected!");
			g_wifi_connected = true;
			k_sem_give(&g_wifi_connected_sem);
		} else {
			LOG_WRN("WiFi connect failed (status %d)", status->status);
		}
	} else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
		LOG_WRN("WiFi disconnected");
		g_wifi_connected = false;
	}
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

static void print_banner(void)
{
	printk("\n");
	printk("╔══════════════════════════════════════════════╗\n");
	printk("║        ZephyrClaw - Embedded AI Agent        ║\n");
	printk("║   Board: nRF7002-DK  |  RTOS: Zephyr         ║\n");
	printk("║   Version: 0.1.0     |  License: Apache-2.0  ║\n");
	printk("╚══════════════════════════════════════════════╝\n");
	printk("\n");
	printk("Quick start:\n");
	printk("  1. Connect to WiFi:\n");
	printk("       wifi connect -s <SSID> -p <password>\n");
	printk("  2. Set API key (not stored to flash):\n");
	printk("       claw key sk-...\n");
	printk("  3. [Optional] Change model/endpoint:\n");
	printk("       claw model gpt-4o-mini\n");
	printk("       claw host api.openai.com\n");
	printk("  4. Chat:\n");
	printk("       claw chat Hello! What can you do?\n");
	printk("  5. Other commands:\n");
	printk("       claw status    -- show config\n");
	printk("       claw history   -- show conversation\n");
	printk("       claw summary   -- show NVS summary\n");
	printk("       claw skill list\n");
	printk("       claw tools\n");
	printk("\n");
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */

int main(void)
{
	int rc;

	printk("Start\n");

	/* 1. Memory — NVS mount and load persisted summary */
	rc = memory_init();
	if (rc < 0) {
		LOG_WRN("Memory/NVS init warning: %d (continuing without persistence)", rc);
	}

	/* 2. Config — load WiFi credentials + API key from NVS, auto-connect */
	config_init();

	/* 3. Skills */
	skills_register_builtins();

	/* 4. Agent */
	rc = agent_init();
	if (rc < 0) {
		LOG_ERR("Agent init failed: %d", rc);
		return rc;
	}

	/* 5. Register WiFi event callback */
	net_mgmt_init_event_callback(&g_wifi_cb, wifi_event_handler, WIFI_MGMT_EVENTS);
	net_mgmt_add_event_callback(&g_wifi_cb);

	/* 6. Print banner */
	print_banner();

	/* Log persisted summary if available */
	const char *summary = memory_get_summary();
	if (summary != NULL) {
		LOG_DBG("Loaded prior context from NVS: %.80s...", summary);
		printk("  [Prior session context loaded from NVS]\n\n");
	}

	/* 7. Idle — shell handles everything else */
	while (1) {
		k_sleep(K_SECONDS(30));

		if (g_wifi_connected) {
			LOG_DBG("Heartbeat — WiFi up, agent %s", agent_is_busy() ? "busy" : "idle");
		}
	}

	return 0;
}
