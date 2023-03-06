#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -eo pipefail

PREFIX="/usr/local/`gcc -dumpmachine`"
export PATH=${PREFIX}/bin:${PATH}
mkdir -p build && cd build
cmake \
  -DFWE_STATIC_LINK=On \
  -DFWE_STRIP_SYMBOLS=On \
  -DFWE_SECURITY_COMPILE_FLAGS=On \
  -DFWE_TEST_FAKETIME=On \
  -DCMAKE_PREFIX_PATH=${PREFIX} \
  ..
make -j`nproc`
