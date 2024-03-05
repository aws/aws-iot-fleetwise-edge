#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
source ${SCRIPT_DIR}/install-deps-versions.sh

USE_CACHE="true"
WITH_GREENGRASSV2_SUPPORT="false"
SHARED_LIBS="OFF"
WITH_VISION_SYSTEM_DATA="false"
WITH_ROS2_SUPPORT="false"

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
        --native-prefix)
            NATIVE_PREFIX="$2"
            USE_CACHE="false"
            shift
            ;;
        --shared-libs)
            SHARED_LIBS="ON"
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --with-greengrassv2-support  Install dependencies for Greengrass V2"
            echo "  --with-ros2-support          Install dependencies for ROS2 support"
            echo "  --native-prefix              Native install prefix"
            echo "  --shared-libs                Build shared libs, rather than static libs"
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

print_file() {
    echo ">>> $1: $2"
    cat $2
    echo ">>>"
}

print_file "Before patching" /etc/apt/sources.list
sed -i -E "s/deb (http|mirror\+file)/deb [arch=${ARCH}] \1/g" /etc/apt/sources.list
cp /etc/apt/sources.list /etc/apt/sources.list.d/arm64.list
sed -i -E "s/deb \[arch=${ARCH}\] (http|mirror\+file)/deb [arch=arm64] \1/g" /etc/apt/sources.list.d/arm64.list
# GitHub uses a separate mirrors file
if [ -f /etc/apt/apt-mirrors.txt ]; then
    print_file "Before patching" /etc/apt/apt-mirrors.txt
    cp /etc/apt/apt-mirrors.txt /etc/apt/apt-mirrors-arm64.txt
    sed -i "s#/etc/apt/apt-mirrors.txt#/etc/apt/apt-mirrors-arm64.txt#g" /etc/apt/sources.list.d/arm64.list
    PATCH_FILE="/etc/apt/apt-mirrors-arm64.txt"
    print_file "After patching" /etc/apt/apt-mirrors-arm64.txt
else
    PATCH_FILE="/etc/apt/sources.list.d/arm64.list"
fi
sed -i -E "s#(archive|security).ubuntu.com/ubuntu#ports.ubuntu.com/ubuntu-ports#g" ${PATCH_FILE}
print_file "After patching" /etc/apt/sources.list.d/arm64.list

dpkg --add-architecture arm64
apt update
apt install -y \
    build-essential
apt install -y \
    cmake \
    crossbuild-essential-arm64 \
    curl \
    git \
    libsnappy-dev:arm64 \
    libssl-dev:arm64 \
    unzip \
    wget \
    zlib1g-dev:arm64

if ${WITH_ROS2_SUPPORT}; then
    wget -q https://raw.githubusercontent.com/ros/rosdistro/master/ros.key -O /usr/share/keyrings/ros-archive-keyring.gpg
    echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu $(source /etc/os-release && echo $UBUNTU_CODENAME) main" > /etc/apt/sources.list.d/ros2.list
    apt update
    apt install -y \
        bison \
        python3-numpy \
        python3-lark \
        libasio-dev \
        libacl1-dev:arm64 \
        liblog4cxx-dev:arm64 \
        libpython3-dev:arm64 \
        python3-colcon-common-extensions
fi

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
    mkdir deps-cross-arm64
    cd deps-cross-arm64

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
    echo "using gcc : arm64 : aarch64-linux-gnu-g++ ;" > user-config.jam
    ./b2 \
        --with-atomic \
        --with-system \
        --with-thread \
        --with-filesystem \
        --with-program_options \
        --prefix=/usr/local/aarch64-linux-gnu \
        ${BOOST_OPTIONS} \
        --user-config=user-config.jam \
        -j`nproc` \
        toolset=gcc-arm64 \
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
        -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
        -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
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
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
            ..
        make install -j`nproc`
        cd ../..
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
        -DCMAKE_INSTALL_PREFIX=${NATIVE_PREFIX} \
        ..
    make install -j`nproc`
    cd ..
    mkdir build_arm64 && cd build_arm64
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
        -DCMAKE_POSITION_INDEPENDENT_CODE=On \
        -Dprotobuf_BUILD_TESTS=Off \
        -Dprotobuf_BUILD_PROTOC_BINARIES=Off \
        -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
        -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
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
        --host=aarch64-linux
        --without-ca-bundle
        --without-ca-path
        --with-ca-fallback
        --prefix=/usr/local/aarch64-linux-gnu"
    if [ "${SHARED_LIBS}" == "OFF" ]; then
        LDFLAGS="-static" PKG_CONFIG="pkg-config --static" CC=aarch64-linux-gnu-gcc \
        ../configure \
            --disable-shared \
            --enable-static \
            ${CURL_OPTIONS}
        make install -j`nproc` V=1 LDFLAGS="-static"
    else
        CC=aarch64-linux-gnu-gcc \
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
            -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
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

    git clone -b ${VERSION_AWS_SDK_CPP} --recursive https://github.com/aws/aws-sdk-cpp.git
    cd aws-sdk-cpp
    mkdir build && cd build
    if [ "${SHARED_LIBS}" == "OFF" ]; then
        AWS_SDK_CPP_OPTIONS="-DZLIB_LIBRARY=/usr/lib/aarch64-linux-gnu/libz.a -DCURL_LIBRARY=/usr/local/aarch64-linux-gnu/lib/libcurl.a"
    else
        AWS_SDK_CPP_OPTIONS=""
    fi
    cmake \
        -DENABLE_TESTING=OFF \
        -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_ONLY='transfer;s3-crt;iot' \
        -DAWS_CUSTOM_MEMORY_MANAGEMENT=ON \
        ${AWS_SDK_CPP_OPTIONS} \
        -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
        -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
        ..
    make install -j`nproc`
    cd ../..

    if ${WITH_ROS2_SUPPORT}; then
        git clone -b ${VERSION_TINYXML2} https://github.com/leethomason/tinyxml2.git
        cd tinyxml2
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
            -DBUILD_STATIC_LIBS=ON \
            -DBUILD_TESTS=OFF \
            -DCMAKE_POSITION_INDEPENDENT_CODE=On \
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
            ..
        make install -j`nproc`
        cd ../..

        git clone -b ${VERSION_FAST_CDR} https://github.com/eProsima/Fast-CDR.git
        cd Fast-CDR
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
            -DCMAKE_POSITION_INDEPENDENT_CODE=On \
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/aarch64-linux-gnu \
            ..
        make install -j`nproc`
        cd ../..

        apt install -y \
            ros-dev-tools
        git clone -b 0.8.0 https://github.com/eclipse-cyclonedds/cyclonedds.git
        cd cyclonedds
        mkdir build && cd build
        cmake ..
        make install -j`nproc`
        cp bin/ddsconf /usr/local/bin
        cd ../..
        mkdir -p ros2_build/src && cd ros2_build
        vcs import --input https://raw.githubusercontent.com/ros2/ros2/release-galactic-20221209/ros2.repos src
        rosdep init
        rosdep update
        # Without setting PythonExtra_EXTENSION_SUFFIX the .so file are aarch64 but have x86_64 in the name
        colcon build \
            --merge-install \
            --install-base /opt/ros/galactic \
            --packages-up-to rclcpp rosbag2 sensor_msgs \
            --cmake-args \
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/aarch64-linux-gnu/lib/cmake/arm64-toolchain.cmake \
            -DBUILD_TESTING=OFF \
            -DPythonExtra_EXTENSION_SUFFIX=.cpython-38-aarch64-linux-gnu \
            --no-warn-unused-cli
        cd ..
        apt remove -y ros-dev-tools
    fi

    cd ..
    rm -rf deps-cross-arm64
fi
