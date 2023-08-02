# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
set(libraryTargetName iotfleetwise.datamanager)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::DataManager)

set(SRCS
  src/CheckinAndPersistency.cpp
  src/CollectionSchemeManager.cpp
  src/DecoderDictionaryExtractor.cpp
  src/InspectionMatrixExtractor.cpp
  src/Schema.cpp
)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  ${SRCS}
)

target_include_directories(${libraryTargetName} PUBLIC include)

target_link_libraries(
  ${libraryTargetName}
  IoTFleetWise::Platform::Linux
  IoTFleetWise::Vehiclenetwork
  IoTFleetWise::DataInspection
  IoTFleetWise::DataCollection
  IoTFleetWise::DataDecoding
  IoTFleetWise::OffboardConnectivityAwsIot
  IoTFleetWise::Platform::Utility
)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### Install ###

install(TARGETS ${libraryTargetName} DESTINATION lib)

install(
  FILES
  include/ICollectionSchemeManager.h
  include/IActiveCollectionSchemesListener.h
  include/CollectionSchemeManagementListener.h
  include/CollectionSchemeManager.h
  include/Schema.h
  include/SchemaListener.h
  DESTINATION include
)


### Tests ###
# This is a list of tests that will compiled.
# If adding a test, simply add the source file here

if(${BUILD_TESTING})
  message(STATUS "Building tests for ${libraryTargetName}")

  find_library(GMOCK_LIB
  NAMES
    gmock)

  find_library(GMOCK_MAIN_LIBRARY
  NAMES
    gmock_main)


  set(
    testSources
    test/CheckinAndPersistencyTest.cpp
    test/CollectionSchemeManagerGtest.cpp
    test/CollectionSchemeManagerTest.cpp
    test/DecoderDictionaryExtractorTest.cpp
    test/InspectionMatrixExtractorTest.cpp
    test/SchemaTest.cpp
  )
  # Add the executable targets
  foreach(testSource ${testSources})
    # Need a name for each exec so use filename w/o extension
    get_filename_component(testName ${testSource} NAME_WE)

    add_executable(${testName} ${testSource})

    target_include_directories(
      ${testName}
      PRIVATE
      include
      test/include)

    # Link to the project library and testing library main
    target_link_libraries(
      ${testName}
      PRIVATE
      ${libraryTargetName}
      IoTFleetWise::TestingSupport
      ${GMOCK_LIB}
    )

    add_test(NAME ${testName} COMMAND ${testName} --gtest_output=xml:report-${testName}.xml)
    add_valgrind_test(${testName})
    install(TARGETS ${testName} RUNTIME DESTINATION bin/tests)

  endforeach()
else()
  message(STATUS "Testing not enabled for ${libraryTargetName}")
endif()
