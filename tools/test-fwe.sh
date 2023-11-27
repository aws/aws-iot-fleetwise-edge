#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -eo pipefail

WITH_ROS2_SUPPORT="false"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --with-ros2-support)
            WITH_ROS2_SUPPORT="true"
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --with-ros2-support  Test with ROS2 support"
            exit 0
            ;;
        esac
        shift
    done
}

parse_args "$@"

if ${WITH_ROS2_SUPPORT}; then
    source /opt/ros/galactic/setup.bash
    # colcon hides the test output, so use tail in the background.
    tail -F log/latest_test/iotfleetwise/stdout_stderr.log &
    colcon test --return-code-on-test-failure
else
    cd build
    ctest -V
fi
