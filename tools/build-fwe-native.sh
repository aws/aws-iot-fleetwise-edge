#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -eo pipefail

mkdir -p build && cd build
cmake \
  -DFWE_STATIC_LINK=On \
  -DFWE_STRIP_SYMBOLS=On \
  -DFWE_SECURITY_COMPILE_FLAGS=On \
  ..
make -j`nproc`
