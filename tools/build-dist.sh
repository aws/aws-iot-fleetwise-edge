#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

if (($#<1)); then
    echo "Error: Output files not provided" >&2
    exit -1
fi

rm -rf build/dist
mkdir build/dist
cd build/dist

for FILE in $@; do
    SRC=`echo $FILE | cut -d ':' -f1`
    DEST=`echo $FILE | cut -d ':' -f2`
    mkdir -p $DEST
    cp ../../$SRC $DEST
done

cp -r ../../LICENSE \
    ../../THIRD-PARTY-LICENSES \
    ../../configuration \
    ../../tools \
    .
rm -rf tools/android-app

tar -zcf ../aws-iot-fleetwise-edge.tar.gz *
