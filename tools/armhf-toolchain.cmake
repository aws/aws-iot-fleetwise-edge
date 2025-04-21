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
# During cross-compilation, CMake cannot execute TRY_RUN tests since the compiled binary
# would be for the target architecture (ARM) rather than the host system. These cache
# variables provide pre-set values to bypass the actual execution of tests:
# - SM_RUN_RESULT: Set to "FAILED_TO_RUN" as expected by project dependencies
# - SM_RUN_RESULT__TRYRUN_OUTPUT: Set to "NOTFOUND" for expected behavior
set( SM_RUN_RESULT
     "FAILED_TO_RUN"
     CACHE STRING "Result from TRY_RUN" FORCE)

set( SM_RUN_RESULT__TRYRUN_OUTPUT
     "NOTFOUND"
     CACHE STRING "Output from TRY_RUN" FORCE)
