#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
source ${SCRIPT_DIR}/install-deps-versions.sh

USE_CACHE="false"
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
        --native-prefix)
            NATIVE_PREFIX="$2"
            shift
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
            echo "  --native-prefix <PREFIX>             Native install prefix"
            echo "  --shared-libs                        Build shared libs, rather than static libs"
            exit 0
            ;;
        esac
        shift
    done
}

parse_args "$@"

ARCH=`dpkg --print-architecture`
if [ "${ARCH}" == "armhf" ]; then
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
print_file "After patching" /etc/apt/sources.list

cp ${SCRIPT_DIR}/armhf.list /etc/apt/sources.list.d/
dpkg --add-architecture armhf
apt-get update
apt-get install -y \
    build-essential \
    cmake \
    crossbuild-essential-armhf \
    curl \
    git \
    libsnappy-dev:armhf \
    libssl-dev:armhf \
    unzip \
    wget \
    zlib1g-dev:armhf

if ${WITH_ROS2_SUPPORT}; then
    wget -q https://raw.githubusercontent.com/ros/rosdistro/master/ros.key -O /usr/share/keyrings/ros-archive-keyring.gpg
    echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu $(source /etc/os-release && echo $UBUNTU_CODENAME) main" > /etc/apt/sources.list.d/ros2.list
    apt-get update
    apt-get install -y \
        bison \
        python3-numpy \
        python3-lark \
        libasio-dev \
        libacl1-dev:armhf \
        liblog4cxx-dev:armhf \
        libpython3-dev:armhf \
        python3-colcon-common-extensions \
        ros-dev-tools
fi

if ${WITH_SOMEIP_SUPPORT}; then
    apt-get install -y \
        default-jre \
        python3-distutils \
        libpython3-dev:armhf
fi

if ${WITH_CPYTHON_SUPPORT}; then
    apt-get install -y \
        libpython3-dev:armhf
fi

if [ ! -f /usr/include/linux/can/isotp.h ]; then
    git clone https://github.com/hartkopp/can-isotp.git
    cd can-isotp
    git checkout beb4650660179963a8ed5b5cbf2085cc1b34f608
    cp include/uapi/linux/can/isotp.h /usr/include/linux/can
    cd ..
    rm -rf can-isotp
fi

: ${NATIVE_PREFIX:="/usr/local"}

if ! ${USE_CACHE}; then
    mkdir -p /usr/local/arm-linux-gnueabihf/lib/cmake/
    mkdir -p ${NATIVE_PREFIX}
    cp ${SCRIPT_DIR}/armhf-toolchain.cmake /usr/local/arm-linux-gnueabihf/lib/cmake/
    mkdir deps-cross-armhf
    cd deps-cross-armhf

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
    echo "using gcc : armhf : arm-linux-gnueabihf-g++ ;" > user-config.jam
    ./b2 \
        --with-atomic \
        --with-system \
        --with-thread \
        --with-chrono \
        --with-filesystem \
        --with-program_options \
        --prefix=/usr/local/arm-linux-gnueabihf \
        ${BOOST_OPTIONS} \
        --user-config=user-config.jam \
        -j`nproc` \
        toolset=gcc-armhf \
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
        -DCMAKE_TOOLCHAIN_FILE=/usr/local/arm-linux-gnueabihf/lib/cmake/armhf-toolchain.cmake \
        -DCMAKE_INSTALL_PREFIX=/usr/local/arm-linux-gnueabihf \
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
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/arm-linux-gnueabihf/lib/cmake/armhf-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/arm-linux-gnueabihf \
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
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/arm-linux-gnueabihf/lib/cmake/armhf-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/arm-linux-gnueabihf \
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
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/arm-linux-gnueabihf/lib/cmake/armhf-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/arm-linux-gnueabihf \
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
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/arm-linux-gnueabihf/lib/cmake/armhf-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/arm-linux-gnueabihf \
            ..
        make install -j`nproc`
        cd ../..

        wget -q https://github.com/COVESA/capicxx-core-tools/releases/download/${VERSION_COMMON_API_GENERATOR}/commonapi_core_generator.zip
        mkdir -p ${NATIVE_PREFIX}/share/commonapi-core-generator
        unzip -q -o commonapi_core_generator.zip -d ${NATIVE_PREFIX}/share/commonapi-core-generator
        wget -q https://github.com/COVESA/capicxx-someip-tools/releases/download/${VERSION_COMMON_API_GENERATOR}/commonapi_someip_generator.zip
        mkdir -p ${NATIVE_PREFIX}/share/commonapi-someip-generator
        unzip -q -o commonapi_someip_generator.zip -d ${NATIVE_PREFIX}/share/commonapi-someip-generator

        git clone -b ${VERSION_PYBIND11} https://github.com/pybind/pybind11.git
        cd pybind11
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
            -DCMAKE_POSITION_INDEPENDENT_CODE=On \
            -DBUILD_TESTING=Off \
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/arm-linux-gnueabihf/lib/cmake/armhf-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/arm-linux-gnueabihf \
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
            -DSTORE_USE_LTO=OFF \
            -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
            -DCMAKE_POSITION_INDEPENDENT_CODE=On \
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/arm-linux-gnueabihf/lib/cmake/armhf-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/arm-linux-gnueabihf \
            -DSTORE_BUILD_EXAMPLE=OFF \
            ..
        make install -j`nproc`
        cd ../..
    fi

    if ${WITH_MICROPYTHON_SUPPORT}; then
        git clone -b ${VERSION_MICROPYTHON} https://github.com/micropython/micropython.git
        mkdir -p /usr/local/arm-linux-gnueabihf/src
        cp -r micropython /usr/local/arm-linux-gnueabihf/src
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
    mkdir build_armhf && cd build_armhf
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
        -DCMAKE_POSITION_INDEPENDENT_CODE=On \
        -Dprotobuf_BUILD_TESTS=Off \
        -Dprotobuf_BUILD_PROTOC_BINARIES=Off \
        -DCMAKE_TOOLCHAIN_FILE=/usr/local/arm-linux-gnueabihf/lib/cmake/armhf-toolchain.cmake \
        -DCMAKE_INSTALL_PREFIX=/usr/local/arm-linux-gnueabihf \
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
        --host=arm-linux
        --without-ca-bundle
        --without-ca-path
        --with-ca-fallback
        --without-brotli
        --prefix=/usr/local/arm-linux-gnueabihf"
    if [ "${SHARED_LIBS}" == "OFF" ]; then
        LDFLAGS="-static" PKG_CONFIG="pkg-config --static" CC=arm-linux-gnueabihf-gcc PKG_CONFIG_LIBDIR=/usr/lib/arm-linux-gnueabihf \
        ../configure \
            --disable-shared \
            --enable-static \
            ${CURL_OPTIONS}
        make install -j`nproc` V=1 LDFLAGS="-static"
    else
        CC=arm-linux-gnueabihf-gcc PKG_CONFIG_LIBDIR=/usr/lib/arm-linux-gnueabihf \
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
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/arm-linux-gnueabihf/lib/cmake/armhf-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/arm-linux-gnueabihf \
            ..
        make install -j`nproc`
        cd ../..
    fi

    git clone -b ${VERSION_AWS_SDK_CPP} --recursive https://github.com/aws/aws-sdk-cpp.git
    cd aws-sdk-cpp
    mkdir build && cd build
    if [ "${SHARED_LIBS}" == "OFF" ]; then
        AWS_SDK_CPP_OPTIONS="-DZLIB_LIBRARY=/usr/lib/arm-linux-gnueabihf/libz.a -DCURL_LIBRARY=/usr/local/arm-linux-gnueabihf/lib/libcurl.a"
    else
        AWS_SDK_CPP_OPTIONS=""
    fi
    cmake \
        -DENABLE_TESTING=OFF \
        -DBUILD_SHARED_LIBS=${SHARED_LIBS} \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_ONLY='transfer;s3-crt;iot' \
        ${AWS_SDK_CPP_OPTIONS} \
        -DCMAKE_TOOLCHAIN_FILE=/usr/local/arm-linux-gnueabihf/lib/cmake/armhf-toolchain.cmake \
        -DCMAKE_INSTALL_PREFIX=/usr/local/arm-linux-gnueabihf \
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
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/arm-linux-gnueabihf/lib/cmake/armhf-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/arm-linux-gnueabihf \
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
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/arm-linux-gnueabihf/lib/cmake/armhf-toolchain.cmake \
            -DCMAKE_INSTALL_PREFIX=/usr/local/arm-linux-gnueabihf \
            ..
        make install -j`nproc`
        cd ../..

        mkdir -p ros2_build/src && cd ros2_build
        vcs import --input https://raw.githubusercontent.com/ros2/ros2/release-humble-20241205/ros2.repos src
        rosdep init
        rosdep update
        # Without setting PythonExtra_EXTENSION_SUFFIX the .so file are armhf but have x86_64 in the name
        colcon build \
            --merge-install \
            --install-base /opt/ros/humble \
            --packages-up-to rclcpp rosbag2 sensor_msgs \
            --cmake-args \
            -DCMAKE_TOOLCHAIN_FILE=/usr/local/arm-linux-gnueabihf/lib/cmake/armhf-toolchain.cmake \
            -DBUILD_TESTING=OFF \
            -DPythonExtra_EXTENSION_SUFFIX=.cpython-310-arm-linux-gnueabihf \
            --no-warn-unused-cli
        cd ..
        apt-get remove -y ros-dev-tools
    fi

    cd ..
    rm -rf deps-cross-armhf
fi
