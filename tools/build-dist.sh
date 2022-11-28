#!/bin/bash

set -euo pipefail

rm -rf build/dist
mkdir build/dist
cd build/dist
cp -r ../src/executionmanagement/aws-iot-fleetwise-edge \
    ../../LICENSE \
    ../../THIRD-PARTY-LICENSES \
    ../../configuration \
    ../../tools \
    .
tar -zcf ../aws-iot-fleetwise-edge.tar.gz *
