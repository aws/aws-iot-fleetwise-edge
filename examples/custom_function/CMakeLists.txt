# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

cmake_minimum_required(VERSION 3.10.2)

project(fwe-example-custom-function)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies
find_package(AwsIotFwe REQUIRED)

add_executable(fwe-example-custom-function
  CustomFunctionCounter.cpp
  CustomFunctionFileSize.cpp
  CustomFunctionSin.cpp
  main.cpp
)
target_include_directories(fwe-example-custom-function PUBLIC
  ${AwsIotFwe_INCLUDE_DIR}
)
target_link_libraries(fwe-example-custom-function
  AwsIotFwe::aws-iot-fleetwise-edge
)
