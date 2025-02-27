# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

cmake_minimum_required(VERSION 3.10.2)

project(fwe-example-s3-upload)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(AwsIotFwe REQUIRED)

add_executable(fwe-example-s3-upload
  MyS3Upload.cpp
  main.cpp
)
target_include_directories(fwe-example-s3-upload PUBLIC
  ${AwsIotFwe_INCLUDE_DIR}
)
target_link_libraries(fwe-example-s3-upload
  AwsIotFwe::aws-iot-fleetwise-edge
)
