# ZBot

An open-source embedded AI agent running on the **Nordic nRF7002-DK** development board, powered by **Zephyr RTOS**. ZBot implements a ReAct (Reason + Act) loop that connects to any OpenAI-compatible LLM API over WiFi and can control hardware, maintain conversation memory across reboots, and run multi-step skills.

![demo](docs/output.gif)

**Target board:** nRF7002-DK (nRF5340 + nRF7002 WiFi)
**RTOS:** [Zephyr](https://zephyrproject.org) ≥ latest
**License:** Apache-2.0

---

## Architecture

```
┌──────────────────────────────────────────────────────┐
│                    ZBot Agent                        │
│                                                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐    │
│  │  Config  │  │  Memory  │  │   LLM Client     │    │
│  │ endpoint │  │ K_FIFO   │  │  HTTP → OpenAI   │    │
│  │ model    │  │ pool+    │  │  compatible API  │    │
│  │ api key  │  │ settings │  │                  │    │
│  └──────────┘  └──────────┘  └──────────────────┘    │
│                                                      │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐    │
│  │  Tools   │  │  Skills  │  │  Agent (ReAct)   │    │
│  │ gpio_read│  │ blink    │  │  Reason→Act loop │    │
│  │ gpio_writ│  │ sos      │  │  tool calling    │    │
│  │ get_heap │  │ status   │  │  auto-summarise  │    │
│  │ board_inf│  │ clear_mem│  │                  │    │
│  └──────────┘  └──────────┘  └──────────────────┘    │
│                                                      │
│  ┌────────────────────────────────────────────────┐  │
│  │  Shell Commands  (zbot key / chat / skill ...) │  │
│  └────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────┘
```

### Modules

| Module | File | Purpose |
|--------|------|---------|
| **Config** | `config.h/c` | LLM endpoint, model, API key + WiFi credentials (settings persistence) |
| **Memory** | `memory.h/c` | k_mem_slab conversation history + settings-persisted rolling summary |
| **LLM Client** | `llm_client.h/c` | HTTPS POST to OpenAI-compatible Chat Completions API |
| **Tools** | `tools.h/c` | Atomic hardware actions: GPIO, uptime, heap, board info |
| **Skills** | `skill.h/c` | Multi-step reusable workflows (blink, SOS, status, clear) |
| **Agent** | `agent.h/c` | ReAct loop: reason → call tool → observe → repeat → summarise |
| **Shell** | `shell_cmd.c` | All `zbot` shell subcommands |

### ReAct Loop

```
user input
    │
    ▼
build messages JSON  ←────────────────────────────────────────────────┐
    │                                                                 │
    ▼                                                                 │
LLM API call (tools enabled)                                          │
    │                                                                 |
    ├── finish_reason: tool_call ──► execute tool ──► append result ──┘
    │
    └── finish_reason: stop ──► return answer to user
                                        │
                                        ▼
                                request summary ──► NVS
```

Max iterations per turn: **10**

### Conversation Memory

History uses a **10-node static pool** (`k_mem_slab`) backed by a `sys_slist_t` linked list ordered oldest → newest.

When `k_mem_slab_alloc` fails on `memory_add_turn()`:
1. **Compress** — the oldest nodes are summarised by the LLM; the source nodes are freed back to the slab.
2. **Evict** *(fallback if compression fails)* — the oldest history node is recycled directly.

After each compression, the rolling summary is written to NVS and injected as prior context on the next boot.

**Settings layout:**

| Settings key | Type | Notes |
|---|---|---|
| `zbot/summary` | `char[768]` | Conversation summary (managed by memory.c) |
| `zbot/apikey` | `char[256]` | Saved automatically when `zbot key` is run |
| `zbot/host` | `char[128]` | LLM endpoint hostname |
| `zbot/path` | `char[128]` | LLM API path |
| `zbot/model` | `char[128]` | Model name |
| `zbot/provider_id` | `char[64]` | `X-Model-Provider-Id` header value |
| `zbot/use_tls` | `uint8_t` | TLS enabled flag |
| `zbot/tls_verify` | `uint8_t` | TLS peer certificate verification flag (default: on) |
| `zbot/port` | `uint16_t` | TCP port |
| `wifi/...` | — | WiFi credentials managed by Zephyr `wifi_credentials` subsystem |

All configuration fields are persisted to NVS automatically when set.
WiFi credentials are stored by `CONFIG_WIFI_CREDENTIALS` (separate from the `zbot` subtree).

---

## Prerequisites

Set up a Zephyr development environment following the official guide:
https://docs.zephyrproject.org/latest/develop/getting_started/index.html

The latest Zephyr version is recommended. Once your environment is ready, clone zbot into your workspace and proceed below.

---

## Quick Start

### 1. Build & Flash

From the Zephyr workspace root:

```bash
west build -b nrf7002dk/nrf5340/cpuapp zbot
west flash
```

### 2. Connect Serial

```bash
minicom -D /dev/ttyACM0 -b 115200
# or
screen /dev/ttyACM0 115200
```

### 3. Connect to WiFi

```
uart:~$ zbot wifi connect <SSID> <password>
```

Credentials are saved to flash. On the next reboot, the board auto-connects without any manual command.

### 4. Set API Key

```
uart:~$ zbot key sk-...
```

The key is saved to NVS flash immediately and restored on every reboot.

### 5. (Optional) Configure Endpoint

Default endpoint is `openrouter.ai:443`. To use OpenAI directly:

```
uart:~$ zbot host openrouter.ai
uart:~$ zbot path /api/v1/chat/completions
uart:~$ zbot model minimax/minimax-m2.5
uart:~$ zbot tls on 443
```

For DeepSeek:

```
uart:~$ zbot tls_verify off
uart:~$ zbot host api.deepseek.com
uart:~$ zbot path /chat/completions
uart:~$ zbot model deepseek-chat
uart:~$ zbot tls off
uart:~$ zbot key sk-...
uart:~$ zbot chat Hi
```

For a local model (e.g. Ollama):

```
uart:~$ zbot host 192.168.1.100
uart:~$ zbot tls off 11434
```

### 6. Chat

One-shot message:

```
uart:~$ zbot chat Hello! What can you do?
uart:~$ zbot chat Turn on LED0
uart:~$ zbot chat What is the board uptime?
```

Interactive chat mode (no arguments):

```
uart:~$ zbot chat
Entering interactive chat mode. Type /exit to quit.
zbot:~$ Hello! What can you do?
Thinking...

zbot: Hi! I'm zbot ...

zbot:~$ Turn on LED0
Thinking...

zbot: Done — LED0 is now on.

zbot:~$ /exit
Leaving chat mode.
uart:~$
```

---

## Shell Commands

All commands are subcommands of `zbot`.

### Configuration

| Command | Description |
|---------|-------------|
| `zbot key <key>` | Set API key (saved to flash automatically) |
| `zbot key-delete` | Delete API key from flash |
| `zbot config_reset` | Reset all config to defaults and wipe from flash |
| `zbot host <hostname>` | Set LLM endpoint host (saved to flash) |
| `zbot path <path>` | Set LLM API path (saved to flash) |
| `zbot model <name>` | Set model name (saved to flash) |
| `zbot provider <id>` | Set `X-Model-Provider-Id` header (saved to flash) |
| `zbot tls <on\|off> [port]` | Enable/disable TLS and set port (saved to flash) |
| `zbot tls_verify <on\|off>` | Enable/disable TLS peer certificate verification (saved to flash) |
| `zbot status` | Show current config and agent state |

### WiFi

| Command | Description |
|---------|-------------|
| `zbot wifi connect <ssid> [pass]` | Connect to WiFi and save credentials to flash |
| `zbot wifi disconnect` | Disconnect from current WiFi network |
| `zbot wifi status` | Show saved SSID |

### Conversation

| Command | Description |
|---------|-------------|
| `zbot chat <message>` | Send a single message to the agent |
| `zbot chat` | Enter interactive chat mode (`zbot:~$ ` prompt; `/exit` to quit) |
| `zbot history` | Print in-RAM conversation history |
| `zbot summary` | Show the NVS-persisted session summary |
| `zbot clear` | Clear RAM history (keeps NVS summary) |
| `zbot wipe` | Wipe all history and NVS summary |

### Skills & Tools

| Command | Description |
|---------|-------------|
| `zbot skill list` | List all registered skills |
| `zbot skill run <name> [arg]` | Run a skill directly |
| `zbot tools` | List all available tools with descriptions |

---

## Built-in Tools

Tools are invoked automatically by the agent during the ReAct loop.

| Tool | Parameters | Description |
|------|-----------|-------------|
| `gpio_read` | `pin`: `led0` / `led1` / `button0` | Read GPIO pin level |
| `gpio_write` | `pin`: `led0` / `led1`, `value`: `0` / `1` | Set GPIO output level |
| `get_uptime` | — | Device uptime in ms and seconds |
| `get_board_info` | — | Board model, SoC, WiFi chip, RTOS, version |
| `get_heap_info` | — | Heap free / allocated / max-used bytes |
| `echo` | `message`: string | Echo a message (for testing) |

---

## Built-in Skills

Skills are multi-step workflows invocable by name.

| Skill | Argument | Description |
|-------|----------|-------------|
| `blink_led` | Count (1–20, default 3) | Blink LED0 N times at 300 ms intervals |
| `sos` | — | Blink SOS morse code pattern on LED0 |
| `system_status` | — | Report uptime, heap usage, and board info |
| `clear_memory` | — | Wipe conversation history and NVS summary |

```
uart:~$ zbot skill run blink_led 5
uart:~$ zbot skill run sos
uart:~$ zbot skill run system_status
```

---

## Extending zbot

### Adding a Custom Tool

Add a handler and entry to `g_tools[]` in `tools.c`:

```c
int tool_my_sensor(const char *args_json, char *result, size_t res_len)
{
    snprintf(result, res_len, "{\"temp_c\":25}");
    return 0;
}

// in g_tools[]:
{
    .name = "read_temperature",
    .description = "Read the ambient temperature sensor.",
    .parameters_json_schema = "{\"type\":\"object\",\"properties\":{}}",
    .handler = tool_my_sensor,
},
```

The agent will include the new tool in every LLM request automatically.

### Adding a Custom Skill

```c
static int my_skill(const char *arg, char *result, size_t res_len)
{
    snprintf(result, res_len, "done");
    return 0;
}

// in main.c or skill.c, before agent_init():
skill_register("my_skill", "Description of what it does", my_skill);
```

---

## Security Notes

- **API key** is saved to NVS flash immediately when set via `zbot key`. Use `zbot key-delete` to remove it. Anyone with physical flash access can read the raw NVS partition.
- **WiFi passphrase** is stored in flash as plain text when you use `zbot wifi connect`. Anyone with physical flash access can read it. Acceptable for dev boards; use additional protections in production.
- **TLS peer verification** is enabled by default (`tls_verify on`). Disable it with `zbot tls_verify off` only for development/debugging against servers with self-signed certificates.
- **NVS summary** is stored as plain text. Avoid sensitive information in conversations if flash readout is a concern.

---

## Project Structure

```
zbot/
├── CMakeLists.txt
├── prj.conf                              # Zephyr Kconfig (all targets)
├── sysbuild.conf
└── src/
    ├── main.c          # Boot sequence, WiFi events, banner
    ├── config.h/c      # LLM runtime config + settings persistence
    ├── memory.h/c      # K_FIFO history pool + settings summary
    ├── llm_client.h/c  # HTTPS Chat Completions client
    ├── tools.h/c       # Hardware tool primitives
    ├── skill.h/c       # Multi-step skill framework
    ├── agent.h/c       # ReAct reasoning loop + system prompt
    └── shell_cmd.c     # `zbot` shell command tree
```
