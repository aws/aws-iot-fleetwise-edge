#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

WITH_CAMERA_SUPPORT="false"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --with-camera-support)
            WITH_CAMERA_SUPPORT="true"
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --with-camera-support  Installs dependencies required for camera support"
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

cp tools/arm64.list /etc/apt/sources.list.d/
mkdir -p /usr/local/aarch64-linux-gnu/lib/cmake/
cp tools/arm64-toolchain.cmake /usr/local/aarch64-linux-gnu/lib/cmake/

dpkg --add-architecture arm64
sed -i "s/deb http/deb [arch=${ARCH}] http/g" /etc/apt/sources.list
apt update
apt install -y \
    libssl-dev:arm64 \
    libboost-system-dev:arm64 \
    libboost-log-dev:arm64 \
    libboost-thread-dev:arm64 \
    build-essential \
    crossbuild-essential-arm64 \
    cmake \
    unzip \
    git \
    wget \
    curl \
    zlib1g-dev:arm64 \
    libsnappy-dev:arm64

mkdir -p deps-cross-arm64 && cd deps-cross-arm64

if [ ! -d jsoncpp ]; then
    git clone -b 1.7.4 https://github.com/open-source-parsers/jsoncpp.git
    cd jsoncpp
    mkdir build && cd build
    cmake \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=On \
        -DJSONCPP_WITH_TESTS=Off \
        -DJSONCPP_WITH_POST_BUILD_UNITTEST=Off \
        -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
        -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
        ..
    cd ../..
fi
make install -j`nproc` -C jsoncpp/build

if [ ! -d protobuf-21.7 ]; then
    wget -q https://github.com/protocolbuffers/protobuf/releases/download/v21.7/protobuf-all-21.7.tar.gz
    tar -zxf protobuf-all-21.7.tar.gz
    cd protobuf-21.7
    mkdir build && cd build
    ../configure
    cd ..
    mkdir build_arm64 && cd build_arm64
    CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++ \
        ../configure --host=aarch64-linux --prefix=/usr/local/aarch64-linux-gnu
    cd ../..
fi
make install -j`nproc` -C protobuf-21.7/build
make install -j`nproc` -C protobuf-21.7/build_arm64

if [ ! -d can-isotp ]; then
    git clone https://github.com/hartkopp/can-isotp.git
    cd can-isotp
    git checkout beb4650660179963a8ed5b5cbf2085cc1b34f608
    cd ..
fi
cp can-isotp/include/uapi/linux/can/isotp.h /usr/include/linux/can

if [ ! -d curl-7.86.0 ]; then
    wget -q https://github.com/curl/curl/releases/download/curl-7_86_0/curl-7.86.0.tar.gz
    tar -zxf curl-7.86.0.tar.gz
    cd curl-7.86.0
    mkdir build && cd build
    LDFLAGS="-static" PKG_CONFIG="pkg-config --static" CC=aarch64-linux-gnu-gcc ../configure \
        --disable-shared --enable-static --disable-ldap --enable-ipv6 --with-ssl --disable-unix-sockets \
        --disable-rtsp --host=aarch64-linux --prefix=/usr/local/aarch64-linux-gnu
    cd ../..
fi
make install -j`nproc` -C curl-7.86.0/build V=1 LDFLAGS="-static"

if [ ! -d aws-sdk-cpp ]; then
    git clone -b 1.9.253 --recursive https://github.com/aws/aws-sdk-cpp.git
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
    cd ../..
fi
make install -j`nproc` -C aws-sdk-cpp/build

if [ ! -d googletest ]; then
    git clone -b release-1.10.0 https://github.com/google/googletest.git
    cd googletest
    mkdir build && cd build
    cmake \
        -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
        -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
        ..
    cd ../.. 
fi
make install -j`nproc` -C googletest/build

if [ ! -d benchmark ]; then
    git clone -b v1.6.1 https://github.com/google/benchmark.git
    cd benchmark
    mkdir build && cd build
    cmake \
        -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
        -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
        -DBENCHMARK_DOWNLOAD_DEPENDENCIES=on \
        -DCMAKE_BUILD_TYPE=Release \
        ..
    cd ../.. 
fi
make install -j`nproc` -C benchmark/build

# AWS IoT FleetWise Edge camera support requires Fast-DDS and its dependencies:
if [ "${WITH_CAMERA_SUPPORT}" == "true" ]; then
    apt install -y \
        default-jre \
        libasio-dev \
        qemu-user-binfmt

    if [ ! -d tinyxml2 ]; then
        git clone -b 6.0.0 https://github.com/leethomason/tinyxml2.git
        cd tinyxml2
        mkdir build && cd build
        cmake \
            -DBUILD_SHARED_LIBS=OFF \
            -DBUILD_STATIC_LIBS=ON \
            -DBUILD_TESTING=OFF \
            -DCMAKE_POSITION_INDEPENDENT_CODE=On \
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
            ..
        cd ../..
    fi
    make install -j`nproc` -C tinyxml2/build

    if [ ! -d foonathan_memory_vendor ]; then
        git clone -b v1.1.0 https://github.com/eProsima/foonathan_memory_vendor.git
        cd foonathan_memory_vendor
        mkdir build && cd build
        cmake \
            -DBUILD_SHARED_LIBS=OFF \
            -Dextra_cmake_args="-DCMAKE_CROSSCOMPILING_EMULATOR=qemu-aarch64" \
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
            ..
        cd ../..
    fi
    make install -j`nproc` -C foonathan_memory_vendor/build

    if [ ! -d Fast-CDR ]; then
        git clone -b v1.0.21 https://github.com/eProsima/Fast-CDR.git
        cd Fast-CDR
        mkdir build && cd build
        cmake \
            -DBUILD_SHARED_LIBS=OFF \
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
            ..
        cd ../..
    fi
    make install -j`nproc` -C Fast-CDR/build

    if [ ! -d Fast-DDS ]; then
        git clone -b v2.3.4 https://github.com/eProsima/Fast-DDS.git
        cd Fast-DDS
        mkdir build && cd build
        cmake \
            -DBUILD_SHARED_LIBS=OFF \
            -DCOMPILE_TOOLS=OFF \
            -DCMAKE_CXX_FLAGS="-DUSE_FOONATHAN_NODE_SIZES=1" \
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
            ..
        cd ../..
    fi
    make install -j`nproc` -C Fast-DDS/build

    if [ ! -d Fast-DDS-Gen ]; then
        git clone -b v2.0.1 --recursive https://github.com/eProsima/Fast-DDS-Gen.git
        cd Fast-DDS-Gen
        ./gradlew assemble
        cd ..
    fi
    mkdir -p /usr/local/share/fastddsgen/java
    cp Fast-DDS-Gen/share/fastddsgen/java/fastddsgen.jar /usr/local/share/fastddsgen/java
    cp Fast-DDS-Gen/scripts/fastddsgen /usr/local/bin
fi

ldconfig
