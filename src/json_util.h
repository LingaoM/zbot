/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZBot - JSON utility
 *
 * Minimal JSON string extraction with full escape sequence support:
 *   \n  \t  \"  \\  \uXXXX  (including UTF-16 surrogate pairs for emoji)
 */

#ifndef ZBOT_JSON_UTIL_H
#define ZBOT_JSON_UTIL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Extract a JSON string value for @p key into @p out.
 *
 * Handles null values (writes empty string, returns 0) and all standard
 * JSON escape sequences including \uXXXX with UTF-16 surrogate pairs.
 *
 * @param json    JSON text to search.
 * @param key     Key name (without quotes).
 * @param out     Output buffer.
 * @param out_len Output buffer size.
 * @return Number of bytes written on success, -EINVAL on bad args,
 *         -ENOENT if key not found or value is not a string.
 */
int json_get_str(const char *json, const char *key, char *out, size_t out_len);

/**
 * @brief Escape a UTF-8 string for embedding in a JSON string value.
 *
 * Escapes \", \\, \n, \r and replaces other control characters with a space.
 * Does NOT add surrounding quotes.
 *
 * @param src     Input string.
 * @param dst     Output buffer.
 * @param dst_len Output buffer size.
 * @return Number of bytes written (excluding NUL terminator).
 */
int json_escape(const char *src, char *dst, size_t dst_len);

#ifdef __cplusplus
}
#endif

#endif /* ZBOT_JSON_UTIL_H */
