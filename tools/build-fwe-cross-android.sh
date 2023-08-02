#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR=$(dirname $(realpath "$0"))
source ${SCRIPT_DIR}/install-deps-versions.sh

NATIVE_PREFIX="/usr/local/`gcc -dumpmachine`"
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
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --archs <ARCHS>  Space separated list of archs in the format <ARCH>:<HOST_PLATFORM>"
            echo "  --native-prefix  Native install prefix, default ${NATIVE_PREFIX}"
            exit 0
            ;;
        esac
        shift
    done
}

parse_args "$@"

export PATH=/usr/local/android_sdk/cmake/${VERSION_CMAKE}/bin:${NATIVE_PREFIX}/bin:${PATH}
mkdir -p build && cd build

build() {
    TARGET_ARCH="$1"
    HOST_PLATFORM="$2"

    mkdir -p ${TARGET_ARCH} && cd ${TARGET_ARCH}
    LDFLAGS=-L/usr/local/${HOST_PLATFORM}/lib cmake \
        -DFWE_STATIC_LINK=On \
        -DFWE_STRIP_SYMBOLS=On \
        -DFWE_FEATURE_CUSTOM_DATA_SOURCE=On \
        -DFWE_FEATURE_EXTERNAL_GPS=On \
        -DFWE_FEATURE_AAOS_VHAL=On \
        -DFWE_BUILD_EXECUTABLE=Off \
        -DFWE_BUILD_ANDROID_SHARED_LIBRARY=On \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=Off \
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
