# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

cmake_minimum_required(VERSION 3.10.2)

project(iotfleetwise VERSION 0.1.2)

# Due to autosar rules, AWS IoT FleetWise Edge will use C++11
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

message(STATUS "Hello from the AWS IoT FleetWise build system!")

# Print out the compile commands which is useful for IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# All AWS IoT FleetWise build and configure options are prefaced with an FWE_ to set
# them apart from other build options.
option(FWE_STATIC_LINK "Statically link external libs" OFF)
option(FWE_CODE_COVERAGE "Enables code coverage" OFF)
option(FWE_VALGRIND "Enable valgrind tests" OFF)
option(FWE_BUILD_DOC "Build documentation" ON)
option(FWE_STRIP_SYMBOLS "Strips symbols from output binaries" OFF)
option(FWE_FEATURE_CAMERA "Enable Camera Data Collection feature" OFF)
option(FWE_TEST_CLANG_TIDY "Add clang-tidy test" ON)
option(FWE_TEST_CLANG_FORMAT "Add clang-format test" ON)
option(FWE_SECURITY_COMPILE_FLAGS "Add security related compile options" OFF)
option(FWE_IOT_SDK_SHARED_LIBS "Use AWS IoT Device SDK shared libs" OFF)

# Define the default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(cmake/compiler_gcc.cmake)
include(cmake/protobufs.cmake)
if(FWE_FEATURE_CAMERA)
  add_compile_options("-DFWE_FEATURE_CAMERA")
  include(cmake/ddsidls.cmake)
endif()
include(cmake/snappy.cmake)
include(CTest)
include(cmake/valgrind.cmake)
include(cmake/clang_tools.cmake)
include(cmake/clang_tidy.cmake)
if(FWE_BUILD_DOC)
  include(cmake/doxygen.cmake)
endif()

add_subdirectory(src)
