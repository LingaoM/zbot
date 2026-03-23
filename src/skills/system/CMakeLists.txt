# Copyright (c) 2026 Lingao Meng
# SPDX-License-Identifier: Apache-2.0

target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/SKILL.c)

generate_inc_file_for_target(
    app
    ${CMAKE_CURRENT_SOURCE_DIR}/SKILL.md
    ${gen_dir}/skills/system/SKILL.md.inc
)
