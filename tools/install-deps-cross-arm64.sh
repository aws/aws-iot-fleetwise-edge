#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR=$(dirname $(realpath "$0"))
source ${SCRIPT_DIR}/install-deps-versions.sh

USE_CACHE="true"
WITH_GREENGRASSV2_SUPPORT="false"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --with-greengrassv2-support)
            WITH_GREENGRASSV2_SUPPORT="true"
            ;;
        --native-prefix)
            NATIVE_PREFIX="$2"
            USE_CACHE="false"
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --with-greengrassv2-support  Install dependencies for Greengrass V2"
            echo "  --native-prefix              Native install prefix"
            exit 0
            ;;
        esac
        shift
    done
}

parse_args "$@"

ARCH=`dpkg --print-architecture`
if [ "${ARCH}" == "arm64" ]; then
    echo "Error: Architecture is already ${ARCH}, use install-deps-native.sh" >&2
    exit -1
fi

sed -i "s/deb http/deb [arch=${ARCH}] http/g" /etc/apt/sources.list
cp /etc/apt/sources.list /etc/apt/sources.list.d/arm64.list
sed -i "s/deb \[arch=${ARCH}\] http/deb [arch=arm64] http/g" /etc/apt/sources.list.d/arm64.list
sed -i -E "s#(archive|security).ubuntu.com/ubuntu#ports.ubuntu.com/ubuntu-ports#g" /etc/apt/sources.list.d/arm64.list
dpkg --add-architecture arm64
apt update
apt install -y \
    build-essential \
    cmake \
    crossbuild-essential-arm64 \
    curl \
    git \
    libboost-log-dev:arm64 \
    libboost-system-dev:arm64 \
    libboost-thread-dev:arm64 \
    libsnappy-dev:arm64 \
    libssl-dev:arm64 \
    unzip \
    wget \
    zlib1g-dev:arm64

if [ ! -f /usr/include/linux/can/isotp.h ]; then
    git clone https://github.com/hartkopp/can-isotp.git
    cd can-isotp
    git checkout beb4650660179963a8ed5b5cbf2085cc1b34f608
    cp include/uapi/linux/can/isotp.h /usr/include/linux/can
    cd ..
    rm -rf can-isotp
fi

if [ -z "${NATIVE_PREFIX+x}" ]; then
    NATIVE_PREFIX="/usr/local/`gcc -dumpmachine`"
fi

if ! ${USE_CACHE} || [ ! -d /usr/local/aarch64-linux-gnu ] || [ ! -d ${NATIVE_PREFIX} ]; then
    mkdir -p /usr/local/aarch64-linux-gnu/lib/cmake/
    mkdir -p ${NATIVE_PREFIX}
    cp ${SCRIPT_DIR}/arm64-toolchain.cmake /usr/local/aarch64-linux-gnu/lib/cmake/
    mkdir deps-cross-arm64 && cd deps-cross-arm64

    git clone -b ${VERSION_JSON_CPP} https://github.com/open-source-parsers/jsoncpp.git
    cd jsoncpp
    mkdir build && cd build
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=On \
        -DJSONCPP_WITH_TESTS=Off \
        -DJSONCPP_WITH_POST_BUILD_UNITTEST=Off \
        -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
        -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
        ..
    make install -j`nproc`
    cd ../..

    wget -q https://github.com/protocolbuffers/protobuf/releases/download/${VERSION_PROTOBUF_RELEASE}/protobuf-cpp-${VERSION_PROTOBUF}.tar.gz
    tar -zxf protobuf-cpp-${VERSION_PROTOBUF}.tar.gz
    cd protobuf-${VERSION_PROTOBUF}
    mkdir build && cd build
    ../configure --prefix=${NATIVE_PREFIX}
    make install -j`nproc`
    cd ..
    mkdir build_arm64 && cd build_arm64
    CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ \
        ../configure --host=aarch64-linux --prefix=/usr/local/aarch64-linux-gnu
    make install -j`nproc`
    cd ../..

    wget -q https://github.com/curl/curl/releases/download/${VERSION_CURL_RELEASE}/curl-${VERSION_CURL}.tar.gz
    tar -zxf curl-${VERSION_CURL}.tar.gz
    cd curl-${VERSION_CURL}
    mkdir build && cd build
    LDFLAGS="-static" PKG_CONFIG="pkg-config --static" CC=aarch64-linux-gnu-gcc ../configure \
        --disable-shared --enable-static --disable-ldap --enable-ipv6 --with-ssl --disable-unix-sockets \
        --disable-rtsp --without-zstd --host=aarch64-linux --prefix=/usr/local/aarch64-linux-gnu
    make install -j`nproc` V=1 LDFLAGS="-static"
    cd ../..

    git clone -b ${VERSION_AWS_SDK_CPP} --recursive https://github.com/aws/aws-sdk-cpp.git
    cd aws-sdk-cpp
    mkdir build && cd build
    cmake \
        -DENABLE_TESTING=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_ONLY='s3-crt;iot' \
        -DAWS_CUSTOM_MEMORY_MANAGEMENT=ON \
        -DZLIB_LIBRARY=/usr/lib/aarch64-linux-gnu/libz.a \
        -DCURL_LIBRARY=/usr/local/aarch64-linux-gnu/lib/libcurl.a \
        -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
        -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
        ..
    make install -j`nproc`
    cd ../..

    if ${WITH_GREENGRASSV2_SUPPORT}; then
        git clone -b ${VERSION_AWS_IOT_DEVICE_SDK_CPP_V2} --recursive https://github.com/aws/aws-iot-device-sdk-cpp-v2.git
        cd aws-iot-device-sdk-cpp-v2
        mkdir build && cd build
        cmake \
            -DBUILD_SHARED_LIBS=OFF \
            -DBUILD_DEPS=ON \
            -DBUILD_TESTING=OFF \
            -DUSE_OPENSSL=ON \
            -DBUILD_ONLY='greengrass_ipc' \
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
            ..
        make install -j`nproc`
        cd ../..
    fi

    cd ..
    rm -rf deps-cross-arm64
fi
