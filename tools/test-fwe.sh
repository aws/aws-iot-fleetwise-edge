#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -eo pipefail

WITH_ROS2_SUPPORT="false"
IGNORE_TESTS=""

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --with-ros2-support)
            WITH_ROS2_SUPPORT="true"
            shift
            ;;
        --extra-ctest-args)
            IGNORE_TESTS=$2
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --with-ros2-support  Test with ROS2 support"
            echo "  --extra-ctest-args Gives the tests to ignore"
            exit 0
            ;;
        esac
        shift
    done
}

parse_args "$@"

if ${WITH_ROS2_SUPPORT}; then
    source /opt/ros/galactic/setup.bash
    if [ -n "${IGNORE_TESTS}" ]; then
        ctest -V ${IGNORE_TESTS}
    else
        ctest -V
    fi
else
    cd build
    if [ -n "${IGNORE_TESTS}" ]; then
        ctest -V ${IGNORE_TESTS}
    else
        ctest -V
    fi
fi
