# gpio

Unified GPIO skill. All LED and button operations go through this skill.
tools: tool_exec({"tool":"gpio","args":{...}})

## Actions

### read
Read the current level of a GPIO pin.
- `pin`: `led0`, `led1`, or `button0`

Example: `{"tool":"gpio","args":{"action":"read","pin":"led0"}}`

### write
Set the output level of a GPIO pin.
- `pin`: `led0` or `led1`
- `value`: `0` (off) or `1` (on)

Example: `{"tool":"gpio","args":{"action":"write","pin":"led0","value":1}}`

### blink
Blink LED0 N times with a 300ms on/off cycle.
- `count` (optional): number of blinks, 1–20, default 3

Example: `{"tool":"gpio","args":{"action":"blink","count":5}}`

### sos
Transmit SOS morse code (`... --- ...`) on LED0.
No additional arguments required.

Example: `{"tool":"gpio","args":{"action":"sos"}}`
