# Copyright (c) 2026 LingaoMeng
# SPDX-License-Identifier: Apache-2.0
#
# ZephyrClaw - An open-source embedded AI Agent for Zephyr RTOS
# Target: nRF7002-DK (nRF5340 + nRF7002 WiFi)

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(zephyrclaw VERSION 0.1.0)

target_sources(app PRIVATE
    src/main.c
    src/tools.c
    src/memory.c
    src/config.c
    src/llm_client.c
    src/agent.c
    src/skill.c
    src/shell_cmd.c
)

target_include_directories(app PRIVATE src)
