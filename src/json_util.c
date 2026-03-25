/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * zbot - JSON utility implementation
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>

#include "json_util.h"

int json_get_str(const char *json, const char *key, char *out, size_t out_len)
{
	char search[80];
	const char *pos;
	size_t i = 0;

	if (!json || !key || !out) {
		return -EINVAL;
	}

	snprintf(search, sizeof(search), "\"%s\"", key);
	pos = strstr(json, search);
	if (!pos) {
		return -ENOENT;
	}

	pos += strlen(search);
	while (*pos == ' ' || *pos == ':' || *pos == '\t') {
		pos++;
	}
	if (*pos == 'n') { /* null */
		out[0] = '\0';
		return 0;
	}
	if (*pos != '"') {
		return -ENOENT;
	}
	pos++;

	while (*pos != '\0' && i + 1 < out_len) {
		if (*pos == '\\') {
			pos++;
			switch (*pos) {
			case 'n':  out[i++] = '\n'; pos++; break;
			case 't':  out[i++] = '\t'; pos++; break;
			case '"':  out[i++] = '"';  pos++; break;
			case '\\': out[i++] = '\\'; pos++; break;
			case 'u': {
				/* Decode \uXXXX, with UTF-16 surrogate pair support */
				pos++;
				if (pos[0] && pos[1] && pos[2] && pos[3]) {
					char hex[5] = {pos[0], pos[1], pos[2], pos[3], '\0'};
					uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);

					pos += 4;

					/* High surrogate \uD800–\uDBFF followed by \uDC00–\uDFFF */
					if (cp >= 0xD800 && cp <= 0xDBFF &&
					    pos[0] == '\\' && pos[1] == 'u' &&
					    pos[2] && pos[3] && pos[4] && pos[5]) {
						char hex2[5] = {pos[2], pos[3], pos[4], pos[5], '\0'};
						uint32_t lo = (uint32_t)strtoul(hex2, NULL, 16);

						if (lo >= 0xDC00 && lo <= 0xDFFF) {
							cp = 0x10000u +
							     ((cp - 0xD800u) << 10) +
							     (lo - 0xDC00u);
							pos += 6;
						}
					}

					/* Encode as UTF-8 */
					if (cp < 0x80u) {
						if (i + 1 < out_len) {
							out[i++] = (char)cp;
						}
					} else if (cp < 0x800u) {
						if (i + 2 < out_len) {
							out[i++] = (char)(0xC0u | (cp >> 6));
							out[i++] = (char)(0x80u | (cp & 0x3Fu));
						}
					} else if (cp < 0x10000u) {
						if (i + 3 < out_len) {
							out[i++] = (char)(0xE0u | (cp >> 12));
							out[i++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
							out[i++] = (char)(0x80u | (cp & 0x3Fu));
						}
					} else {
						/* U+10000–U+10FFFF: emoji range, 4-byte UTF-8 */
						if (i + 4 < out_len) {
							out[i++] = (char)(0xF0u | (cp >> 18));
							out[i++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
							out[i++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
							out[i++] = (char)(0x80u | (cp & 0x3Fu));
						}
					}
				}
				break;
			}
			default: out[i++] = *pos++; break;
			}
		} else if (*pos == '"') {
			break;
		} else {
			out[i++] = *pos++;
		}
	}
	out[i] = '\0';
	return (int)i;
}

int json_escape(const char *src, char *dst, size_t dst_len)
{
	size_t j = 0;

	for (size_t i = 0; src[i] != '\0' && j + 2 < dst_len; i++) {
		unsigned char c = (unsigned char)src[i];

		if (c == '"') {
			dst[j++] = '\\';
			dst[j++] = '"';
		} else if (c == '\\') {
			dst[j++] = '\\';
			dst[j++] = '\\';
		} else if (c == '\n') {
			dst[j++] = '\\';
			dst[j++] = 'n';
		} else if (c == '\r') {
			dst[j++] = '\\';
			dst[j++] = 'r';
		} else if (c < 0x20) {
			dst[j++] = ' ';
		} else {
			dst[j++] = c;
		}
	}
	dst[j] = '\0';
	return (int)j;
}
