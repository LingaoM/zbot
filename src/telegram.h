/*
 * Copyright (c) 2026 Lingao Meng
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ZBot - Telegram Bot Module
 *
 * Polls the Telegram Bot API (getUpdates long-polling) and forwards
 * incoming messages to the agent, sending the response back to the chat.
 *
 * Token is stored via the zbot config subsystem ("zbot/tg_token").
 * Use 'zbot telegram token <token>' to configure and persist it.
 *
 * Commands:
 *   zbot telegram token <token>  -- Set and persist Telegram bot token
 *   zbot telegram start          -- Start polling thread
 *   zbot telegram stop           -- Stop polling thread
 *   zbot telegram status         -- Show current state
 */

#ifndef ZBOT_TELEGRAM_H
#define ZBOT_TELEGRAM_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Start the Telegram polling thread. Returns 0 or negative errno. */
int telegram_start(void);

/** @brief Stop the Telegram polling thread. */
void telegram_stop(void);

/** @brief Return true if the polling thread is running. */
bool telegram_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* ZBOT_TELEGRAM_H */
