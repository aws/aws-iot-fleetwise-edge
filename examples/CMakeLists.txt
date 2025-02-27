# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

cmake_minimum_required(VERSION 3.10.2)

project(fwe-examples)

add_subdirectory(custom_function)
add_subdirectory(mqtt_pub_sub)
add_subdirectory(network_agnostic_actuator_commands)
add_subdirectory(network_agnostic_data_collection)
add_subdirectory(s3_upload)
add_subdirectory(someip)
