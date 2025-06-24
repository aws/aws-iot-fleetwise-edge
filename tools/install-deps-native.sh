#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
source ${SCRIPT_DIR}/install-deps-versions.sh

USE_CACHE="false"
INSTALL_BUILD_TIME_DEPS="true"
WITH_GREENGRASSV2_SUPPORT="false"
SHARED_LIBS="OFF"
WITH_VISION_SYSTEM_DATA="false"
WITH_ROS2_SUPPORT="false"
WITH_SOMEIP_SUPPORT="false"
WITH_STORE_AND_FORWARD_SUPPORT="false"
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
            WITH_VISION_SYSTEM_DATA="true"
            ;;
        --with-someip-support)
            WITH_SOMEIP_SUPPORT="true"
            ;;
        --with-store-and-forward-support)
            WITH_STORE_AND_FORWARD_SUPPORT="true"
            ;;
        --with-cpython-support)
            WITH_CPYTHON_SUPPORT="true"
            ;;
        --with-micropython-support)
            WITH_MICROPYTHON_SUPPORT="true"
            ;;
        --prefix)
            PREFIX="$2"
            shift
            ;;
        --runtime-only)
            INSTALL_BUILD_TIME_DEPS="false"
            ;;
        --shared-libs)
            SHARED_LIBS="ON"
            ;;
        --use-cache)
            USE_CACHE="true"
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --with-greengrassv2-support          Install dependencies for Greengrass V2"
            echo "  --with-ros2-support                  Install dependencies for ROS2 support"
            echo "  --with-someip-support                Install dependencies for SOME/IP support"
            echo "  --with-store-and-forward-support     Install dependencies for store and forward"
            echo "  --with-cpython-support               Install dependencies for CPython support"
            echo "  --with-micropython-support           Install dependencies for MicroPython support"
            echo "  --runtime-only                       Install only runtime dependencies"
            echo "  --prefix                             Install prefix"
            echo "  --shared-libs                        Build shared libs, rather than static libs"
            exit 0
            ;;
        esac
        shift
    done
}

parse_args "$@"

if ${INSTALL_BUILD_TIME_DEPS}; then
    apt-get update
    apt-get install -y \
        build-essential \
        clang-tidy-12 \
        cmake \
        doxygen \
        git \
        graphviz \
        libsnappy-dev \
        libssl-dev \
        parallel \
        python3 \
        python3-pip \
        unzip \
        wget \
        zlib1g-dev
    git clone -b ${VERSION_FAKETIME} https://github.com/wolfcw/libfaketime.git
    cd libfaketime
    make install -j `nproc`
    cd ..
    update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-12 1000
    python3 -m pip install -r ${SCRIPT_DIR}/requirements-unit-test.txt
fi

if ${WITH_ROS2_SUPPORT}; then
    command -v wget > /dev/null || ( apt-get update && apt-get install -y wget)
    wget -q https://raw.githubusercontent.com/ros/rosdistro/master/ros.key -O /usr/share/keyrings/ros-archive-keyring.gpg
    echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu $(source /etc/os-release && echo $UBUNTU_CODENAME) main" > /etc/apt/sources.list.d/ros2.list
    apt-get update
    apt-get install -y \
        ros-humble-rclcpp \
        ros-humble-rosbag2 \
        ros-humble-ros2topic \
        ros-humble-sensor-msgs \
        ros-humble-rmw-cyclonedds-cpp
    if ${INSTALL_BUILD_TIME_DEPS}; then
        apt-get install -y \
            ros-humble-rosidl-default-generators \
            python3-colcon-common-extensions
    fi
fi

if ${WITH_SOMEIP_SUPPORT}; then
    apt-get install -y \
        default-jre \
        python3-distutils \
        libpython3-dev
fi

if ${WITH_CPYTHON_SUPPORT}; then
    apt-get install -y \
        libpython3-dev
fi

if ${INSTALL_BUILD_TIME_DEPS} && [ ! -f /usr/include/linux/can/isotp.h ]; then
    git clone https://github.com/hartkopp/can-isotp.git
    cd can-isotp
    git checkout beb4650660179963a8ed5b5cbf2085cc1b34f608
    cp include/uapi/linux/can/isotp.h /usr/include/linux/can
    cd ..
    rm -rf can-isotp
fi

: ${PREFIX:="/usr/local"}

if ${INSTALL_BUILD_TIME_DEPS} && ( ! ${USE_CACHE} ); then
    mkdir -p ${PREFIX}
    mkdir deps-native
    cd deps-native

    wget -q https://archives.boost.io/release/${VERSION_BOOST}/source/boost_${VERSION_BOOST//./_}.tar.bz2
    tar -jxf boost_${VERSION_BOOST//./_}.tar.bz2
    cd boost_${VERSION_BOOST//./_}
    ./bootstrap.sh
    # Build static libraries with -fPIC enabled, so they can later be linked into shared libraries
    # (The Ubuntu static libraries are not built with -fPIC)
    if [ "${SHARED_LIBS}" == "OFF" ]; then
        BOOST_OPTIONS="cxxflags=-fPIC cflags=-fPIC link=static"
    else
        BOOST_OPTIONS=""
    fi
    ./b2 \
        --with-atomic \
        --with-system \
        --with-thread \
        --with-chrono \
        --with-filesystem \
        --with-program_options \
        --prefix=${PREFIX} \
        ${BOOST_OPTIONS} \
        -j`nproc` \
        install
    cd ..

    git clone -b ${VERSION_JSON_CPP} https://github.com/open-source-parsers/jsoncpp.git
    cd jsoncpp
    mkdir build && cd build
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
        -DCMAKE_POSITION_INDEPENDENT_CODE=On \
        -DJSONCPP_WITH_TESTS=Off \
        -DJSONCPP_WITH_POST_BUILD_UNITTEST=Off \
        -DCMAKE_INSTALL_PREFIX=${PREFIX} \
        ..
    make install -j`nproc`
    cd ../..

    if ${WITH_VISION_SYSTEM_DATA}; then
        git clone https://github.com/amazon-ion/ion-c.git
        cd ion-c
        git checkout ${VERSION_ION_C}
        git submodule update --init
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
            -DCMAKE_INSTALL_PREFIX=${PREFIX} \
            ..
        make install -j`nproc`
        cd ../..
    fi

    if ${WITH_ROS2_SUPPORT}; then
        git clone -b ${VERSION_FAST_CDR} https://github.com/eProsima/Fast-CDR.git
        cd Fast-CDR
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
            -DCMAKE_INSTALL_PREFIX=${PREFIX} \
            ..
        make install -j`nproc`
        cd ../..
    fi

    if ${WITH_SOMEIP_SUPPORT}; then
        git clone -b ${VERSION_VSOMEIP} https://github.com/COVESA/vsomeip.git
        cd vsomeip
        # https://github.com/COVESA/vsomeip/pull/528
        # https://github.com/COVESA/vsomeip/pull/789
        git apply ${SCRIPT_DIR}/patches/vsomeip_allow_static_libs_fix_shutdown_segfaults.patch
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
            -DCMAKE_POSITION_INDEPENDENT_CODE=On \
            -DCMAKE_INSTALL_PREFIX=${PREFIX} \
            ..
        make install -j`nproc`
        cd ../..

        git clone -b ${VERSION_COMMON_API} https://github.com/COVESA/capicxx-core-runtime.git
        cd capicxx-core-runtime
        # https://github.com/COVESA/capicxx-core-runtime/pull/42
        git apply ${SCRIPT_DIR}/patches/capicxx_core_runtime_allow_static_libs.patch
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
            -DCMAKE_POSITION_INDEPENDENT_CODE=On \
            -DCMAKE_INSTALL_PREFIX=${PREFIX} \
            ..
        make install -j`nproc`
        cd ../..

        git clone -b ${VERSION_COMMON_API_SOMEIP} https://github.com/COVESA/capicxx-someip-runtime.git
        cd capicxx-someip-runtime
        # https://github.com/COVESA/capicxx-someip-runtime/pull/27
        git apply ${SCRIPT_DIR}/patches/capicxx_someip_runtime_allow_static_libs.patch
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
            -DCMAKE_POSITION_INDEPENDENT_CODE=On \
            -DCMAKE_INSTALL_PREFIX=${PREFIX} \
            ..
        make install -j`nproc`
        cd ../..

        wget -q https://github.com/COVESA/capicxx-core-tools/releases/download/${VERSION_COMMON_API_GENERATOR}/commonapi_core_generator.zip
        mkdir -p ${PREFIX}/share/commonapi-core-generator
        unzip -q -o commonapi_core_generator.zip -d ${PREFIX}/share/commonapi-core-generator
        wget -q https://github.com/COVESA/capicxx-someip-tools/releases/download/${VERSION_COMMON_API_GENERATOR}/commonapi_someip_generator.zip
        mkdir -p ${PREFIX}/share/commonapi-someip-generator
        unzip -q -o commonapi_someip_generator.zip -d ${PREFIX}/share/commonapi-someip-generator

        git clone -b ${VERSION_PYBIND11} https://github.com/pybind/pybind11.git
        cd pybind11
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
            -DCMAKE_POSITION_INDEPENDENT_CODE=On \
            -DBUILD_TESTING=Off \
            -DCMAKE_INSTALL_PREFIX=${PREFIX} \
            ..
        make install -j`nproc`
        cd ../..
    fi

    if ${WITH_STORE_AND_FORWARD_SUPPORT}; then
        git clone -b ${VERSION_DEVICE_STORELIBRARY_CPP} https://github.com/aws/device-storelibrary-cpp.git
        cd device-storelibrary-cpp
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DSTORE_BUILD_TESTS=OFF \
            -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
            -DCMAKE_POSITION_INDEPENDENT_CODE=On \
            -DCMAKE_INSTALL_PREFIX=${PREFIX} \
            -DSTORE_BUILD_EXAMPLE=OFF \
            ..
        make install -j`nproc`
        cd ../..
    fi

    if ${WITH_MICROPYTHON_SUPPORT}; then
        git clone -b ${VERSION_MICROPYTHON} https://github.com/micropython/micropython.git
        mkdir -p ${PREFIX}/src
        cp -r micropython ${PREFIX}/src
    fi

    wget -q https://github.com/protocolbuffers/protobuf/releases/download/${VERSION_PROTOBUF_RELEASE}/protobuf-cpp-${VERSION_PROTOBUF}.tar.gz
    tar -zxf protobuf-cpp-${VERSION_PROTOBUF}.tar.gz
    cd protobuf-${VERSION_PROTOBUF}
    mkdir build && cd build
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
        -DCMAKE_POSITION_INDEPENDENT_CODE=On \
        -Dprotobuf_BUILD_TESTS=Off \
        -DCMAKE_INSTALL_PREFIX=${PREFIX} \
        ..
    make install -j`nproc`
    cd ../..

    wget -q https://github.com/curl/curl/releases/download/${VERSION_CURL_RELEASE}/curl-${VERSION_CURL}.tar.gz
    tar -zxf curl-${VERSION_CURL}.tar.gz
    cd curl-${VERSION_CURL}
    mkdir build && cd build
    CURL_OPTIONS="
        --disable-ldap
        --enable-ipv6
        --with-ssl
        --disable-unix-sockets
        --disable-rtsp
        --without-zstd
        --without-ca-bundle
        --without-ca-path
        --with-ca-fallback
        --without-brotli
        --prefix=${PREFIX}"
    if [ "${SHARED_LIBS}" == "OFF" ]; then
        LDFLAGS="-static" PKG_CONFIG="pkg-config --static" \
        ../configure \
            --disable-shared \
            --enable-static \
            ${CURL_OPTIONS}
        make install -j`nproc` V=1 LDFLAGS="-static"
    else
        ../configure \
            --enable-shared \
            --disable-static \
            ${CURL_OPTIONS}
        make install -j`nproc` V=1
    fi
    cd ../..

    # Build AWS IoT Device SDK before AWS SDK because they both include aws-crt-cpp as submodules.
    # And although we make sure both versions match, we want the AWS SDK version to prevail so that
    # we always use the same aws-crt-cpp regardless whether Greengrass is enabled.
    if ${WITH_GREENGRASSV2_SUPPORT}; then
        git clone -b ${VERSION_AWS_IOT_DEVICE_SDK_CPP_V2} --recursive https://github.com/aws/aws-iot-device-sdk-cpp-v2.git
        cd aws-iot-device-sdk-cpp-v2
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
            -DBUILD_DEPS=ON \
            -DBUILD_TESTING=OFF \
            -DUSE_OPENSSL=ON \
            -DBUILD_ONLY='greengrass_ipc' \
            -DCMAKE_INSTALL_PREFIX=${PREFIX} \
            ..
        make install -j`nproc`
        cd ../..
    fi

    git clone -b ${VERSION_AWS_SDK_CPP} --recursive https://github.com/aws/aws-sdk-cpp.git
    cd aws-sdk-cpp
    mkdir build && cd build
    if [ "${SHARED_LIBS}" == "OFF" ]; then
        AWS_SDK_CPP_OPTIONS="-DZLIB_LIBRARY=/usr/lib/$(gcc -dumpmachine)/libz.a -DCURL_LIBRARY=${PREFIX}/lib/libcurl.a"
    else
        AWS_SDK_CPP_OPTIONS=""
    fi
    cmake \
        -DENABLE_TESTING=OFF \
        -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_ONLY='transfer;s3-crt;iot' \
        ${AWS_SDK_CPP_OPTIONS} \
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
        -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
        -DBENCHMARK_DOWNLOAD_DEPENDENCIES=on \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=${PREFIX} \
        ..
    make install -j`nproc`
    cd ../..

    cd ..
    rm -rf deps-native

    if [ "${SHARED_LIBS}" == "ON" ]; then
        ldconfig
    fi
fi
