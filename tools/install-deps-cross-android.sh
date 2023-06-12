#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR=$(dirname $(realpath "$0"))
source ${SCRIPT_DIR}/install-deps-versions.sh

USE_CACHE="true"
SDK_PREFIX="/usr/local/android_sdk"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --native-prefix)
            NATIVE_PREFIX="$2"
            USE_CACHE="false"
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --native-prefix  Native install prefix"
            exit 0
            ;;
        esac
        shift
    done
}

parse_args "$@"

apt update
apt install -y \
    unzip \
    git \
    wget \
    curl \
    default-jre \
    build-essential \
    file

if [ ! -d ${SDK_PREFIX} ]; then
    curl -o cmdline-tools.zip https://dl.google.com/android/repository/commandlinetools-linux-${VERSION_ANDROID_CMDLINE_TOOLS}_latest.zip
    unzip cmdline-tools.zip
    rm cmdline-tools.zip
    mkdir -p ${SDK_PREFIX}/cmdline-tools
    mv cmdline-tools ${SDK_PREFIX}/cmdline-tools/latest
    yes | ${SDK_PREFIX}/cmdline-tools/latest/bin/sdkmanager --licenses || true
    ${SDK_PREFIX}/cmdline-tools/latest/bin/sdkmanager --install \
        "ndk;${VERSION_ANDROID_NDK}" \
        "cmake;${VERSION_CMAKE}" \
        "platforms;android-${VERSION_ANDROID_SDK}" \
        "platform-tools" \
        "emulator" \
        "build-tools;${VERSION_ANDROID_BUILD_TOOLS}"
fi
export PATH=${SDK_PREFIX}/cmake/${VERSION_CMAKE}/bin/:${PATH}

if [ -z "${NATIVE_PREFIX+x}" ]; then
    NATIVE_PREFIX="/usr/local/`gcc -dumpmachine`"
fi

install_deps() {
    TARGET_ARCH="$1"
    HOST_PLATFORM="$2"
    SSL_TARGET="$3"
    INSTALL_PREFIX="/usr/local/${HOST_PLATFORM}"

    mkdir -p ${INSTALL_PREFIX}
    mkdir -p ${NATIVE_PREFIX}
    mkdir deps-cross-android && cd deps-cross-android

    # Boost
    git clone https://github.com/moritz-wundke/Boost-for-Android.git
    cd Boost-for-Android
    git checkout 53e6c16ea80c7dcb2683fd548e0c7a09ddffbfc1
    ./build-android.sh \
        ${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK} \
        --boost=${VERSION_BOOST} \
        --with-libraries=system,thread,filesystem,chrono,date_time,atomic \
        --layout=system \
        --arch=${TARGET_ARCH} \
        --target-version=${VERSION_ANDROID_API} > /dev/null
    mv build/out/${TARGET_ARCH}/lib ${INSTALL_PREFIX}
    mv build/out/${TARGET_ARCH}/include ${INSTALL_PREFIX}
    cd ..

    # Snappy
    git clone -b ${VERSION_SNAPPY} https://github.com/google/snappy.git
    cd snappy
    mkdir build && cd build
    cmake \
        -DSNAPPY_BUILD_TESTS=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DSNAPPY_BUILD_BENCHMARKS=OFF \
        -DCMAKE_BUILD_TYPE=Release \
        -DANDROID_ABI=${TARGET_ARCH} \
        -DANDROID_PLATFORM=android-${VERSION_ANDROID_API} \
        -DCMAKE_ANDROID_NDK=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK} \
        -DCMAKE_TOOLCHAIN_FILE=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK}/build/cmake/android.toolchain.cmake \
        -DANDROID_TOOLCHAIN=clang \
        -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} \
        ..
    make install -j`nproc`
    cd ../..

    # Protobuf
    wget -q https://github.com/protocolbuffers/protobuf/releases/download/${VERSION_PROTOBUF_RELEASE}/protobuf-cpp-${VERSION_PROTOBUF}.tar.gz
    tar -zxf protobuf-cpp-${VERSION_PROTOBUF}.tar.gz
    cd protobuf-${VERSION_PROTOBUF}
    if [ ! -f ${NATIVE_PREFIX}/bin/protoc ]; then
        mkdir build_native && cd build_native
        ../configure --prefix=${NATIVE_PREFIX}
        make install -j`nproc`
        cd ..
    fi
    mkdir build_target && cd build_target
    NDK=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK} \
        TOOLCHAIN=${NDK}/toolchains/llvm/prebuilt/linux-x86_64 \
        TARGET=${HOST_PLATFORM} \
        API=${VERSION_ANDROID_API} \
        AR=${TOOLCHAIN}/bin/llvm-ar \
        CC=${TOOLCHAIN}/bin/${TARGET}${API}-clang \
        AS=${CC} \
        CXX=${TOOLCHAIN}/bin/${TARGET}${API}-clang++ \
        LD=${TOOLCHAIN}/bin/ld \
        RANLIB=${TOOLCHAIN}/bin/llvm-ranlib \
        STRIP=${TOOLCHAIN}/bin/llvm-strip \
        ../configure \
            --host=${HOST_PLATFORM} \
            --prefix=${INSTALL_PREFIX} \
            "CFLAGS=-fPIC" "CXXFLAGS=-fPIC"
    make install -j`nproc`
    cd ../..

    # JsonCpp
    git clone -b ${VERSION_JSON_CPP} https://github.com/open-source-parsers/jsoncpp.git
    cd jsoncpp
    mkdir build && cd build
    cmake \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=On \
        -DJSONCPP_WITH_TESTS=Off \
        -DJSONCPP_WITH_POST_BUILD_UNITTEST=Off \
        -DANDROID_ABI=${TARGET_ARCH} \
        -DANDROID_PLATFORM=android-${VERSION_ANDROID_API} \
        -DCMAKE_ANDROID_NDK=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK} \
        -DCMAKE_TOOLCHAIN_FILE=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK}/build/cmake/android.toolchain.cmake \
        -DANDROID_TOOLCHAIN=clang \
        -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} \
        ..
    make install -j`nproc`
    cd ../..

    # OpenSSL
    wget -q https://www.openssl.org/source/openssl-${VERSION_OPENSSL}.tar.gz
    tar -zxf openssl-${VERSION_OPENSSL}.tar.gz
    cd openssl-${VERSION_OPENSSL}
    ANDROID_NDK_HOME=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK} \
        PATH=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/bin:${PATH} \
        INSTALL_PREFIX=${INSTALL_PREFIX} SSL_TARGET=${SSL_TARGET} VERSION_ANDROID_API=${VERSION_ANDROID_API} \
        bash -c './Configure ${SSL_TARGET} -D__ANDROID_API__=${VERSION_ANDROID_API} --prefix=${INSTALL_PREFIX} no-shared \
            && make -j`nproc`'
    make install > /dev/null
    cd ..

    # curl
    wget -q https://github.com/curl/curl/releases/download/${VERSION_CURL_RELEASE}/curl-${VERSION_CURL}.tar.gz
    tar -zxf curl-${VERSION_CURL}.tar.gz
    cd curl-${VERSION_CURL}
    NDK=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK} \
        TOOLCHAIN=${NDK}/toolchains/llvm/prebuilt/linux-x86_64 \
        TARGET=${HOST_PLATFORM} \
        API=${VERSION_ANDROID_API} \
        AR=${TOOLCHAIN}/bin/llvm-ar \
        CC=${TOOLCHAIN}/bin/${TARGET}${API}-clang \
        AS=${CC} \
        CXX=${TOOLCHAIN}/bin/${TARGET}${API}-clang++ \
        LD=${TOOLCHAIN}/bin/ld \
        RANLIB=${TOOLCHAIN}/bin/llvm-ranlib \
        STRIP=${TOOLCHAIN}/bin/llvm-strip \
        LDFLAGS="-static" \
        PKG_CONFIG="pkg-config --static" \
        ./configure \
            --disable-shared \
            --enable-static \
            --disable-ldap \
            --enable-ipv6 \
            --with-ssl=${INSTALL_PREFIX} \
            --disable-unix-sockets \
            --disable-rtsp \
            --host=${HOST_PLATFORM} \
            --prefix=${INSTALL_PREFIX}
    make install -j`nproc` V=1 LDFLAGS="-static -L${INSTALL_PREFIX}/lib"
    cd ..

    # AWS C++ SDK
    git clone -b ${VERSION_AWS_SDK_CPP} --recursive https://github.com/aws/aws-sdk-cpp.git
    cd aws-sdk-cpp
    mkdir build && cd build
    CFLAGS=-I${INSTALL_PREFIX}/include cmake \
        -DENABLE_TESTING=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_ONLY='s3-crt;iot' \
        -DAWS_CUSTOM_MEMORY_MANAGEMENT=ON \
        -DANDROID_ABI=${TARGET_ARCH} \
        -DANDROID_PLATFORM=android-${VERSION_ANDROID_API} \
        -DCMAKE_ANDROID_NDK=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK} \
        -DCMAKE_TOOLCHAIN_FILE=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK}/build/cmake/android.toolchain.cmake \
        -DANDROID_TOOLCHAIN=clang \
        -DCURL_LIBRARY=${INSTALL_PREFIX}/lib/libcurl.a \
        -DCMAKE_FIND_ROOT_PATH=${INSTALL_PREFIX} \
        -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} \
        ..
    make install -j`nproc`
    cd ../..
    # pthread is directly linked somewhere, so just create a dummy .a file
    ar -rc ${INSTALL_PREFIX}/lib/libpthread.a

    cd ..
    rm -rf deps-cross-android
}

if ! ${USE_CACHE} || [ ! -d /usr/local/aarch64-linux-android ]  || [ ! -d /usr/local/armv7a-linux-androideabi ] || [ ! -d ${NATIVE_PREFIX} ]; then
    install_deps "armeabi-v7a" "armv7a-linux-androideabi" "android-arm"
    install_deps "arm64-v8a" "aarch64-linux-android" "android-arm64"
fi
