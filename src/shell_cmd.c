/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZBot - Shell Commands
 *
 * All user-facing commands are under the "zbot" root command.
 *
 * Usage:
 *   zbot key <api-key>                -- Set API key (persisted to flash)
 *   zbot key_delete                   -- Delete API key from flash
 *   zbot config_reset                 -- Reset all config to defaults and wipe from flash
 *   zbot host <hostname>              -- Set LLM endpoint host
 *   zbot path <path>                  -- Set LLM API path
 *   zbot model <model>                -- Set model name
 *   zbot provider <id>                -- Set X-Model-Provider-Id header
 *   zbot tls <on|off> [port]          -- Configure TLS
 *   zbot tls_verify <on|off>          -- Enable/disable TLS peer certificate verification
 *   zbot status                       -- Show current config status
 *   zbot history                      -- Show conversation history
 *   zbot summary                      -- Show persisted NVS summary
 *   zbot clear                        -- Clear conversation history
 *   zbot wipe                         -- Wipe history + NVS summary
 *   zbot skill list                   -- List all registered skills
 *   zbot skill run <name> [arg]       -- Run a skill directly
 *   zbot tools                        -- List all available tools
 *
 * WiFi commands (only available when CONFIG_WIFI is enabled, e.g. nRF7002-DK):
 *   zbot wifi connect <ssid> [pass]   -- Connect to WiFi (saves credentials)
 *   zbot wifi disconnect              -- Disconnect from WiFi
 *   zbot wifi status                  -- Show saved SSID and connection state
 *
 * Interactive chat mode (zbot chat with no arguments):
 *   Enters a dedicated prompt "zbot:~$ " where every line of input is sent
 *   directly to the agent.  Type /exit to return to the normal shell.
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#if defined(CONFIG_WIFI)
#include <zephyr/net/wifi_credentials.h>
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "memory.h"
#include "agent.h"
#include "tools.h"
#include "skill.h"

LOG_MODULE_REGISTER(zbot_shell, LOG_LEVEL_INF);

/* ------------------------------------------------------------------ */
/* zbot key                                                           */
/* ------------------------------------------------------------------ */

static int cmd_key(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	if (argc < 2) {
		shell_print(sh, "Usage: zbot key <api-key>");
		return -EINVAL;
	}

	rc = config_set_api_key(argv[1]);
	if (rc == 0) {
		shell_print(sh, "API key set (%zu chars) and saved to flash.", strlen(argv[1]));
	} else {
		shell_error(sh, "Failed to set key: %d", rc);
	}
	return rc;
}

/* ------------------------------------------------------------------ */
/* zbot key_delete — remove API key from NVS flash                    */
/* ------------------------------------------------------------------ */

static int cmd_key_delete(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = config_delete_api_key();
	shell_print(sh, rc == 0 ? "API key deleted from flash." : "No key stored.");
	return 0;
}

/* ------------------------------------------------------------------ */
/* zbot config_reset                                                  */
/* ------------------------------------------------------------------ */

static int cmd_config_reset(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = config_reset();
	if (rc == 0) {
		shell_print(sh, "All config reset to defaults and wiped from flash.");
	} else {
		shell_error(sh, "Config reset completed with errors: %d", rc);
	}
	return rc;
}

/* ------------------------------------------------------------------ */
/* zbot host                                                          */
/* ------------------------------------------------------------------ */

static int cmd_host(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	if (argc < 2) {
		shell_print(sh, "Usage: zbot host <hostname>");
		shell_print(sh, "Example: zbot host openrouter.ai");
		return -EINVAL;
	}

	rc = config_set_endpoint_host(argv[1]);
	if (rc == 0) {
		shell_print(sh, "Host set: %s", argv[1]);
	} else {
		shell_error(sh, "Failed: %d", rc);
	}
	return rc;
}

/* ------------------------------------------------------------------ */
/* zbot path                                                          */
/* ------------------------------------------------------------------ */

static int cmd_path(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	if (argc < 2) {
		shell_print(sh, "Usage: zbot path <path>");
		shell_print(sh, "Example: zbot path /api/v1/chat/completions");
		return -EINVAL;
	}

	rc = config_set_endpoint_path(argv[1]);
	if (rc == 0) {
		shell_print(sh, "Path set: %s", argv[1]);
	} else {
		shell_error(sh, "Failed: %d", rc);
	}
	return rc;
}

/* ------------------------------------------------------------------ */
/* zbot model                                                         */
/* ------------------------------------------------------------------ */

static int cmd_model(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	if (argc < 2) {
		shell_print(sh, "Usage: zbot model <model-name>");
		shell_print(sh, "Example: zbot model gpt-4o-mini");
		return -EINVAL;
	}

	rc = config_set_model(argv[1]);
	if (rc == 0) {
		shell_print(sh, "Model set: %s", argv[1]);
	} else {
		shell_error(sh, "Failed: %d", rc);
	}
	return rc;
}

/* ------------------------------------------------------------------ */
/* zbot provider                                                      */
/* ------------------------------------------------------------------ */

static int cmd_provider(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	if (argc < 2) {
		shell_print(sh, "Usage: zbot provider <provider-id>");
		shell_print(sh, "Example: zbot provider azure_openai");
		shell_print(sh, "Sets the X-Model-Provider-Id request header.");
		return -EINVAL;
	}

	rc = config_set_provider_id(argv[1]);
	if (rc == 0) {
		shell_print(sh, "Provider ID set: %s", argv[1]);
	} else {
		shell_error(sh, "Failed: %d", rc);
	}
	return rc;
}

/* ------------------------------------------------------------------ */
/* zbot tls                                                           */
/* ------------------------------------------------------------------ */

static int cmd_tls(const struct shell *sh, size_t argc, char **argv)
{
	bool use_tls;
	uint16_t port;
	int rc;

	if (argc < 2) {
		shell_print(sh, "Usage: zbot tls <on|off> [port]");
		return -EINVAL;
	}

	use_tls = (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "1") == 0 ||
		   strcmp(argv[1], "yes") == 0);

	port = use_tls ? 443 : 80;
	if (argc >= 3) {
		port = (uint16_t)strtoul(argv[2], NULL, 10);
	}

	rc = config_set_tls(use_tls, port);
	if (rc == 0) {
		shell_print(sh, "TLS: %s, port: %u (saved to flash)", use_tls ? "on" : "off",
			    port);
	} else {
		shell_error(sh, "Failed to persist TLS config: %d", rc);
	}
	return rc;
}

/* ------------------------------------------------------------------ */
/* zbot tls_verify                                                    */
/* ------------------------------------------------------------------ */

static int cmd_tls_verify(const struct shell *sh, size_t argc, char **argv)
{
	bool verify;
	int rc;

	if (argc < 2) {
		shell_print(sh, "Usage: zbot tls_verify <on|off>");
		return -EINVAL;
	}

	verify = (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "1") == 0 ||
		  strcmp(argv[1], "yes") == 0);

	rc = config_set_tls_verify(verify);
	if (rc == 0) {
		shell_print(sh, "TLS peer verify: %s (saved to flash)", verify ? "on" : "off");
	} else {
		shell_error(sh, "Failed to persist tls_verify: %d", rc);
	}
	return rc;
}

/* ------------------------------------------------------------------ */
/* zbot status                                                        */
/* ------------------------------------------------------------------ */

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	config_print_status();
	shell_print(sh, "Agent busy: %s", agent_is_busy() ? "yes" : "no");
	return 0;
}

/* ------------------------------------------------------------------ */
/* zbot chat — runs agent on system workqueue to avoid shell stack OVF */
/* ------------------------------------------------------------------ */

struct chat_work_item {
	int rc;
	char input[AGENT_INPUT_MAX_LEN];
	char response[AGENT_OUTPUT_MAX_LEN];
	struct k_sem done;
};

static struct chat_work_item g_chat_work;

static void agent_rsp_cb(int err, struct agent_response_ctx *ctx)
{
	struct chat_work_item *chat = ctx->user_data;

	chat->rc = err;
	k_sem_give(&chat->done);
}

/* Send one message and print the response.  Returns the agent error code. */
static int chat_send(const struct shell *sh, const char *msg)
{
	struct agent_response_ctx ctx = {
		.output        = g_chat_work.response,
		.output_length = sizeof(g_chat_work.response),
		.cb            = agent_rsp_cb,
		.user_data     = &g_chat_work,
	};
	int rc;

	strncpy(g_chat_work.input, msg, sizeof(g_chat_work.input) - 1);
	g_chat_work.input[sizeof(g_chat_work.input) - 1] = '\0';

	shell_print(sh, "Thinking...");

	k_sem_init(&g_chat_work.done, 0, 1);

	rc = agent_submit_input(g_chat_work.input, &ctx);
	if (rc) {
		shell_error(sh, "Agent submit input error");
		return -EACCES;
	}

	if (k_sem_take(&g_chat_work.done, K_SECONDS(60)) != 0) {
		shell_error(sh, "Timeout waiting for agent response");
		return -ETIMEDOUT;
	}

	if (g_chat_work.rc < 0) {
		shell_error(sh, "Agent error: %d", g_chat_work.rc);
		if (g_chat_work.response[0] != '\0') {
			shell_print(sh, "%s", g_chat_work.response);
		}
	} else {
		shell_print(sh, "\nzbot: %s\n", g_chat_work.response);
	}

	return g_chat_work.rc;
}

/* ------------------------------------------------------------------ */
/* Interactive chat mode — shell bypass                               */
/* ------------------------------------------------------------------ */

/* State shared between the bypass handler and cmd_chat */
static const struct shell *g_chat_shell;

/* Line accumulation buffer for the bypass handler */
#define CHAT_LINE_MAX 256
static char g_chat_line[CHAT_LINE_MAX];
static size_t g_chat_line_pos;

static size_t utf8_char_len(const char *buf, size_t pos)
{
	size_t len = 0;

	/* Step back over continuation bytes */
	while (pos > 0 && len < 4 && ((uint8_t)buf[pos - 1] & 0xC0) == 0x80) {
		pos--;
		len++;
	}

	return len + 1;
}

static void chat_bypass_handler(const struct shell *sh, uint8_t *data,
				size_t len, void *args)
{
	for (size_t i = 0; i < len; i++) {
		uint8_t c = data[i];

		if (c == '\r' || c == '\n') {
			shell_fprintf(sh, SHELL_NORMAL, "\r\n");
			g_chat_line[g_chat_line_pos] = '\0';

			if (strcmp(g_chat_line, "/exit") == 0) {
				shell_print(sh, "Leaving chat mode.");
				shell_set_bypass(sh, NULL, NULL);
				g_chat_shell = NULL;
				g_chat_line_pos = 0;
				return;
			}

			if (g_chat_line_pos > 0) {
				chat_send(sh, g_chat_line);
			}

			/* Print prompt for the next line */
			shell_fprintf(sh, SHELL_INFO, "zbot:~$ ");
			g_chat_line_pos = 0;
		} else if (c == '\b' || c == 0x7f) {
			/*
			 * Backspace: remove the previous UTF-8 character.
			 * Erase as many terminal columns as the character
			 * occupies bytes (conservative; works for ASCII and
			 * common CJK wide chars displayed as 2 columns on
			 * most terminals when byte count == 3).
			 */
			if (g_chat_line_pos > 0) {
				size_t char_len =
					utf8_char_len(g_chat_line, g_chat_line_pos);

				g_chat_line_pos -= char_len;

				if (char_len != 1) {
					shell_fprintf(sh, SHELL_NORMAL, "\b \b");
				}

				shell_fprintf(sh, SHELL_NORMAL, "\b \b");
			}
		} else if (c >= 0x20 && c < 0x7f) {
			/* Printable ASCII */
			if (g_chat_line_pos < CHAT_LINE_MAX - 1) {
				g_chat_line[g_chat_line_pos++] = (char)c;
				shell_fprintf(sh, SHELL_NORMAL, "%c", c);
			}
		} else if (c >= 0x80) {
			/*
			 * UTF-8 multi-byte continuation or leading byte.
			 * Accumulate the raw bytes; the terminal already
			 * echoes them correctly once the full sequence is sent.
			 */
			if (g_chat_line_pos < CHAT_LINE_MAX - 1) {
				g_chat_line[g_chat_line_pos++] = (char)c;
				shell_fprintf(sh, SHELL_NORMAL, "%c", c);
			}
		}
		/* Ignore other control characters */
	}
}

static int cmd_chat(const struct shell *sh, size_t argc, char **argv)
{
	/* No arguments → enter interactive chat mode */
	if (!config_has_api_key()) {
		shell_error(sh, "API key not set. Run: zbot key <your-api-key>");
		return -EACCES;
	}

	shell_print(sh, "Entering interactive chat mode. Type /exit to quit.");
	shell_fprintf(sh, SHELL_INFO, "zbot:~$ ");

	g_chat_shell = sh;
	g_chat_line_pos = 0;
	shell_set_bypass(sh, chat_bypass_handler, NULL);
	return 0;
}

/* ------------------------------------------------------------------ */
/* zbot history                                                       */
/* ------------------------------------------------------------------ */

static int cmd_history(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	memory_dump();
	return 0;
}

/* ------------------------------------------------------------------ */
/* zbot summary                                                       */
/* ------------------------------------------------------------------ */

static int cmd_summary(const struct shell *sh, size_t argc, char **argv)
{
	const char *s;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	s = memory_get_summary();
	shell_print(sh, "NVS Summary: %s", s ? s : "(none)");
	return 0;
}

/* ------------------------------------------------------------------ */
/* zbot clear                                                         */
/* ------------------------------------------------------------------ */

static int cmd_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	memory_clear_history();
	shell_print(sh, "In-RAM history cleared. NVS summary preserved.");
	return 0;
}

/* ------------------------------------------------------------------ */
/* zbot wipe                                                          */
/* ------------------------------------------------------------------ */

static int cmd_wipe(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	memory_wipe_all();
	shell_print(sh, "All history and NVS summary wiped.");
	return 0;
}

/* ------------------------------------------------------------------ */
/* zbot skill list / run                                              */
/* ------------------------------------------------------------------ */

static int cmd_skill_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	skill_list();
	return 0;
}

static int cmd_skill_run(const struct shell *sh, size_t argc, char **argv)
{
	static char result[SKILL_RESULT_MAX_LEN];
	const char *arg;
	int rc;

	if (argc < 2) {
		shell_print(sh, "Usage: zbot skill run <name> [arg]");
		return -EINVAL;
	}

	arg = argc >= 3 ? argv[2] : NULL;
	rc = skill_run(argv[1], arg, result, sizeof(result));
	shell_print(sh, "%s", result);
	return rc;
}

/* ------------------------------------------------------------------ */
/* zbot tools                                                         */
/* ------------------------------------------------------------------ */

static int cmd_tools(const struct shell *sh, size_t argc, char **argv)
{
	const struct tool_descriptor *tools;
	int count;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	tools = tools_get_all(&count);

	shell_print(sh, "=== zbot Tools (%d) ===", count);
	for (int i = 0; i < count; i++) {
		shell_print(sh, "  %-20s %s", tools[i].name, tools[i].description);
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/* zbot wifi connect / disconnect / status                            */
/* Only compiled when CONFIG_WIFI is enabled (e.g. nRF7002-DK).       */
/* ------------------------------------------------------------------ */

#if defined(CONFIG_WIFI)
static int cmd_wifi_connect(const struct shell *sh, size_t argc, char **argv)
{
	const char *pass;
	int rc;

	if (argc < 2) {
		shell_print(sh, "Usage: zbot wifi connect <ssid> [passphrase]");
		shell_print(sh, "Credentials are saved to flash for auto-connect on next boot.");
		return -EINVAL;
	}

	pass = argc >= 3 ? argv[2] : "";
	rc = config_wifi_connect(argv[1], pass);
	if (rc == 0) {
		shell_print(sh, "WiFi connect request sent to SSID: %s", argv[1]);
		shell_print(sh, "Credentials saved — will auto-connect on reboot.");
	} else {
		shell_error(sh, "WiFi connect failed: %d", rc);
	}
	return rc;
}

static int cmd_wifi_disconnect(const struct shell *sh, size_t argc, char **argv)
{
	int rc;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	rc = config_wifi_disconnect();
	if (rc == 0) {
		shell_print(sh, "WiFi disconnect requested.");
	} else {
		shell_error(sh, "WiFi disconnect failed: %d", rc);
	}
	return rc;
}

static int cmd_wifi_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (wifi_credentials_is_empty()) {
		shell_print(sh, "No WiFi credentials saved.");
		shell_print(sh, "Use: zbot wifi connect <ssid> [pass]");
	} else {
		shell_print(sh, "WiFi credentials saved (use 'zbot status' for details).");
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_wifi,
	SHELL_CMD_ARG(connect, NULL, "Connect: wifi connect <ssid> [pass]", cmd_wifi_connect, 2, 1),
	SHELL_CMD(disconnect, NULL, "Disconnect from current WiFi", cmd_wifi_disconnect),
	SHELL_CMD(status, NULL, "Show saved WiFi SSID", cmd_wifi_status),
	SHELL_SUBCMD_SET_END);
#endif /* CONFIG_WIFI */

SHELL_STATIC_SUBCMD_SET_CREATE(sub_skill,
	SHELL_CMD(list, NULL, "List all skills", cmd_skill_list),
	SHELL_CMD_ARG(run, NULL, "Run a skill: run <name> [arg]", cmd_skill_run, 2, 1),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_zbot,
	SHELL_CMD_ARG(key, NULL, "Set API key: key <key>", cmd_key, 2, 0),
	SHELL_CMD(key_delete, NULL, "Delete API key from flash", cmd_key_delete),
	SHELL_CMD(config_reset, NULL, "Reset all config to defaults and wipe from flash",
		  cmd_config_reset),
	SHELL_CMD_ARG(host, NULL, "Set LLM host: host <hostname>", cmd_host, 2, 0),
	SHELL_CMD_ARG(path, NULL, "Set API path: path <path>", cmd_path, 2, 0),
	SHELL_CMD_ARG(model, NULL, "Set model: model <name>", cmd_model, 2, 0),
	SHELL_CMD_ARG(provider, NULL, "Set X-Model-Provider-Id: provider <id>", cmd_provider, 2, 0),
	SHELL_CMD_ARG(tls, NULL, "Configure TLS: tls <on|off> [port]", cmd_tls, 2, 1),
	SHELL_CMD_ARG(tls_verify, NULL, "TLS peer verify: tls_verify <on|off>", cmd_tls_verify, 2,
		      0),
	SHELL_CMD(status, NULL, "Show current configuration", cmd_status),
#if defined(CONFIG_WIFI)
	SHELL_CMD(wifi, &sub_wifi, "WiFi commands", NULL),
#endif
	SHELL_CMD_ARG(chat, NULL, "Chat: chat", cmd_chat, 1, 0),
	SHELL_CMD(history, NULL, "Show conversation history", cmd_history),
	SHELL_CMD(summary, NULL, "Show NVS persisted summary", cmd_summary),
	SHELL_CMD(clear, NULL, "Clear in-RAM conversation history", cmd_clear),
	SHELL_CMD(wipe, NULL, "Wipe all history and NVS summary", cmd_wipe),
	SHELL_CMD(skill, &sub_skill, "Skill commands", NULL),
	SHELL_CMD(tools, NULL, "List available tools", cmd_tools),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(zbot, &sub_zbot, "zbot AI Agent commands", NULL);
