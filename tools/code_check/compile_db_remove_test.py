#!/usr/bin/env python
# Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
# SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
# Licensed under the Amazon Software License (the "License").
# You may not use this file except in compliance with the License.
# A copy of the License is located at
# http://aws.amazon.com/asl/
# or in the "license" file accompanying this file. This file is distributed
# on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
# express or implied. See the License fo  r the specific language governing
# permissions and limitations under the License.

import sys
import re

# argv[1] should hold the build dir path
cmake_build_dir = sys.argv[1]
lines = []

with open(cmake_build_dir + '/compile_commands.json', 'r') as f:
    lines = f.readlines()
f.close()

index = 0
while index < len(lines):
    x = re.search('"command": ".+awsiotcpp\/test\/include.+', lines[index] )
    if x:
        del lines[index-2:index+3] # remove a json block
        index = index - 3
    index += 1

# re-write the db file for clang-tidy
f = open(cmake_build_dir + '/Testing/Temporary/compile_commands.json', "w")
f.writelines(lines)
f.close()
