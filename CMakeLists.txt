# Copyright (c) 2026 Lingao Meng
# SPDX-License-Identifier: Apache-2.0
#
# ZBot - An open-source embedded AI Agent for Zephyr RTOS
# Supported boards: nRF7002-DK (nRF5340 + nRF7002 WiFi), native_sim

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(ZBot VERSION 0.1.0)

target_sources(app PRIVATE
    src/main.c
    src/tools.c
    src/memory.c
    src/config.c
    src/llm_client.c
    src/agent.c
    src/shell_cmd.c
    src/telegram.c
    src/json_util.c
    src/skills/skill.c
)

target_include_directories(app PRIVATE src src/skills src/tools)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

# OpenRouter TLS certificate
generate_inc_file_for_target(
    app
    src/certs/openrouter/cert-3.der
    ${gen_dir}/openrouter/https-cert.der.inc
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/AGENTS.md.in
    ${gen_dir}/AGENTS.md
    @ONLY
)

# Agent system prompt
generate_inc_file_for_target(
    app
    ${gen_dir}/AGENTS.md
    ${gen_dir}/AGENTS.md.inc
)

add_subdirectory(src/tools)
add_subdirectory(src/skills)
