# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH "/usr/local/aarch64-linux-gnu")
set(PKG_CONFIG_EXECUTABLE "/usr/bin/aarch64-linux-gnu-pkg-config")
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
