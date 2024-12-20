# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
set(CMAKE_FIND_ROOT_PATH "/usr/local/arm-linux-gnueabihf")
set(PKG_CONFIG_EXECUTABLE "/usr/bin/arm-linux-gnueabihf-pkg-config")
set(CMAKE_CXX_FLAGS "" CACHE INTERNAL "")
set(CMAKE_C_FLAGS "" CACHE INTERNAL "")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-psabi")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-psabi")
