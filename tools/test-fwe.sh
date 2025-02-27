#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -efo pipefail

WITH_ROS2_SUPPORT="false"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --with-ros2-support)
            WITH_ROS2_SUPPORT="true"
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --with-ros2-support    Test with ROS2 support"
            exit 0
            ;;
        esac
        shift
    done
}

parse_args "$@"

if ${WITH_ROS2_SUPPORT}; then
    source /opt/ros/galactic/setup.bash
    cd build/iotfleetwise
else
    cd build
fi
ctest -V
