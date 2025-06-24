#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

WITH_LAST_KNOWN_STATE_SUPPORT="false"
WITH_CUSTOM_FUNCTION_EXAMPLES="false"

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
source ${SCRIPT_DIR}/install-deps-versions.sh

NATIVE_PREFIX="/usr/local"
ARCHS="x86_64:x86_64-linux-android \
       armeabi-v7a:armv7a-linux-androideabi \
       arm64-v8a:aarch64-linux-android"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --archs)
            ARCHS=$2
            shift
            ;;
        --native-prefix)
            NATIVE_PREFIX="$2"
            shift
            ;;
        --with-lks-support)
            WITH_LAST_KNOWN_STATE_SUPPORT="true"
            ;;
        --with-custom-function-examples)
            WITH_CUSTOM_FUNCTION_EXAMPLES="true"
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --archs <ARCHS>                   Space separated list of archs in the format <ARCH>:<HOST_PLATFORM>"
            echo "  --native-prefix                   Native install prefix, default ${NATIVE_PREFIX}"
            echo "  --with-lks-support                Build with LastKnownState support"
            echo "  --with-custom-function-examples   Build with custom function examples"
            exit 0
            ;;
        esac
        shift
    done
}

parse_args "$@"

: ${FWE_ADDITIONAL_CMAKE_ARGS:=""}

export PATH=/usr/local/android_sdk/cmake/${VERSION_CMAKE}/bin:${NATIVE_PREFIX}/bin:${PATH}

CMAKE_OPTIONS="
    -DFWE_STATIC_LINK=On
    -DFWE_STRIP_SYMBOLS=On
    -DFWE_FEATURE_EXTERNAL_GPS=On
    -DFWE_FEATURE_AAOS_VHAL=On
    -DFWE_BUILD_EXECUTABLE=Off
    -DFWE_BUILD_ANDROID_SHARED_LIBRARY=On
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_TESTING=Off"
if ${WITH_LAST_KNOWN_STATE_SUPPORT}; then
    CMAKE_OPTIONS="${CMAKE_OPTIONS} -DFWE_FEATURE_LAST_KNOWN_STATE=On"
fi
if ${WITH_CUSTOM_FUNCTION_EXAMPLES}; then
    CMAKE_OPTIONS="${CMAKE_OPTIONS} -DFWE_FEATURE_CUSTOM_FUNCTION_EXAMPLES=On"
fi

mkdir -p build && cd build

build() {
    TARGET_ARCH="$1"
    HOST_PLATFORM="$2"

    mkdir -p ${TARGET_ARCH} && cd ${TARGET_ARCH}
    LDFLAGS=-L/usr/local/${HOST_PLATFORM}/lib cmake \
        ${CMAKE_OPTIONS} \
        ${FWE_ADDITIONAL_CMAKE_ARGS} \
        -DANDROID_ABI=${TARGET_ARCH} \
        -DANDROID_PLATFORM=android-${VERSION_ANDROID_API} \
        -DCMAKE_ANDROID_NDK=/usr/local/android_sdk/ndk/${VERSION_ANDROID_NDK} \
        -DCMAKE_TOOLCHAIN_FILE=/usr/local/android_sdk/ndk/${VERSION_ANDROID_NDK}/build/cmake/android.toolchain.cmake \
        -DANDROID_TOOLCHAIN=clang \
        -DCMAKE_FIND_ROOT_PATH=/usr/local/${HOST_PLATFORM} \
        ../..
    make -j`nproc`
    cd ..
}

for ARCH in ${ARCHS}; do
    TARGET_ARCH=`echo $ARCH | cut -d ':' -f1`
    HOST_PLATFORM=`echo $ARCH | cut -d ':' -f2`
    build ${TARGET_ARCH} ${HOST_PLATFORM}
done
