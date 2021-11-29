# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

set(libraryTargetName iotfleetwise.executionmanagement)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::Executionmanagement)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  src/IoTFleetWiseConfig.cpp
  src/IoTFleetWiseEngine.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/IoTFleetWiseVersion.cpp
)

target_include_directories(${libraryTargetName} PUBLIC include)

target_link_libraries(
  ${libraryTargetName}
  IoTFleetWise::Vehiclenetwork
  IoTFleetWise::Datamanagement
  IoTFleetWise::Datainspection
  IoTFleetWise::Platform
  IoTFleetWise::OffboardConnectivityAwsIot
)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### AWS IoT FleetWise Edge Executable ###

add_executable(aws-iot-fleetwise-edge src/main.cpp)

target_link_libraries(
  aws-iot-fleetwise-edge
  ${libraryTargetName}
  IoTFleetWise::Platform
  pthread
  -Xlinker
  -Map=aws-iot-fleetwise-edge.map
)

### Version ###

execute_process(COMMAND git rev-parse HEAD
        OUTPUT_VARIABLE VERSION_GIT_SHA
        ERROR_QUIET)

execute_process(COMMAND git describe --tags --abbrev=0
        OUTPUT_VARIABLE VERSION_GIT_TAG
        ERROR_QUIET)

string(STRIP "${VERSION_GIT_SHA}" VERSION_GIT_SHA)

string(STRIP "${VERSION_GIT_TAG}" VERSION_GIT_TAG)

string(TIMESTAMP VERSION_CURRENT_TIME "%Y-%m-%d %H:%M:%S" UTC)

configure_file(src/IoTFleetWiseVersion.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/IoTFleetWiseVersion.cpp)

### Install ###

install(TARGETS ${libraryTargetName} DESTINATION lib)

install(
  FILES
  include/IoTFleetWiseConfig.h
  include/IoTFleetWiseEngine.h
  DESTINATION include
)

