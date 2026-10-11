# Toolchain smoke test: one SDL3 program built natively and with Emscripten (tools/port/env_check.sh).
cmake_minimum_required(VERSION 3.20)
project(env_check C)
find_package(SDL3 3.4 REQUIRED CONFIG)
add_executable(env_check env_check.c)
target_link_libraries(env_check PRIVATE SDL3::SDL3)
