# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

cmake_minimum_required(VERSION 3.10.2)

project(iotfleetwise VERSION 1.0.3)

# AWS IoT FleetWise Edge uses C++14 for compatibility reasons with 
# Automotive middlewares ( Adaptive AUTOSAR, ROS2)
set(CMAKE_CXX_STANDARD 14)
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
option(FWE_TEST_FAKETIME "Enable tests that use the faketime library" OFF)
option(FWE_SECURITY_COMPILE_FLAGS "Add security related compile options" OFF)
option(FWE_AWS_SDK_SHARED_LIBS "Use AWS SDK shared libs. Needs to be set to the same value of BUILD_SHARED_LIBS that the SDK was compiled with." OFF)
option(FWE_AWS_SDK_EXTRA_LIBS "Extra libs required to link with the AWS SDK. When FWE_STATIC_LINK is ON, setting this to ON will automatically find the standard libs. Can be a space-separated list of libs." ON)
option(FWE_EXAMPLE_IWAVEGPS "Include the IWave GPS example for a custom data source" OFF)
if(FWE_EXAMPLE_IWAVEGPS)
  add_compile_options("-DFWE_EXAMPLE_IWAVEGPS")
endif()

# Define the default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(cmake/graphviz.cmake)
include(cmake/compiler_gcc.cmake)
include(cmake/protobufs.cmake)
if(FWE_FEATURE_CAMERA)
  add_compile_options("-DFWE_FEATURE_CAMERA")
  include(cmake/ddsidls.cmake)
endif()
include(cmake/snappy.cmake)
include(CTest)
include(cmake/unit_test.cmake)
include(cmake/valgrind.cmake)
include(cmake/clang_tools.cmake)
include(cmake/clang_tidy.cmake)
if(FWE_BUILD_DOC)
  include(cmake/doxygen.cmake)
endif()

add_subdirectory(src)

# Disallow cycles
set_property( GLOBAL PROPERTY GLOBAL_DEPENDS_NO_CYCLES ON )
