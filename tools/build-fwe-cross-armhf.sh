#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -eo pipefail

NATIVE_PREFIX="/usr/local/`gcc -dumpmachine`"
export PATH=${NATIVE_PREFIX}/bin:${PATH}
mkdir -p build && cd build
cmake \
  -DFWE_STATIC_LINK=On \
  -DFWE_STRIP_SYMBOLS=On \
  -DFWE_SECURITY_COMPILE_FLAGS=On \
  -DCMAKE_TOOLCHAIN_FILE=/usr/local/arm-linux-gnueabihf/lib/cmake/armhf-toolchain.cmake \
  -DBUILD_TESTING=Off \
  ..
make -j`nproc`
