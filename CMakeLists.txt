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
    src/skill.c
    src/shell_cmd.c
)

target_include_directories(app PRIVATE src)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

generate_inc_file_for_target(
    app
    src/certs/openrouter/cert-3.der
    ${gen_dir}/openrouter/https-cert.der.inc
)
