# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
set(libraryTargetName iotfleetwise.datamanagement.types)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::DataManagementTypes)

set(SRCS
    src/Geohash.cpp
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
)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### Install ###

install(TARGETS ${libraryTargetName} DESTINATION lib)
install(
  FILES
  include/CollectionInspectionAPITypes.h
  include/CANDataTypes.h
  include/EventTypes.h
  include/Geohash.h
  include/GeohashInfo.h
  include/MessageTypes.h
  include/OBDDataTypes.h
  include/SignalTypes.h
  DESTINATION
  include)

### Tests ###
# This is a list of tests that will compiled.
# If adding a test, simply add the source file here
if(${BUILD_TESTING})
  message(STATUS "Building tests for ${libraryTargetName}")

  set(
    testSources
    test/GeohashTest.cpp
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
    add_test(NAME ${testName} COMMAND ${testName} --gtest_output=xml:report-${testName}.xml)
    add_valgrind_test(${testName})
    install(TARGETS ${testName} RUNTIME DESTINATION bin/tests)

  endforeach()
else()
  message(STATUS "Testing not enabled for ${libraryTargetName}")
endif()
