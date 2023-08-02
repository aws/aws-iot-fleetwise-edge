# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
set(libraryTargetName iotfleetwise.datasender)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::DataSender)

set(SRCS
  src/DataSenderManagerWorkerThread.cpp
  src/DataSenderManager.cpp
  src/DataSenderProtoWriter.cpp
)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  ${SRCS}
)

target_include_directories(${libraryTargetName} PUBLIC include)

target_link_libraries(
  ${libraryTargetName}
  IoTFleetWise::DataManagementTypes
  IoTFleetWise::Platform::Linux
  IoTFleetWise::DataInspection
  IoTFleetWise::DataManager
  IoTFleetWise::OffboardConnectivityAwsIot
)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### Install ###

install(TARGETS ${libraryTargetName} DESTINATION lib)

install(
  FILES
  include/DataSenderManager.h
  include/DataSenderManagerWorkerThread.h
  include/DataSenderProtoWriter.h
  DESTINATION include
)

if(${BUILD_TESTING})
  message(STATUS "Building tests for ${libraryTargetName}")

  find_library(GMOCK_LIB
  NAMES
    gmock)

  find_library(GMOCK_MAIN_LIBRARY
  NAMES
    gmock_main)

  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/dm-collection-scheme-example.json
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR})


  set(
      testSources
      test/DataSenderProtoWriterTest.cpp
  )

  find_package(Boost 1.65.1 REQUIRED COMPONENTS filesystem)

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
      Boost::filesystem
      IoTFleetWise::TestingSupport
      IoTFleetWise::OffboardConnectivityAwsIot
      ${GMOCK_LIB}
    )

    target_include_directories(
      ${testName}
      PRIVATE
      include
      test/include)

    add_unit_test(${testName})
    add_valgrind_test(${testName})
    install(TARGETS ${testName} RUNTIME DESTINATION bin/tests)

  endforeach()

endif()
