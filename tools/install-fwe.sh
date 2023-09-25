#!/bin/sh
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -e

./tools/deploy/stop-fwe.sh

mkdir -p /var/aws-iot-fleetwise

cp build/aws-iot-fleetwise-edge /usr/bin/
cp tools/deploy/run-fwe.sh      /usr/bin/
cp tools/deploy/fwe@.service    /lib/systemd/system/

./tools/deploy/start-and-enable-fwe.sh
