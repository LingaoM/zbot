/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZBot - Main Entry Point
 *
 * WiFi:
 *   Use 'zbot wifi connect <ssid> [pass]' to connect and save credentials.
 *   On next boot, config_init() auto-connects using the saved credentials.
 *
 * API key:
 *   'zbot key <key>'  — set and persist to flash
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>

#include "config.h"
#include "memory.h"
#include "llm_client.h"
#include "agent.h"
#include "skill.h"
#include "tools.h"

LOG_MODULE_REGISTER(zbot_main, LOG_LEVEL_INF);

#define WIFI_MGMT_EVENTS (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT)

static struct net_mgmt_event_callback g_wifi_cb;
static K_SEM_DEFINE(g_wifi_connected_sem, 0, 1);
static volatile bool g_wifi_connected;

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

static void print_banner(void)
{
	printk("\n");
	printk("╔══════════════════════════════════════════════╗\n");
	printk("║        ZBot - Embedded AI Agent              ║\n");
	printk("║   Board: nRF7002-DK  |  RTOS: Zephyr         ║\n");
	printk("║   Version: 0.1.0     |  License: Apache-2.0  ║\n");
	printk("╚══════════════════════════════════════════════╝\n");
	printk("\n");
	printk("Quick start:\n");
	printk("  1. Connect to WiFi (saves credentials for auto-connect):\n");
	printk("       zbot wifi connect <SSID> <password>\n");
	printk("  2. Set API key (saved to flash):\n");
	printk("       zbot key sk-...\n");
	printk("  3. [Optional] Change model/endpoint:\n");
	printk("       zbot model gpt-4o-mini\n");
	printk("       zbot host openrouter.ai\n");
	printk("  4. Chat:\n");
	printk("       zbot chat Hello! What can you do?\n");
	printk("  5. Other commands:\n");
	printk("       zbot status              -- show config + WiFi SSID\n");
	printk("       zbot wifi status         -- show saved WiFi SSID\n");
	printk("       zbot history             -- show conversation\n");
	printk("       zbot summary             -- show NVS summary\n");
	printk("       zbot skill list\n");
	printk("       zbot tools\n");
	printk("\n");
}

int main(void)
{
	const char *summary;
	int rc;

	printk("Start\n");

	/* Memory — NVS mount and load persisted summary */
	rc = memory_init();
	if (rc < 0) {
		LOG_WRN("Memory/NVS init warning: %d (continuing without persistence)", rc);
	}

	/* Register WiFi event callback */
	net_mgmt_init_event_callback(&g_wifi_cb, wifi_event_handler, WIFI_MGMT_EVENTS);
	net_mgmt_add_event_callback(&g_wifi_cb);

	/* Config — load API key + WiFi credentials from NVS; auto-connect WiFi */
	config_init();

	/* LLM client — register TLS CA certificate once at boot */
	llm_client_init();

	/* Skills */
	skills_register_builtins();

	/* Agent */
	rc = agent_init();
	if (rc < 0) {
		LOG_ERR("Agent init failed: %d", rc);
		return rc;
	}

	/* Print banner */
	print_banner();

	/* Log persisted summary if available */
	summary = memory_get_summary();
	if (summary != NULL) {
		LOG_DBG("Loaded prior context from NVS: %.80s...", summary);
		printk("  [Prior session context loaded from NVS]\n\n");
	}

	/* Idle — shell handles everything else */
	while (1) {
		k_sleep(K_SECONDS(30));

		if (g_wifi_connected) {
			LOG_DBG("Heartbeat — WiFi up");
		}
	}

	return 0;
}
