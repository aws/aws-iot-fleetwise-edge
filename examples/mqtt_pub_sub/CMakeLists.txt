# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

cmake_minimum_required(VERSION 3.10.2)

project(fwe-example-mqtt-pub-sub)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(AwsIotFwe REQUIRED)

add_executable(fwe-example-mqtt-pub-sub
  main.cpp
)
target_include_directories(fwe-example-mqtt-pub-sub PUBLIC
  ${AwsIotFwe_INCLUDE_DIR}
)
target_link_libraries(fwe-example-mqtt-pub-sub
  AwsIotFwe::aws-iot-fleetwise-edge
)
