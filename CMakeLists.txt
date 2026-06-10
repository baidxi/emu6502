# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(emu6502)

# 递归收集 src/ 下所有 .c 源文件
FILE(GLOB_RECURSE app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

# NES 模拟核心模块头文件路径
target_include_directories(app PRIVATE
    src
    src/nes
    src/ui
    src/platform
)
