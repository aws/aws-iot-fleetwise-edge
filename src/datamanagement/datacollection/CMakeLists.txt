# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
set(libraryTargetName iotfleetwise.datacollection)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::DataCollection)

set(SRCS
  src/CollectionScheme.cpp
  src/CollectionSchemeIngestion.cpp
  src/CollectionSchemeIngestionList.cpp
  src/CollectionSchemeJSONParser.cpp
  src/DataCollectionJSONWriter.cpp
  src/DataCollectionProtoWriter.cpp
  src/DataCollectionSender.cpp
)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  ${SRCS}
)

find_path(JSONCPP_INCLUDE_DIR "json/json.h" PATH_SUFFIXES "jsoncpp")
find_library(JSONCPP_LIBRARY NAMES jsoncpp)

target_include_directories(${libraryTargetName} PUBLIC include ${JSONCPP_INCLUDE_DIR})

find_package(Boost 1.65.1 REQUIRED COMPONENTS filesystem)

target_link_libraries(
  ${libraryTargetName}
  Boost::filesystem
  IoTFleetWise::DataManagementTypes
  IoTFleetWise::OffboardConnectivityAwsIot
  IoTFleetWise::Proto
  IoTFleetWise::Platform::Linux
  IoTFleetWise::Vehiclenetwork
)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### Install ###

install(TARGETS ${libraryTargetName} DESTINATION lib)

install(
  FILES
  include/CANInterfaceIDTranslator.h
  include/CollectionScheme.h
  include/CollectionSchemeIngestion.h
  include/CollectionSchemeIngestionList.h
  include/CollectionSchemeJSONParser.h
  include/DataCollectionJSONWriter.h
  include/DataCollectionProtoWriter.h
  include/DataCollectionSender.h
  include/ICollectionScheme.h
  include/ICollectionSchemeList.h
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
      test/CollectionSchemeJSONParserTest.cpp
      test/DataCollectionJSONWriterTest.cpp
      test/DataCollectionProtoWriterTest.cpp
      test/DataCollectionSenderTest.cpp
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
      Boost::filesystem
      IoTFleetWise::TestingSupport
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
