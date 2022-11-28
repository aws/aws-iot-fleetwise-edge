# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

set(libraryTargetName iotfleetwise.offboardconnectivityawsiot)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::OffboardConnectivityAwsIot)

set(librarySrc
  src/AwsIotChannel.cpp
  src/AwsIotConnectivityModule.cpp
  src/RetryThread.cpp
  src/PayloadManager.cpp
  src/RemoteProfiler.cpp)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  ${librarySrc}
)

find_path(JSONCPP_INCLUDE_DIR "json/json.h" PATH_SUFFIXES "jsoncpp")
find_library(JSONCPP_LIBRARY NAMES jsoncpp)

set(OLD_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS ${FWE_AWS_SDK_SHARED_LIBS})
find_package(AWSSDK REQUIRED COMPONENTS iot)
set(BUILD_SHARED_LIBS ${OLD_BUILD_SHARED_LIBS})

message(STATUS "Connectivity json include ${JSONCPP_INCLUDE_DIR}")
target_include_directories(${libraryTargetName} PUBLIC include
${JSONCPP_INCLUDE_DIR})


target_link_libraries(
  ${libraryTargetName}
  IoTFleetWise::OffboardConnectivityAwsBootstrap
  IoTFleetWise::OffboardConnectivity
  IoTFleetWise::Platform::Linux
  ${AWSSDK_LINK_LIBRARIES}
  ${SNAPPY_LIBRARIES}
  ${JSONCPP_LIBRARY}
  ${FWE_AWS_SDK_EXTRA_LIBS}
)


add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### Tests ###

if(${BUILD_TESTING})
  message(STATUS "Building tests for ${libraryTargetName}")

  find_package(GTest REQUIRED)

  find_library(GMOCK_LIB
      NAMES
        gmock)

  find_library(GMOCK_MAIN_LIBRARY
      NAMES
        gmock_main)

  set(testName OffboardConnectivityAwsIotTest)
  add_executable(${testName}
    test/src/AwsIotConnectivityModuleTest.cpp
    test/src/AwsIotSdkMock.cpp
    test/src/MqttClient.cpp
    ${librarySrc})

  add_unit_test(${testName})
  add_valgrind_test(${testName})

  target_link_libraries(
    ${testName}
    PRIVATE
    IoTFleetWise::OffboardConnectivityAwsBootstrap
    IoTFleetWise::OffboardConnectivity
    GTest::Main
    GTest::GTest
    ${GMOCK_MAIN_LIBRARY}
    ${GMOCK_LIB}
    ${SNAPPY_LIBRARIES}
    ${JSONCPP_LIBRARY}
  )

  target_include_directories(${testName} PRIVATE
    include
    test/include
    test/include/aws-iot-cpp-sdk-mock
    ${JSONCPP_INCLUDE_DIR})

  install(TARGETS ${testName} RUNTIME DESTINATION bin/tests)

  set(
      testSources
      test/src/PayloadManagerTest.cpp
      test/src/RemoteProfilerTest.cpp
  )

  # Add the executable targets
  foreach(testSource ${testSources})
    # Need a name for each exec so use filename w/o extension
    get_filename_component(testName ${testSource} NAME_WE)
    add_executable(${testName} ${testSource})

    add_unit_test(${testName})
    add_valgrind_test(${testName})

    # Link to the project library and GTest main
    target_link_libraries(
      ${testName}
      PRIVATE
      ${libraryTargetName}
      GTest::Main
      IoTFleetWise::Proto
      ${SNAPPY_LIBRARIES}
    )

    install(TARGETS ${testName} RUNTIME DESTINATION bin/tests)

  endforeach()

endif()

