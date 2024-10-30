#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -efo pipefail

WITH_ROS2_SUPPORT="false"
EXTRA_GTEST_FILTER="*"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --with-ros2-support)
            WITH_ROS2_SUPPORT="true"
            ;;
        --extra-gtest-filter)
            EXTRA_GTEST_FILTER=$2
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --with-ros2-support    Test with ROS2 support"
            echo "  --extra-gtest-filter   Regex to set for gtest filter eg. '-*<tests to skip>*' or '*<tests to execute>*'"
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
GTEST_FILTER=${EXTRA_GTEST_FILTER} ctest -V
