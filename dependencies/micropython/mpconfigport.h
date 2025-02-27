// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

// Include common MicroPython embed configuration.
#include <port/mpconfigport_common.h>

// clang-format off

// Use the minimal starting configuration (disables all optional features).
#define MICROPY_CONFIG_ROM_LEVEL                (MICROPY_CONFIG_ROM_LEVEL_MINIMUM)

// MicroPython configuration.
#define MICROPY_ENABLE_COMPILER                 (1)
#define MICROPY_ENABLE_GC                       (1)
#define MICROPY_PY_GC                           (1)
#define MICROPY_ERROR_REPORTING                 (MICROPY_ERROR_REPORTING_DETAILED)
#define MICROPY_FLOAT_IMPL                      (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_PY_JSON                         (1)
#define MICROPY_PY_IO                           (1)
#define MICROPY_ENABLE_SOURCE_LINE              (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT          (1)
#define MICROPY_ENABLE_FINALISER                (1)
#define MICROPY_READER_POSIX                    (1)
#define MICROPY_PY_SYS                          (1)
#define MICROPY_PY_SYS_PATH                     (1)
#define MICROPY_PY_SYS_PLATFORM                 "fwe"
#define MICROPY_QSTR_BYTES_IN_LEN               (2)

// clang-format on
