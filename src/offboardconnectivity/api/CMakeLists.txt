# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

set(libraryTargetName iotfleetwise.OffboardConnectivity)

set(libraryAliasName IoTFleetWise::OffboardConnectivity)

add_library(${libraryTargetName}  INTERFACE)

target_include_directories(${libraryTargetName} INTERFACE include)

target_link_libraries(${libraryTargetName} INTERFACE IoTFleetWise::Platform)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})