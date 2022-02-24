# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

set(libraryTargetName iotfleetwise.datamanagement)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::Datamanagement)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  src/CANDecoder.cpp
  src/CollectionScheme.cpp
  src/CollectionSchemeJSONParser.cpp
  src/DataCollectionJSONWriter.cpp
  src/DataCollectionProtoWriter.cpp
  src/DataCollectionSender.cpp
  src/OBDDataDecoder.cpp
  src/Schema.cpp
  src/CollectionSchemeIngestion.cpp
  src/DecoderManifestIngestion.cpp
  src/CollectionSchemeIngestionList.cpp
  src/CollectionSchemeManager/CollectionSchemeManager.cpp
  src/CollectionSchemeManager/CheckinAndPersistency.cpp
  src/CollectionSchemeManager/DecoderDictionaryExtractor.cpp
  src/CollectionSchemeManager/InspectionMatrixExtractor.cpp
)

find_path(JSONCPP_INCLUDE_DIR "json/json.h" PATH_SUFFIXES "jsoncpp")
find_library(JSONCPP_LIBRARY NAMES jsoncpp)

target_include_directories(${libraryTargetName} PUBLIC
  include
  ${JSONCPP_INCLUDE_DIR}
)

target_link_libraries(
  ${libraryTargetName}
  # From the Platform, this is what is used: logmanagement timemanagement
  IoTFleetWise::Platform
  IoTFleetWise::Vehiclenetwork
  ${JSONCPP_LIBRARY}
  stdc++fs
  IoTFleetWise::Proto
  IoTFleetWise::OffboardConnectivity
  IoTFleetWise::Datainspection
)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### Install ###

# No Install targets necessary

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

  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/dm-collection-scheme-example.json
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR})


  set(
      testSources
      test/CANDecoderTest.cpp
      test/CollectionSchemeJSONParserTest.cpp
      test/OBDDataDecoderTest.cpp
      test/DataCollectionProtoWriterTest.cpp
      test/DataCollectionSenderTest.cpp
      test/SchemaTest.cpp
      test/CollectionSchemeManager/CollectionSchemeManagerTest.cpp
      test/CollectionSchemeManager/CollectionSchemeManagerGtest.cpp
      test/CollectionSchemeManager/CheckinAndPersistencyTest.cpp
      test/CollectionSchemeManager/InspectionMatrixExtractorTest.cpp
      test/CollectionSchemeManager/DecoderDictionaryExtractorTest.cpp
    )
   # Add the executable targets
  foreach(testSource ${testSources})
    # Need a name for each exec so use filename w/o extension
    get_filename_component(testName ${testSource} NAME_WE)

    add_executable(${testName} ${testSource})

    # Link to the project library and GTest main
    target_link_libraries(
      ${testName}
      PRIVATE
      ${libraryTargetName}
      GTest::Main
      GTest::GTest
      ${GMOCK_LIB}
  #    ${GMOCK_MAIN_LIBRARY}
      ${JSONCPP_LIBRARY}
      IoTFleetWise::OffboardConnectivityAwsIot
    )

    target_include_directories(
      ${testName}
      PRIVATE
      include
      test/include)

    add_test(NAME ${testName} COMMAND ${testName} --gtest_output=xml:report-${testName}.xml)
    add_valgrind_test(${testName})
    install(TARGETS ${testName} RUNTIME DESTINATION bin/tests)

  endforeach()

endif()
