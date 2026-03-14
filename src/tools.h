/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZBot - Tools Module
 *
 * Tools are the primitive actions the Agent can invoke. Each tool has:
 *   - name        : snake_case identifier used in LLM function calling
 *   - description : human/LLM-readable description
 *   - parameters  : JSON Schema string describing input parameters
 *   - handler     : C function that executes the tool
 *
 * The tools JSON schema is injected into the LLM request so the model
 * knows what it can call. Results are returned as JSON strings.
 */

#ifndef ZBOT_TOOLS_H
#define ZBOT_TOOLS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TOOLS_NAME_MAX_LEN   32
#define TOOLS_RESULT_MAX_LEN 256

/**
 * @brief Tool handler function signature.
 *
 * @param args_json  JSON object string with the tool's arguments.
 * @param result     Output buffer for the JSON result string.
 * @param result_len Size of the result buffer.
 * @return 0 on success, negative errno on error.
 */
typedef int (*tool_handler_fn)(const char *args_json, char *result, size_t result_len);

/** A single tool descriptor */
struct tool_descriptor {
	const char *name;
	const char *description;
	const char *parameters_json_schema; /* JSON Schema for args */
	tool_handler_fn handler;
};

/**
 * @brief Get the array of all registered tools.
 * @param count  Output: number of tools.
 * @return Pointer to the tool descriptor array.
 */
const struct tool_descriptor *tools_get_all(int *count);

/**
 * @brief Build the "tools" JSON array for an OpenAI-compatible request.
 *
 * @param buf      Output buffer.
 * @param buf_len  Buffer size.
 * @return Bytes written, or negative on error.
 */
int tools_build_json(char *buf, size_t buf_len);

/**
 * @brief Find a tool by name and execute it.
 *
 * @param name      Tool name string.
 * @param args_json JSON arguments string.
 * @param result    Output result buffer.
 * @param res_len   Size of result buffer.
 * @return 0 on success, -ENOENT if tool not found, other negative on error.
 */
int tools_execute(const char *name, const char *args_json, char *result, size_t res_len);

/* ------------------------------------------------------------------ */
/* Built-in tool handlers (also callable directly)                     */
/* ------------------------------------------------------------------ */

/** Read GPIO pin level */
int tool_gpio_read(const char *args_json, char *result, size_t res_len);

/** Write GPIO pin level */
int tool_gpio_write(const char *args_json, char *result, size_t res_len);

/** Get system uptime in milliseconds */
int tool_get_uptime(const char *args_json, char *result, size_t res_len);

/** Get board information */
int tool_get_board_info(const char *args_json, char *result, size_t res_len);

/** Get free heap memory */
int tool_get_heap_info(const char *args_json, char *result, size_t res_len);

/** Echo tool (for testing) */
int tool_echo(const char *args_json, char *result, size_t res_len);

#ifdef __cplusplus
}
#endif

#endif /* ZBOT_TOOLS_H */
