#!/bin/sh
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -e

if [ $# -eq 0 ]; then
    echo "Instance number not provided"
    exit 1
fi

EXTRA_ARGS=""

if [ -f /opt/ros/humble/setup.sh ]; then
    . /opt/ros/humble/setup.sh
    EXTRA_ARGS="${EXTRA_ARGS} --ros-args --disable-external-lib-logs"
fi

export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
/usr/bin/aws-iot-fleetwise-edge /etc/aws-iot-fleetwise/config-$1.json ${EXTRA_ARGS}
