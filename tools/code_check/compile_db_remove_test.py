#!/usr/bin/env python
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import re
import sys

# argv[1] should hold the build dir path
cmake_build_dir = sys.argv[1]
lines = []

with open(cmake_build_dir + "/compile_commands.json") as f:
    lines = f.readlines()
f.close()

index = 0
while index < len(lines):
    x = re.search(r'"command": ".+iotcpp\/test\/include.+', lines[index])
    if x:
        del lines[index - 2 : index + 3]  # remove a json block
        index = index - 3
    index += 1

# re-write the db file for clang-tidy
f = open(cmake_build_dir + "/Testing/Temporary/compile_commands.json", "w")
f.writelines(lines)
f.close()
