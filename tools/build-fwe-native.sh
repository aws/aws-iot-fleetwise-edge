#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -eo pipefail

WITH_GREENGRASSV2_SUPPORT="false"
WITH_ROS2_SUPPORT="false"
WITH_SOMEIP_SUPPORT="false"
WITH_SNF_SUPPORT="false"
WITH_LAST_KNOWN_STATE_SUPPORT="false"
WITH_GENERIC_DTC_SUPPORT="false"
WITH_CUSTOM_FUNCTION_EXAMPLES="false"
WITH_REMOTE_COMMANDS_SUPPORT="false"
WITH_CPYTHON_SUPPORT="false"
WITH_MICROPYTHON_SUPPORT="false"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --with-greengrassv2-support)
            WITH_GREENGRASSV2_SUPPORT="true"
            ;;
        --with-ros2-support)
            WITH_ROS2_SUPPORT="true"
            ;;
        --with-someip-support)
            WITH_SOMEIP_SUPPORT="true"
            ;;
        --with-store-and-forward-support)
            WITH_SNF_SUPPORT="true"
            ;;
        --with-lks-support)
            WITH_LAST_KNOWN_STATE_SUPPORT="true"
            ;;
        --with-uds-dtc-example)
            WITH_GENERIC_DTC_SUPPORT="true"
            ;;
        --with-custom-function-examples)
            WITH_CUSTOM_FUNCTION_EXAMPLES="true"
            ;;
        --with-remote-commands-support)
            WITH_REMOTE_COMMANDS_SUPPORT="true"
            ;;
        --with-cpython-support)
            WITH_CPYTHON_SUPPORT="true"
            ;;
        --with-micropython-support)
            WITH_MICROPYTHON_SUPPORT="true"
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --with-greengrassv2-support       Build with Greengrass V2 support"
            echo "  --with-ros2-support               Build with ROS2 support"
            echo "  --with-someip-support             Build with SOME/IP support"
            echo "  --with-store-and-forward-support  Build with Store and Forward support"
            echo "  --with-lks-support                Build with LastKnownState support"
            echo "  --with-uds-dtc-example            Build with UDS DTC Example"
            echo "  --with-custom-function-examples   Build with custom function examples"
            echo "  --with-remote-commands-support    Build with remote commands support"
            echo "  --with-cpython-support            Build with CPython support"
            echo "  --with-micropython-support        Build with MicroPython support"
            exit 0
            ;;
        esac
        shift
    done
}

parse_args "$@"

PREFIX="/usr/local/`gcc -dumpmachine`"
export PATH=${PREFIX}/bin:${PATH}

CMAKE_OPTIONS="
    -DFWE_STATIC_LINK=On
    -DFWE_STRIP_SYMBOLS=On
    -DFWE_SECURITY_COMPILE_FLAGS=On
    -DFWE_TEST_FAKETIME=On
    -DCMAKE_PREFIX_PATH=${PREFIX}"
if ${WITH_GREENGRASSV2_SUPPORT}; then
    CMAKE_OPTIONS="${CMAKE_OPTIONS} -DFWE_FEATURE_GREENGRASSV2=On"
fi
if ${WITH_ROS2_SUPPORT}; then
    CMAKE_OPTIONS="${CMAKE_OPTIONS} -DFWE_FEATURE_ROS2=On"
fi
if ${WITH_SOMEIP_SUPPORT}; then
    CMAKE_OPTIONS="${CMAKE_OPTIONS} -DFWE_FEATURE_SOMEIP=On"
fi
if ${WITH_SNF_SUPPORT}; then
    CMAKE_OPTIONS="${CMAKE_OPTIONS} -DFWE_FEATURE_STORE_AND_FORWARD=On"
fi
if ${WITH_LAST_KNOWN_STATE_SUPPORT}; then
    CMAKE_OPTIONS="${CMAKE_OPTIONS} -DFWE_FEATURE_LAST_KNOWN_STATE=On"
fi
if ${WITH_GENERIC_DTC_SUPPORT}; then
    CMAKE_OPTIONS="${CMAKE_OPTIONS} -DFWE_FEATURE_UDS_DTC_EXAMPLE=On"
fi
if ${WITH_CUSTOM_FUNCTION_EXAMPLES}; then
    CMAKE_OPTIONS="${CMAKE_OPTIONS} -DFWE_FEATURE_CUSTOM_FUNCTION_EXAMPLES=On"
fi
if ${WITH_REMOTE_COMMANDS_SUPPORT}; then
    CMAKE_OPTIONS="${CMAKE_OPTIONS} -DFWE_FEATURE_REMOTE_COMMANDS=On"
fi
if ${WITH_CPYTHON_SUPPORT}; then
    CMAKE_OPTIONS="${CMAKE_OPTIONS} -DFWE_FEATURE_CPYTHON=On"
fi
if ${WITH_MICROPYTHON_SUPPORT}; then
    CMAKE_OPTIONS="${CMAKE_OPTIONS} -DFWE_FEATURE_MICROPYTHON=On"
fi

if ${WITH_ROS2_SUPPORT}; then
    BUILD_DIR=build/iotfleetwise
    source /opt/ros/galactic/setup.bash
    colcon build --cmake-args ${CMAKE_OPTIONS}
else
    BUILD_DIR=build
    mkdir -p build && cd build
    cmake ${CMAKE_OPTIONS} ..
    make -j`nproc`
    cd ..
fi

if ${WITH_SOMEIP_SUPPORT}; then
    cp ${BUILD_DIR}/can-to-someip tools/can-to-someip
    cp ${BUILD_DIR}/someipigen.so tools/someipigen
    cp ${BUILD_DIR}/someip_device_shadow_editor.so tools/someip_device_shadow_editor
fi
