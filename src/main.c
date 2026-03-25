/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * zbot - Main Entry Point
 *
 * WiFi (boards with CONFIG_WIFI, e.g. nRF7002-DK):
 *   Use 'zbot wifi connect <ssid> [pass]' to connect and save credentials.
 *   On next boot, config_init() auto-connects using the saved credentials.
 *
 * Without WiFi (e.g. native_sim):
 *   The network stack is provided by the host OS; no WiFi management needed.
 *
 * API key:
 *   'zbot key <key>'  — set and persist to flash
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#if defined(CONFIG_WIFI)
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#endif

#include "config.h"
#include "memory.h"
#include "llm_client.h"
#include "agent.h"
#include "tools.h"
#include "telegram.h"

LOG_MODULE_REGISTER(zbot_main, LOG_LEVEL_INF);

#if defined(CONFIG_WIFI)
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
#endif /* CONFIG_WIFI */

static void print_banner(void)
{
	printk("\n");
	printk("╔══════════════════════════════════════════════╗\n");
	printk("║        zbot - Embedded AI Agent              ║\n");
	printk("║   Board: %-36s║\n", CONFIG_BOARD "  |  Zephyr RTOS");
	printk("║   Version: 0.1.0     |  License: Apache-2.0  ║\n");
	printk("╚══════════════════════════════════════════════╝\n");
	printk("\n");
	printk("Quick start:\n");
#if defined(CONFIG_WIFI)
	printk("  1. Connect to WiFi (saves credentials for auto-connect):\n");
	printk("       zbot wifi connect <SSID> <password>\n");
	printk("  2. Set API key (saved to flash):\n");
	printk("       zbot key sk-...\n");
#else
	printk("  1. Set API key (saved to flash):\n");
	printk("       zbot key sk-...\n");
#endif
	printk("  2. [Optional] Change model/endpoint:\n");
	printk("       zbot model gpt-4o-mini\n");
	printk("       zbot host openrouter.ai\n");
	printk("  3. Chat:\n");
	printk("       Hello! What can you do?\n");
	printk("  4. Telegram bot (optional):\n");
	printk("       zbot telegram token <BOT_TOKEN>\n");
	printk("       zbot telegram start\n");
	printk("  5. Other commands:\n");
	printk("       zbot status              -- show config\n");
#if defined(CONFIG_WIFI)
	printk("       zbot wifi status         -- show saved WiFi SSID\n");
#endif
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

#if defined(CONFIG_WIFI)
	/* Register WiFi event callback */
	net_mgmt_init_event_callback(&g_wifi_cb, wifi_event_handler, WIFI_MGMT_EVENTS);
	net_mgmt_add_event_callback(&g_wifi_cb);
#endif

	/* Config — load API key + (if WiFi board) credentials from NVS; auto-connect */
	config_init();

	/* LLM client — register TLS CA certificate once at boot */
	llm_client_init();

	/* Agent */
	rc = agent_init();
	if (rc < 0) {
		LOG_ERR("Agent init failed: %d", rc);
		return rc;
	}

	/* Telegram */
	if (config_has_tg_token()) {
		rc = telegram_start();
		if (rc < 0) {
			LOG_WRN("Telegram auto-start failed: %d", rc);
		}
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

#if defined(CONFIG_WIFI)
		if (g_wifi_connected) {
			LOG_DBG("Heartbeat — WiFi up");
		}
#endif
	}

	return 0;
}
