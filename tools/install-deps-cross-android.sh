#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
source ${SCRIPT_DIR}/install-deps-versions.sh

USE_CACHE="false"
SDK_PREFIX="/usr/local/android_sdk"
ARCHS="x86_64:x86_64-linux-android:android-x86_64 \
       armeabi-v7a:armv7a-linux-androideabi:android-arm \
       arm64-v8a:aarch64-linux-android:android-arm64"
WITH_VISION_SYSTEM_DATA="false"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --archs)
            ARCHS=$2
            shift
            ;;
        --with-vision-system-data-support)
            WITH_VISION_SYSTEM_DATA="true"
            ;;
        --native-prefix)
            NATIVE_PREFIX="$2"
            shift
            ;;
        --use-cache)
            USE_CACHE="true"
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --archs <ARCHS>                      Space separated list of archs in the format <ARCH>:<HOST_PLATFORM>:<SSL_TARGET>"
            echo "  --with-vision-system-data-support    Install dependencies for vision-system-data support"
            echo "  --native-prefix <PREFIX>             Native install prefix"
            exit 0
            ;;
        esac
        shift
    done
}

parse_args "$@"

apt-get update
apt-get install -y \
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

: ${NATIVE_PREFIX:="/usr/local"}

install_deps() {
    TARGET_ARCH="$1"
    HOST_PLATFORM="$2"
    SSL_TARGET="$3"
    INSTALL_PREFIX="/usr/local/${HOST_PLATFORM}"

    mkdir -p ${INSTALL_PREFIX}
    mkdir -p ${NATIVE_PREFIX}
    mkdir -p deps-cross-android && cd deps-cross-android

    # Boost
    if [ ! -d "Boost-for-Android" ]; then
        git clone https://github.com/moritz-wundke/Boost-for-Android.git
        wget -q https://archives.boost.io/release/${VERSION_BOOST}/source/boost_${VERSION_BOOST//./_}.tar.bz2 -O Boost-for-Android/boost_${VERSION_BOOST//./_}.tar.bz2
    fi
    cd Boost-for-Android
    git checkout ${VERSION_BOOST_FOR_ANDROID}
    rm -rf build
    ./build-android.sh \
        ${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK} \
        --boost=${VERSION_BOOST} \
        --with-libraries=system,thread,filesystem,chrono,date_time,atomic \
        --layout=system \
        --arch=${TARGET_ARCH} \
        --target-version=${VERSION_ANDROID_API} > /dev/null
    cp -r build/out/${TARGET_ARCH}/lib ${INSTALL_PREFIX}
    cp -r build/out/${TARGET_ARCH}/include ${INSTALL_PREFIX}
    cd ..

    # Snappy
    if [ ! -d "snappy" ]; then
        git clone -b ${VERSION_SNAPPY} https://github.com/google/snappy.git
    fi
    cd snappy
    rm -rf build
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
    make install -j`nproc` > /dev/null
    cd ../..

    if ${WITH_VISION_SYSTEM_DATA}; then
        # Amazon Ion C
        if [ ! -d "ion-c" ]; then
            git clone https://github.com/amazon-ion/ion-c.git
        fi
        cd ion-c
        git checkout ${VERSION_ION_C}
        git submodule update --init
        rm -rf build
        mkdir build && cd build
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=OFF \
            -DANDROID_ABI=${TARGET_ARCH} \
            -DANDROID_PLATFORM=android-${VERSION_ANDROID_API} \
            -DCMAKE_ANDROID_NDK=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK} \
            -DCMAKE_TOOLCHAIN_FILE=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK}/build/cmake/android.toolchain.cmake \
            -DANDROID_TOOLCHAIN=clang \
            -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} \
            ..
        make install -j`nproc`
        cd ../..
    fi

    # Protobuf
    if [ ! -d "protobuf-${VERSION_PROTOBUF}" ]; then
        wget -q https://github.com/protocolbuffers/protobuf/releases/download/${VERSION_PROTOBUF_RELEASE}/protobuf-cpp-${VERSION_PROTOBUF}.tar.gz
        tar -zxf protobuf-cpp-${VERSION_PROTOBUF}.tar.gz
    fi
    cd protobuf-${VERSION_PROTOBUF}
    if [ ! -f ${NATIVE_PREFIX}/bin/protoc ]; then
        rm -rf build_native
        mkdir build_native && cd build_native
        cmake \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_SHARED_LIBS=OFF \
            -DCMAKE_POSITION_INDEPENDENT_CODE=On \
            -Dprotobuf_BUILD_TESTS=Off \
            -DCMAKE_INSTALL_PREFIX=${NATIVE_PREFIX} \
            ..
        make install -j`nproc`
        cd ..
    fi
    rm -rf build_target
    mkdir build_target && cd build_target
    cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=On \
        -Dprotobuf_BUILD_TESTS=Off \
        -Dprotobuf_BUILD_PROTOC_BINARIES=Off \
        -DANDROID_ABI=${TARGET_ARCH} \
        -DANDROID_PLATFORM=android-${VERSION_ANDROID_API} \
        -DCMAKE_ANDROID_NDK=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK} \
        -DCMAKE_TOOLCHAIN_FILE=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK}/build/cmake/android.toolchain.cmake \
        -DANDROID_TOOLCHAIN=clang \
        -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} \
        ..
    make install -j`nproc` > /dev/null
    cd ../..

    # JsonCpp
    if [ ! -d "jsoncpp" ]; then
        git clone -b ${VERSION_JSON_CPP} https://github.com/open-source-parsers/jsoncpp.git
    fi
    cd jsoncpp
    rm -rf build
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
    make install -j`nproc` > /dev/null
    cd ../..

    # OpenSSL
    if [ ! -f "openssl-${VERSION_OPENSSL}.tar.gz" ]; then
        wget -q https://www.openssl.org/source/openssl-${VERSION_OPENSSL}.tar.gz
    fi
    rm -rf openssl-${VERSION_OPENSSL}
    tar -zxf openssl-${VERSION_OPENSSL}.tar.gz
    cd openssl-${VERSION_OPENSSL}
    ANDROID_NDK_ROOT=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK} \
        PATH=${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin:${PATH} \
        INSTALL_PREFIX=${INSTALL_PREFIX} SSL_TARGET=${SSL_TARGET} VERSION_ANDROID_API=${VERSION_ANDROID_API} \
        bash -c './Configure ${SSL_TARGET} -D__ANDROID_API__=${VERSION_ANDROID_API} --prefix=${INSTALL_PREFIX} no-shared \
            && make -j`nproc`' > /dev/null
    make install > /dev/null
    cd ..

    # curl
    if [ ! -f "curl-${VERSION_CURL}.tar.gz" ]; then
        wget -q https://github.com/curl/curl/releases/download/${VERSION_CURL_RELEASE}/curl-${VERSION_CURL}.tar.gz
    fi
    rm -rf curl-${VERSION_CURL}
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
        PKG_CONFIG="pkg-config --static" PKG_CONFIG_LIBDIR=${INSTALL_PREFIX} \
        ./configure \
            --disable-shared \
            --enable-static \
            --disable-ldap \
            --enable-ipv6 \
            --with-ssl=${INSTALL_PREFIX} \
            --without-ca-bundle \
            --without-ca-path \
            --with-ca-fallback \
            --disable-unix-sockets \
            --disable-rtsp \
            --host=${HOST_PLATFORM} \
            --prefix=${INSTALL_PREFIX}
    make install -j`nproc` V=1 LDFLAGS="-static -L${INSTALL_PREFIX}/lib" > /dev/null
    cd ..

    # AWS C++ SDK
    if [ ! -d "aws-sdk-cpp" ]; then
        git clone -b ${VERSION_AWS_SDK_CPP} --recursive https://github.com/aws/aws-sdk-cpp.git
    fi
    cd aws-sdk-cpp
    rm -rf build
    mkdir build && cd build
    CFLAGS=-I${INSTALL_PREFIX}/include cmake \
        -DENABLE_TESTING=OFF \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_ONLY='transfer;s3-crt;iot' \
        -DANDROID_ABI=${TARGET_ARCH} \
        -DANDROID_PLATFORM=android-${VERSION_ANDROID_API} \
        -DCMAKE_ANDROID_NDK=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK} \
        -DCMAKE_TOOLCHAIN_FILE=${SDK_PREFIX}/ndk/${VERSION_ANDROID_NDK}/build/cmake/android.toolchain.cmake \
        -DANDROID_TOOLCHAIN=clang \
        -DCURL_LIBRARY=${INSTALL_PREFIX}/lib/libcurl.a \
        -DCMAKE_FIND_ROOT_PATH=${INSTALL_PREFIX} \
        -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} \
        ..
    make install -j`nproc` > /dev/null
    cd ../..
    # pthread is directly linked somewhere, so just create a dummy .a file
    ar -rc ${INSTALL_PREFIX}/lib/libpthread.a

    cd ..
}

if ${USE_CACHE}; then
    echo "using cache, not building..."
    exit 0
fi

for ARCH in ${ARCHS}; do
    TARGET_ARCH=`echo $ARCH | cut -d ':' -f1`
    HOST_PLATFORM=`echo $ARCH | cut -d ':' -f2`
    SSL_TARGET=`echo $ARCH | cut -d ':' -f3`
    install_deps ${TARGET_ARCH} ${HOST_PLATFORM} ${SSL_TARGET}
done
rm -rf deps-cross-android
