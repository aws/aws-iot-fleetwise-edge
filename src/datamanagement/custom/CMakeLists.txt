# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
set(libraryTargetName iotfleetwise.customdata)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::CustomDataSource)

if(FWE_FEATURE_IWAVE_GPS)
  set(EXTRA_SOURCE_FILES ${EXTRA_SOURCE_FILES} example/iwavegps/src/IWaveGpsSource.cpp)
endif()
if(FWE_FEATURE_EXTERNAL_GPS)
  set(EXTRA_SOURCE_FILES ${EXTRA_SOURCE_FILES} example/externalgps/src/ExternalGpsSource.cpp)
endif()
set(SRCS
  generic/src/CustomDataSource.cpp
  ${EXTRA_SOURCE_FILES}
)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  ${SRCS}
)

if(FWE_FEATURE_IWAVE_GPS)
  set(EXTRA_INCLUDE_DIRS ${EXTRA_INCLUDE_DIRS} example/iwavegps/include)
endif()
if(FWE_FEATURE_EXTERNAL_GPS)
  set(EXTRA_INCLUDE_DIRS ${EXTRA_INCLUDE_DIRS} example/externalgps/include)
endif()
target_include_directories(${libraryTargetName} PUBLIC
  generic/include
  ${EXTRA_INCLUDE_DIRS}
)

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

if(FWE_FEATURE_IWAVE_GPS)
  set(EXTRA_INSTALL_FILES ${EXTRA_INSTALL_FILES} example/iwavegps/include/IWaveGpsSource.h)
endif()
if(FWE_FEATURE_EXTERNAL_GPS)
  set(EXTRA_INSTALL_FILES ${EXTRA_INSTALL_FILES} example/externalgps/include/ExternalGpsSource.h)
endif()
install(
  FILES
  generic/include/CustomDataSource.h
  ${EXTRA_INSTALL_FILES}
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

  if(FWE_FEATURE_IWAVE_GPS)
    set(EXTRA_TEST_FILES ${EXTRA_TEST_FILES} example/iwavegps/test/IWaveGpsSourceTest.cpp)
  endif()
  if(FWE_FEATURE_EXTERNAL_GPS)
    set(EXTRA_TEST_FILES ${EXTRA_TEST_FILES} example/externalgps/test/ExternalGpsSourceTest.cpp)
  endif()
  set(
    testSources
    generic/test/CustomDataSourceTest.cpp
    ${EXTRA_TEST_FILES}
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

    # Link to the project library and testing library main
    target_link_libraries(
      ${testName}
      PRIVATE
      ${libraryTargetName}
      IoTFleetWise::TestingSupport
      ${GMOCK_LIB}
    )

    add_test(NAME ${testName} COMMAND ${testName} --gtest_output=xml:report-${testName}.xml)
    add_valgrind_test(${testName} ${CMAKE_CURRENT_SOURCE_DIR}/generic/test/valgrind.supp)
    install(TARGETS ${testName} RUNTIME DESTINATION bin/tests)

  endforeach()
else()
  message(STATUS "Testing not enabled for ${libraryTargetName}")
endif()
