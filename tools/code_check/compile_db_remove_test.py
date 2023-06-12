#!/usr/bin/env python
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import re
import sys

# argv[1] should hold the build dir path
cmake_build_dir = sys.argv[1]

compile_commands = []

with open(cmake_build_dir + "/compile_commands.json") as f:
    compile_commands = json.load(f)
f.close()

output_compile_commands = []

for block in compile_commands:
    if "command" in block:
        if re.search(r".+iotcpp\/test\/include.+", block["command"]):
            continue
        # If we keep -Werror, we will get compiler errors from clang that don't happen with GCC.
        # Since clang is currently not one of our supported compilers, we will ignore the warnings.
        block["command"] = block["command"].replace("-Werror", "")
    output_compile_commands.append(block)

# re-write the db file for clang-tidy
f = open(cmake_build_dir + "/Testing/Temporary/compile_commands.json", "w")
f.writelines(json.dumps(output_compile_commands, indent=2))
f.close()
