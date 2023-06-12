# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
set(libraryTargetName iotfleetwise.datainspection)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::DataInspection)

set(SRCS
  src/CollectionInspectionEngine.cpp
  src/CollectionInspectionWorkerThread.cpp
  $<$<BOOL:${FWE_FEATURE_CAMERA}>:src/dds/DataOverDDSModule.cpp>
  src/diag/OBDOverCANModule.cpp
  src/diag/OBDOverCANECU.cpp
  src/location/GeohashFunctionNode.cpp
  src/vehicledatasource/CANDataSource.cpp
  src/vehicledatasource/CANDataConsumer.cpp
  src/vehicledatasource/ExternalCANDataSource.cpp
)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  ${SRCS}
)

find_path(JSONCPP_INCLUDE_DIR "json/json.h" PATH_SUFFIXES "jsoncpp")
find_library(JSONCPP_LIBRARY NAMES jsoncpp)

target_include_directories(${libraryTargetName} PUBLIC
  include
  ${JSONCPP_INCLUDE_DIR}
)

target_link_libraries(
  ${libraryTargetName}
  IoTFleetWise::Platform::Linux
  IoTFleetWise::Vehiclenetwork
  IoTFleetWise::DataDecoding
  IoTFleetWise::Platform::Utility
  ${JSONCPP_LIBRARY}
)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### Install ###

install(TARGETS ${libraryTargetName} DESTINATION lib)

install(
  FILES
  include/CollectionInspectionEngine.h
  include/CollectionInspectionWorkerThread.h
  $<$<BOOL:${FWE_FEATURE_CAMERA}>:${CMAKE_CURRENT_LIST_DIR}/include/DataOverDDSModule.h>
  include/DataReduction.h
  include/GeohashFunctionNode.h
  include/IActiveConditionProcessor.h
  include/IDataReadyToPublishListener.h
  include/InspectionEventListener.h
  include/CANDataSource.h
  include/CANDataConsumer.h
  include/OBDOverCANModule.h
  include/OBDOverCANECU.h
  DESTINATION include
)

### Tests ###
# This is a list of tests that will compiled.
# If adding a test, simply add the source file here
set(
  testSources
  test/GeohashFunctionNodeTest.cpp
  test/OBDOverCANModuleTest.cpp
  test/CollectionInspectionEngineTest.cpp
  test/CollectionInspectionWorkerThreadTest.cpp
  test/CANDataSourceTest.cpp
  test/ExternalCANDataSourceTest.cpp
)

if(FWE_FEATURE_CAMERA)
  set(
    testSources
    ${testSources}
    test/DataOverDDSModuleTest.cpp
  )
endif()

if(${BUILD_TESTING})
  message(STATUS "Building tests for ${libraryTargetName}")

  # Copy the json file required for the test application
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/di-collection-scheme-example.json
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR} )
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/di-collection-scheme-example-reduced.json
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

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
else()
  message(STATUS "Testing not enabled for ${libraryTargetName}")
endif()
