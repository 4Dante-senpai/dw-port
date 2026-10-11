# DW1 port build (native and web). The PS1 matching build stays in the upstream Makefile.
cmake_minimum_required(VERSION 3.28)
project(dw1_port C)

set(DW1_VERSION us)
string(TOUPPER ${DW1_VERSION} DW1_VERSION_MACRO)

# Game translation units and their unit (main or overlay), generated from upstream config (AD-1).
set(DW1_TU_LIST ${CMAKE_BINARY_DIR}/tu_list_${DW1_VERSION}.tsv)
execute_process(
  COMMAND python3 -I tools/port/units/tu_list.py ${DW1_VERSION} -o ${DW1_TU_LIST}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMAND_ERROR_IS_FATAL ANY)
file(GLOB DW1_CONFIG_YAML CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/config/${DW1_VERSION}/*.yaml)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  ${CMAKE_SOURCE_DIR}/mk/version/${DW1_VERSION}.mk ${DW1_CONFIG_YAML}
  ${CMAKE_SOURCE_DIR}/tools/port/units/tu_list.py)

file(STRINGS ${DW1_TU_LIST} DW1_TU_ROWS)
set(DW1_GAME_SRC)
foreach(row IN LISTS DW1_TU_ROWS)
  string(REPLACE "\t" ";" fields "${row}")
  list(GET fields 0 path)
  list(GET fields 2 status)
  if(status STREQUAL "build")
    list(APPEND DW1_GAME_SRC ${CMAKE_SOURCE_DIR}/${path})
  endif()
endforeach()

# Text headers (text/<dir>/<name>.yaml -> generated/text/<dir>/<name>.h), as mk/text.mk does.
file(GLOB_RECURSE DW1_TEXT_YAML CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR}/text ${CMAKE_SOURCE_DIR}/text/*/*.yaml)
set(DW1_TEXT_H)
foreach(y IN LISTS DW1_TEXT_YAML)
  string(REGEX REPLACE "\\.yaml$" ".h" h ${y})
  set(out ${CMAKE_BINARY_DIR}/generated/text/${h})
  add_custom_command(OUTPUT ${out}
    COMMAND python3 -I tools/gen_text.py ${DW1_VERSION} text/${y} ${out}
    DEPENDS ${CMAKE_SOURCE_DIR}/text/${y} ${CMAKE_SOURCE_DIR}/tools/gen_text.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  list(APPEND DW1_TEXT_H ${out})
endforeach()

add_compile_options(-std=gnu99 -ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)

# Game code: same semantics in every preset (AD-17). Port headers come first (AD-2).
add_library(dw1_game_objs OBJECT ${DW1_GAME_SRC} ${DW1_TEXT_H})
target_include_directories(dw1_game_objs PRIVATE
  port/psyq/include external/psyq_headers/mw_lib41/include include ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(dw1_game_objs PRIVATE
  DW1_PORT LANGUAGE_C VERSION_${DW1_VERSION_MACRO})
target_compile_options(dw1_game_objs PRIVATE
  -fsigned-char -fwrapv -fno-strict-aliasing -fno-common -w
  -include ${CMAKE_SOURCE_DIR}/port/psyq/include/port_game.h)

add_library(dw1_psyq OBJECT port/psyq/ps1mem.c)
target_compile_options(dw1_psyq PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_test(NAME tu_list COMMAND python3 -I -B tools/port/units/test_tu_list.py
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME inventory COMMAND python3 -I -B tools/port/metrics/test_inventory.py
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME preproc_check COMMAND sh tools/port/ci/preproc_check.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME test_preproc_check COMMAND sh tools/port/ci/test_preproc_check.sh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
