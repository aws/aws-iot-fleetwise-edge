# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

cmake_minimum_required(VERSION 3.10.2)

project(iotfleetwise VERSION 1.0.7)

# FWE uses C++14 for compatibility reasons with Automotive middlewares (Adaptive AUTOSAR, ROS2)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

message(STATUS "Hello from the FWE build system!")

# Print out the compile commands which is useful for IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# All build and configuration options are prefaced with FWE_
option(FWE_STATIC_LINK "Statically link external libs" OFF)
option(FWE_CODE_COVERAGE "Enables code coverage" OFF)
option(FWE_VALGRIND "Enable valgrind tests" OFF)
option(FWE_BUILD_DOC "Build documentation" ON)
option(FWE_STRIP_SYMBOLS "Strips symbols from output binaries" OFF)
option(FWE_TEST_CLANG_TIDY "Add clang-tidy test" ON)
option(FWE_TEST_FAKETIME "Enable tests that use the faketime library" OFF)
option(FWE_WERROR "Enable -Werror compiler flag" OFF)
option(FWE_SECURITY_COMPILE_FLAGS "Add security related compile options" OFF)
option(FWE_AWS_SDK_SHARED_LIBS "Use AWS SDK shared libs. Needs to be set to the same value of BUILD_SHARED_LIBS that the SDK was compiled with." OFF)
option(FWE_AWS_SDK_EXTRA_LIBS "Extra libs required to link with the AWS SDK. When FWE_STATIC_LINK is ON, setting this to ON will automatically find the standard libs. Can be a space-separated list of libs." ON)
option(FWE_FEATURE_GREENGRASSV2 "Enable Greengrass connection module" OFF)
option(FWE_FEATURE_CUSTOM_DATA_SOURCE "Include the custom data source interface" OFF)
option(FWE_FEATURE_IWAVE_GPS "Include the IWave GPS example for a custom data source" OFF)
option(FWE_FEATURE_EXTERNAL_GPS "Include the external GPS example for a custom data source" OFF)
option(FWE_FEATURE_AAOS_VHAL "Include the Android Automotive VHAL example for a custom data source" OFF)
option(FWE_BUILD_EXECUTABLE "Build the executable" ON)
option(FWE_BUILD_ANDROID_SHARED_LIBRARY "Build the android shared library" OFF)
if(FWE_FEATURE_IWAVE_GPS)
  add_compile_options("-DFWE_FEATURE_IWAVE_GPS")
endif()
if(FWE_FEATURE_EXTERNAL_GPS)
  add_compile_options("-DFWE_FEATURE_EXTERNAL_GPS")
endif()
if(FWE_FEATURE_AAOS_VHAL)
  add_compile_options("-DFWE_FEATURE_AAOS_VHAL")
endif()
if(FWE_FEATURE_GREENGRASSV2)
  add_compile_options("-DFWE_FEATURE_GREENGRASSV2")
endif()

# Define the default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(cmake/graphviz.cmake)
include(cmake/compiler_gcc.cmake)
include(cmake/protobufs.cmake)
include(cmake/snappy.cmake)
include(CTest)
include(cmake/unit_test.cmake)
include(cmake/valgrind.cmake)
include(cmake/clang_tidy.cmake)
if(FWE_BUILD_DOC)
  include(cmake/doxygen.cmake)
endif()

add_subdirectory(src)

# Disallow cycles
set_property( GLOBAL PROPERTY GLOBAL_DEPENDS_NO_CYCLES ON )
