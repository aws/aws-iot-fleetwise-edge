# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

if(NOT MICROPYTHON_PATH)
    set(MICROPYTHON_PATH ${CMAKE_SYSTEM_PREFIX_PATH})
endif()

find_path(MICROPYTHON_TOP
    "ports/embed/port/micropython_embed.h"
    PATH_SUFFIXES "src/micropython"
    HINTS ${MICROPYTHON_PATH})

if(NOT MICROPYTHON_TOP)
    message(FATAL_ERROR "Could not find MicroPython source - specify with MICROPYTHON_PATH")
endif()

message(STATUS "Found MicroPython source: ${MICROPYTHON_TOP}")

message(STATUS "Generating MicroPython embed")
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/micropython)
execute_process(
    COMMAND bash -c "cp ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/micropython/* . \
        && make MICROPYTHON_TOP=${MICROPYTHON_TOP} QSTR_DEFS=qstrdefsport.h -f micropython_embed.mk \
        && cp ${MICROPYTHON_TOP}/extmod/modjson.c micropython_embed/extmod"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/micropython
    RESULT_VARIABLE ret
)
if(NOT ret EQUAL 0)
    message(FATAL_ERROR "Error generating MicroPython embed")
endif()

file(GLOB_RECURSE MICROPYTHON_SRC
    ${CMAKE_CURRENT_BINARY_DIR}/micropython/micropython_embed/*.c)

add_library(micropython_embed
    ${MICROPYTHON_SRC}
    ${CMAKE_CURRENT_BINARY_DIR}/micropython/micropython_config.c)

set(MICROPYTHON_INCLUDE_DIRS
    ${CMAKE_CURRENT_BINARY_DIR}/micropython
    ${CMAKE_CURRENT_BINARY_DIR}/micropython/micropython_embed
    ${CMAKE_CURRENT_BINARY_DIR}/micropython/micropython_embed/port
)
target_include_directories(micropython_embed PUBLIC
    ${MICROPYTHON_INCLUDE_DIRS})

target_compile_options(micropython_embed PRIVATE -Wno-conversion -Wno-extra -Wno-pedantic)
