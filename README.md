# ZephyrClaw

An open-source embedded AI agent running on the **Nordic nRF7002-DK** development board, powered by **Zephyr RTOS**. ZephyrClaw implements a ReAct (Reason + Act) loop that connects to any OpenAI-compatible LLM API over WiFi and can control hardware, maintain conversation memory across reboots, and run multi-step skills.

**Target board:** nRF7002-DK (nRF5340 + nRF7002 WiFi)
**RTOS:** [Zephyr](https://zephyrproject.org) ≥ 3.6
**License:** Apache-2.0

---

## Architecture

```
┌──────────────────────────────────────────────────────┐
│                    ZephyrClaw Agent                   │
│                                                       │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐   │
│  │  Config  │  │  Memory  │  │   LLM Client      │   │
│  │ endpoint │  │ K_FIFO   │  │  HTTP → OpenAI    │   │
│  │ model    │  │ pool+    │  │  compatible API   │   │
│  │ api key  │  │ settings │  │                   │   │
│  └──────────┘  └──────────┘  └──────────────────┘   │
│                                                       │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────┐   │
│  │  Tools   │  │  Skills  │  │  Agent (ReAct)    │   │
│  │ gpio_read│  │ blink    │  │  Reason→Act loop  │   │
│  │ gpio_writ│  │ sos      │  │  tool calling     │   │
│  │ get_heap │  │ status   │  │  auto-summarise   │   │
│  │ board_inf│  │ clear_mem│  │                   │   │
│  └──────────┘  └──────────┘  └──────────────────┘   │
│                                                       │
│  ┌────────────────────────────────────────────────┐  │
│  │  Shell Commands  (claw key / chat / skill ...)  │  │
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
| **Shell** | `shell_cmd.c` | All `claw` shell subcommands |

### ReAct Loop

```
user input
    │
    ▼
build messages JSON  ←──────────────────────────────┐
    │                                                │
    ▼                                                │
LLM API call (tools enabled)                        │
    │                                                │
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

**Settings layout (subtree "zc"):**

| Settings key | Type | Notes |
|---|---|---|
| `zc/summary` | `char[768]` | Conversation summary (managed by memory.c) |
| `zc/apikey` | `char[256]` | Optional — only saved via `claw key-save` |
| `zc/wifi/ssid` | `char[33]` | Saved by `claw wifi connect` |
| `zc/wifi/pass` | `char[65]` | Saved by `claw wifi connect` |

The API key is RAM-only by default; use `claw key-save` to optionally persist it.

---

## Prerequisites

Set up a Zephyr development environment following the official guide:
https://docs.zephyrproject.org/latest/develop/getting_started/index.html

The latest Zephyr version is recommended. Once your environment is ready, clone ZephyrClaw into your workspace and proceed below.

---

## Quick Start

### 1. Build & Flash

From the Zephyr workspace root:

```bash
west build -b nrf7002dk/nrf5340/cpuapp zephyrclaw
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
uart:~$ claw wifi connect <SSID> <password>
```

Credentials are saved to flash. On the next reboot, the board auto-connects without any manual command.

### 4. Set API Key

```
uart:~$ claw key sk-...
```

The key is held in RAM only and lost on reboot. Use `claw key-save` to persist it to flash.

### 5. (Optional) Configure Endpoint

Default endpoint is `api.openai.com:443`. To use OpenAI directly:

```
uart:~$ claw host api.openai.com
uart:~$ claw path /v1/chat/completions
uart:~$ claw model gpt-4o-mini
uart:~$ claw tls on 443
```

For a local model (e.g. Ollama):

```
uart:~$ claw host 192.168.1.100
uart:~$ claw tls off 11434
```

### 6. Chat

```
uart:~$ claw chat Hello! What can you do?
uart:~$ claw chat Turn on LED0
uart:~$ claw chat What is the board uptime?
```

---

## Shell Commands

All commands are subcommands of `claw`.

### Configuration

| Command | Description |
|---------|-------------|
| `claw key <key>` | Set API key (RAM only, not persisted) |
| `claw key-save` | Save current API key to flash |
| `claw key-delete` | Delete API key from flash |
| `claw host <hostname>` | Set LLM endpoint host |
| `claw path <path>` | Set LLM API path |
| `claw model <name>` | Set model name |
| `claw provider <id>` | Set `X-Model-Provider-Id` header |
| `claw tls <on\|off> [port]` | Enable/disable TLS and set port |
| `claw status` | Show current config and agent state |

### WiFi

| Command | Description |
|---------|-------------|
| `claw wifi connect <ssid> [pass]` | Connect to WiFi and save credentials to flash |
| `claw wifi disconnect` | Disconnect from current WiFi network |
| `claw wifi status` | Show saved SSID |

### Conversation

| Command | Description |
|---------|-------------|
| `claw chat <message>` | Send a message to the agent |
| `claw history` | Print in-RAM conversation history |
| `claw summary` | Show the NVS-persisted session summary |
| `claw clear` | Clear RAM history (keeps NVS summary) |
| `claw wipe` | Wipe all history and NVS summary |

### Skills & Tools

| Command | Description |
|---------|-------------|
| `claw skill list` | List all registered skills |
| `claw skill run <name> [arg]` | Run a skill directly |
| `claw tools` | List all available tools with descriptions |

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
uart:~$ claw skill run blink_led 5
uart:~$ claw skill run sos
uart:~$ claw skill run system_status
```

---

## Extending ZephyrClaw

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

- **API key** is RAM-only by default. It is cleared on power cycle and never written to flash unless you explicitly run `claw key-save`.
- **WiFi passphrase** is stored in flash as plain text when you use `claw wifi connect`. Anyone with physical flash access can read it. Acceptable for dev boards; use additional protections in production.
- **TLS peer verification** is set to `NONE` by default (development mode). For production, supply a CA certificate and change verify mode to `TLS_PEER_VERIFY_REQUIRED`.
- **NVS summary** is stored as plain text. Avoid sensitive information in conversations if flash readout is a concern.

---

## Project Structure

```
zephyrclaw/
├── CMakeLists.txt
├── prj.conf                              # Zephyr Kconfig (all targets)
├── sysbuild.conf
├── boards/
│   └── nrf7002dk_nrf5340_cpuapp.conf     # Board-specific Kconfig overrides
└── src/
    ├── main.c          # Boot sequence, WiFi events, banner
    ├── config.h/c      # LLM runtime config + settings persistence
    ├── memory.h/c      # K_FIFO history pool + settings summary
    ├── llm_client.h/c  # HTTPS Chat Completions client
    ├── tools.h/c       # Hardware tool primitives
    ├── skill.h/c       # Multi-step skill framework
    ├── agent.h/c       # ReAct reasoning loop + system prompt
    └── shell_cmd.c     # `claw` shell command tree
```
