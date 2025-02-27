# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

cmake_minimum_required(VERSION 3.10.2)

project(fwe-example-network-agnostic-actuator-commands)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(AwsIotFwe REQUIRED)

add_executable(fwe-example-network-agnostic-actuator-commands
  AcCommandDispatcher.cpp
  main.cpp
)
target_include_directories(fwe-example-network-agnostic-actuator-commands PUBLIC
  ${AwsIotFwe_INCLUDE_DIR}
)
target_link_libraries(fwe-example-network-agnostic-actuator-commands
  AwsIotFwe::aws-iot-fleetwise-edge
)
