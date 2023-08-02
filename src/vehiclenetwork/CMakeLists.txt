# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

set(libraryTargetName iotfleetwise.vehiclenetwork)
# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::Vehiclenetwork)


set(SRCS
  src/ISOTPOverCANReceiver.cpp
  src/ISOTPOverCANSender.cpp
  src/ISOTPOverCANSenderReceiver.cpp
)

add_library(
  ${libraryTargetName}
  # Leave STATIC or SHARED blank to allow for BUILD_SHARED_LIBS option
  ${SRCS}
)


# Adding the include directory which contains two subdirectories. Users of this
# library will have to use the subdirectory name to include headers from this
# library, for example: #include "businterfaces/VehicleDataSource.h"
target_include_directories(${libraryTargetName} PUBLIC include)

find_package(Boost 1.71.0 REQUIRED COMPONENTS thread)

set(LIBS
  IoTFleetWise::Platform::Linux
  IoTFleetWise::Platform::Utility
  Boost::thread
)

target_link_libraries(
  ${libraryTargetName}
  PUBLIC
  ${LIBS}
)


add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### Install ###

install(TARGETS ${libraryTargetName} DESTINATION lib)

install(
  FILES
  include/businterfaces/ISOTPOverCANReceiver.h
  include/businterfaces/ISOTPOverCANSender.h
  include/businterfaces/ISOTPOverCANSenderReceiver.h
  DESTINATION
  include
)

install(
  FILES
  include/datatypes/VehicleDataSourceTypes.h
  include/datatypes/ISOTPOverCANOptions.h
  DESTINATION
  include/datatypes
)

### Tests ###
# This is a list of tests that will compiled.
# If adding a test, simply add the source file here
set(
  testSources
  test/ISOTPOverCANProtocolTest.cpp
)

if(${BUILD_TESTING})
  message(STATUS "Building tests for ${libraryTargetName}")

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
