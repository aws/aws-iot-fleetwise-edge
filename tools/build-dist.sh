#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

if (($#<1)); then
    echo "Error: Output binary not provided" >&2
    exit -1
fi
OUTPUT_BINARY=`realpath $1`

rm -rf build/dist
mkdir build/dist
cd build/dist
cp -r ${OUTPUT_BINARY} \
    ../../LICENSE \
    ../../THIRD-PARTY-LICENSES \
    ../../configuration \
    ../../tools \
    .
tar -zcf ../aws-iot-fleetwise-edge.tar.gz *
