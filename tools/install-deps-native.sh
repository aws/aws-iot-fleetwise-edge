#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR=$(dirname $(realpath "$0"))
source ${SCRIPT_DIR}/install-deps-versions.sh

WITH_CAMERA_SUPPORT="false"
USE_CACHE="true"
INSTALL_BUILD_TIME_DEPS="true"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --with-camera-support)
            WITH_CAMERA_SUPPORT="true"
            ;;
        --prefix)
            PREFIX="$2"
            USE_CACHE="false"
            shift
            ;;
        --runtime-only)
            INSTALL_BUILD_TIME_DEPS="false"
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --with-camera-support  Install dependencies for camera support"
            echo "  --runtime-only         Install only runtime dependencies"
            echo "  --prefix               Install prefix"
            exit 0
            ;;
        esac
        shift
    done
}

parse_args "$@"

if ${INSTALL_BUILD_TIME_DEPS}; then
    apt update
    apt install -y \
        build-essential \
        clang-format-10 \
        clang-tidy-10 \
        cmake \
        doxygen \
        faketime \
        git \
        graphviz \
        libboost-log-dev \
        libboost-system-dev \
        libboost-thread-dev \
        libsnappy-dev \
        libssl-dev \
        unzip \
		wget \
        zlib1g-dev
    update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-10 1000
    update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-10 1000
fi

if ${INSTALL_BUILD_TIME_DEPS} && ${WITH_CAMERA_SUPPORT}; then
    apt install -y \
        default-jre \
        libasio-dev
fi

if ${INSTALL_BUILD_TIME_DEPS} && [ ! -f /usr/include/linux/can/isotp.h ]; then
    git clone https://github.com/hartkopp/can-isotp.git
    cd can-isotp
    git checkout beb4650660179963a8ed5b5cbf2085cc1b34f608
    cp include/uapi/linux/can/isotp.h /usr/include/linux/can
    cd ..
    rm -rf can-isotp
fi

if [ -z "${PREFIX+x}" ]; then
    PREFIX="/usr/local/`gcc -dumpmachine`"
fi

if ${INSTALL_BUILD_TIME_DEPS} && ( ! ${USE_CACHE} || [ ! -d ${PREFIX} ] ); then
    mkdir -p ${PREFIX}
    mkdir deps-native && cd deps-native

    git clone -b ${VERSION_JSON_CPP} https://github.com/open-source-parsers/jsoncpp.git
    cd jsoncpp
    mkdir build && cd build
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=On \
        -DJSONCPP_WITH_TESTS=Off \
        -DJSONCPP_WITH_POST_BUILD_UNITTEST=Off \
        -DCMAKE_INSTALL_PREFIX=${PREFIX} \
        ..
    make install -j`nproc`
    cd ../..

    wget -q https://github.com/protocolbuffers/protobuf/releases/download/${VERSION_PROTOBUF_RELEASE}/protobuf-cpp-${VERSION_PROTOBUF}.tar.gz
    tar -zxf protobuf-cpp-${VERSION_PROTOBUF}.tar.gz
    cd protobuf-${VERSION_PROTOBUF}
    mkdir build && cd build
    ../configure --prefix=${PREFIX}
    make install -j`nproc`
    cd ../..

    wget -q https://github.com/curl/curl/releases/download/${VERSION_CURL_RELEASE}/curl-${VERSION_CURL}.tar.gz
    tar -zxf curl-${VERSION_CURL}.tar.gz
    cd curl-${VERSION_CURL}
    mkdir build && cd build
    LDFLAGS="-static" PKG_CONFIG="pkg-config --static" ../configure --disable-shared --enable-static \
        --disable-ldap --enable-ipv6 --with-ssl --disable-unix-sockets --disable-rtsp --without-zstd --prefix=${PREFIX}
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
        -DZLIB_LIBRARY=/usr/lib/$(gcc -dumpmachine)/libz.a \
        -DCURL_LIBRARY=${PREFIX}/lib/libcurl.a \
        -DCMAKE_INSTALL_PREFIX=${PREFIX} \
        ..
    make install -j`nproc`
    cd ../..

    git clone -b ${VERSION_GOOGLE_TEST} https://github.com/google/googletest.git
    cd googletest
    mkdir build && cd build
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=${PREFIX} \
        ..
    make install -j`nproc`
    cd ../..

    git clone -b ${VERSION_GOOGLE_BENCHMARK} https://github.com/google/benchmark.git
    cd benchmark
    mkdir build && cd build
    cmake \
        -DBUILD_SHARED_LIBS=OFF \
        -DBENCHMARK_DOWNLOAD_DEPENDENCIES=on \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=${PREFIX} \
        ..
    make install -j`nproc`
    cd ../..

    # AWS IoT FleetWise Edge camera support requires Fast-DDS and its dependencies:
    if ${WITH_CAMERA_SUPPORT}; then
        git clone -b ${VERSION_TINYXML2} https://github.com/leethomason/tinyxml2.git
        cd tinyxml2
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=OFF \
            -DBUILD_STATIC_LIBS=ON \
            -DBUILD_TESTS=OFF \
            -DCMAKE_POSITION_INDEPENDENT_CODE=On \
            -DCMAKE_INSTALL_PREFIX=${PREFIX} \
            ..
        make install -j`nproc`
        cd ../..

        git clone -b ${VERSION_FOONATHAN_MEMORY_VENDOR} https://github.com/eProsima/foonathan_memory_vendor.git
        cd foonathan_memory_vendor
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=OFF \
            -DCMAKE_INSTALL_PREFIX=${PREFIX} \
            ..
        make install -j`nproc`
        cd ../..

        git clone -b ${VERSION_FAST_CDR} https://github.com/eProsima/Fast-CDR.git
        cd Fast-CDR
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=OFF \
            -DCMAKE_INSTALL_PREFIX=${PREFIX} \
            ..
        make install -j`nproc`
        cd ../..

        git clone -b ${VERSION_FAST_DDS} https://github.com/eProsima/Fast-DDS.git
        cd Fast-DDS
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=OFF \
            -DCOMPILE_TOOLS=OFF \
            -DCMAKE_CXX_FLAGS="-DUSE_FOONATHAN_NODE_SIZES=1" \
            -DCMAKE_INSTALL_PREFIX=${PREFIX} \
            ..
        make install -j`nproc`
        cd ../..

        git clone -b ${VERSION_FAST_DDS_GEN} --recursive https://github.com/eProsima/Fast-DDS-Gen.git
        cd Fast-DDS-Gen
        ./gradlew assemble
        mkdir -p ${PREFIX}/share/fastddsgen/java
        cp share/fastddsgen/java/fastddsgen.jar ${PREFIX}/share/fastddsgen/java
        cp scripts/fastddsgen ${PREFIX}/bin
        cd ..
    fi

    cd ..
    rm -rf deps-native
fi
