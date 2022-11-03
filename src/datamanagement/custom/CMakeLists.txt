# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
set(libraryTargetName iotfleetwise.customdata)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::CustomDataSource)

set(SRCS
  generic/src/CustomDataSource.cpp
  example/iwavegps/src/IWaveGpsSource.cpp
)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  ${SRCS}
)

target_include_directories(${libraryTargetName} PUBLIC generic/include example/iwavegps/include)

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

  find_package(GTest REQUIRED)

  find_library(GMOCK_LIB
  NAMES
    gmock)

  find_library(GMOCK_MAIN_LIBRARY
  NAMES
    gmock_main)


  set(
    testSources
    example/iwavegps/test/IWaveGpsSourceTest.cpp
    generic/test/CustomDataSourceTest.cpp
  )
  # Add the executable targets
  foreach(testSource ${testSources})
    # Need a name for each exec so use filename w/o extension
    get_filename_component(testName ${testSource} NAME_WE)

    add_executable(${testName} ${testSource})

    target_include_directories(
      ${testName}
      PRIVATE
      include)

    # Link to the project library and GTest main
    target_link_libraries(
      ${testName}
      PRIVATE
      ${libraryTargetName}
      GTest::Main
      GTest::GTest
      ${GMOCK_LIB}
    )

    add_test(NAME ${testName} COMMAND ${testName} --gtest_output=xml:report-${testName}.xml)
    add_valgrind_test(${testName} ${CMAKE_CURRENT_SOURCE_DIR}/example/iwavegps/test/valgrind.supp)
    install(TARGETS ${testName} RUNTIME DESTINATION bin/tests)

  endforeach()
else()
  message(STATUS "Testing not enabled for ${libraryTargetName}")
endif()
