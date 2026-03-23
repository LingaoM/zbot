# system

Unified system information skill.
tools: tool_exec({"tool": "system", "args": {...}})

## Actions

### board_info
Return board name, RTOS, and firmware version.

Example: `{"tool":"system","args":{"action":"board_info"}}`

### uptime
Return device uptime in milliseconds and seconds.

Example: `{"action":"uptime"}`

### heap_info
Return heap memory statistics: free, allocated, and peak bytes.
Requires `CONFIG_SYS_HEAP_RUNTIME_STATS=y` for full stats.

Example: `{"tool":"system","args":{"action":"heap_info"}}`

### status
Return a combined report: board info + uptime + heap in one response.

Example: `{"tool":"system","args":{"action":"status"}}`
