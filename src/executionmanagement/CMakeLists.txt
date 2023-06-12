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
  IoTFleetWise::DataManager
  IoTFleetWise::DataCollection
  IoTFleetWise::DataInspection
  IoTFleetWise::DataDecoding
  IoTFleetWise::Platform::Linux
  IoTFleetWise::OffboardConnectivityAwsIot
  $<$<BOOL:${FWE_FEATURE_CUSTOM_DATA_SOURCE}>:IoTFleetWise::CustomDataSource>
)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### AWS IoT FleetWise Edge Executable ###

if(FWE_BUILD_EXECUTABLE)
  add_executable(aws-iot-fleetwise-edge src/main.cpp)
  target_link_libraries(
    aws-iot-fleetwise-edge
    ${libraryTargetName}
    IoTFleetWise::Platform::Linux
    -Xlinker
    -Map=aws-iot-fleetwise-edge.map
  )
endif()

if(FWE_BUILD_ANDROID_SHARED_LIBRARY)
  add_library(aws-iot-fleetwise-edge SHARED src/android_shared_library.cpp)
  target_link_libraries(
    aws-iot-fleetwise-edge
    ${libraryTargetName}
    IoTFleetWise::Platform::Linux
    log
    android
    -Xlinker
    -Map=aws-iot-fleetwise-edge.map
  )
endif()

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

### Tests ###

if(${BUILD_TESTING})
  message(STATUS "Building tests for ${libraryTargetName}")


  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/em-example-config.json
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/em-example-config-corrupt.json
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/em-example-config-inline-creds.json
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

  set(
      testSources
      test/IoTFleetWiseConfigTest.cpp
      test/IoTFleetWiseEngineTest.cpp
    )
   # Add the executable targets
  foreach(testSource ${testSources})
    # Need a name for each exec so use filename w/o extension
    get_filename_component(testName ${testSource} NAME_WE)

    add_executable(${testName} ${testSource})

    # Link to the project library and testing library main
    target_link_libraries(
      ${testName}
      PRIVATE
      ${libraryTargetName}
      IoTFleetWise::TestingSupport
    )

    add_unit_test(${testName})
    add_valgrind_test(${testName})
    install(TARGETS ${testName} RUNTIME DESTINATION bin/tests)

  endforeach()

endif()
