#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

# arg1: ${CMAKE_CURRENT_SOURCE_DIR}
# arg2: ${CMAKE_BINARY_DIR}

# Generate compile_commands.json for clang-tidy test
python3 $1/tools/code_check/compile_db_remove_test.py $2

# Run clang-tidy test
run-clang-tidy-10 -header-filter=$1/src/.* -p $2/Testing/Temporary $1/src
