# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

set(libraryTargetName iotfleetwise.vehiclenetwork)
# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::Vehiclenetwork)


set(SRCS
  src/SocketCANBusChannel.cpp
  src/CANRawMessage.cpp
  src/NetworkChannelBridgeListener.cpp
  src/ISOTPOverCANReceiver.cpp
  src/ISOTPOverCANSender.cpp
  src/ISOTPOverCANSenderReceiver.cpp
  # Camera related
  $<$<BOOL:${FWE_FEATURE_CAMERA}>:src/CameraDataSubscriber.cpp>
  $<$<BOOL:${FWE_FEATURE_CAMERA}>:src/CameraDataPublisher.cpp>
)

add_library(
  ${libraryTargetName}
  # Leave STATIC or SHARED blank to allow for BUILD_SHARED_LIBS option
  ${SRCS}
)


# Adding the include directory which contains two subdirectories. Users of this
# library will have to use the subdirectory name to include headers from this
# library, for example: #include "businterfaces/NetworkChannelBridgeListener.h"
target_include_directories(${libraryTargetName} PUBLIC include)

find_package(Boost 1.65.1 REQUIRED COMPONENTS thread)
if(FWE_FEATURE_CAMERA)
  find_package(fastrtps REQUIRED)
  find_package(fastcdr REQUIRED)
endif()

set(LIBS
  IoTFleetWise::Platform
  Boost::thread
  $<$<BOOL:${FWE_FEATURE_CAMERA}>:IoTFleetWise::Idls>
  $<$<BOOL:${FWE_FEATURE_CAMERA}>:fastrtps>
  $<$<BOOL:${FWE_FEATURE_CAMERA}>:fastcdr>
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
  include/businterfaces/INetworkChannelBridge.h
  include/businterfaces/NetworkChannelBridgeListener.h
  include/businterfaces/SocketCANBusChannel.h
  include/businterfaces/ISOTPOverCANReceiver.h
  include/businterfaces/ISOTPOverCANSender.h
  include/businterfaces/ISOTPOverCANSenderReceiver.h
  DESTINATION
  include
)

install(
  FILES
  include/datatypes/CANRawMessage.h
  include/datatypes/NetworkChannelDataTypes.h
  include/datatypes/ISOTPOverCANOptions.h
  DESTINATION
  include/datatypes
)

if (FWE_FEATURE_CAMERA)
  install(
    FILES
    include/dds/SensorDataListener.h
    include/dds/DDSDataTypes.h
    include/dds/IDDSPublisher.h
    include/dds/IDDSSubscriber.h
    include/dds/CameraDataSubscriber.h
    include/dds/CameraDataPublisher.h
    DESTINATION
    include/dds
  )
endif()

### Tests ###
# This is a list of tests that will compiled.
# If adding a test, simply add the source file here
set(
  testSources
  test/SocketCANBusChannelTest.cpp
  test/ISOTPOverCANProtocolTest.cpp
  test/CANRawMessageTest.cpp
)

if(FWE_FEATURE_CAMERA)
  set(
    testSources
    ${testSources}
    test/CameraDataSubscriberTest.cpp
    test/CameraDataPublisherTest.cpp
  )
endif()

if(${BUILD_TESTING})
  message(STATUS "Building tests for ${libraryTargetName}")
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/CameraSubscriberTestPNG.png
    DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  find_package(GTest REQUIRED)

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
    )
    add_test(NAME ${testName} COMMAND ${testName} --gtest_output=xml:report-${testName}.xml)
    add_valgrind_test(${testName})
    install(TARGETS ${testName} RUNTIME DESTINATION bin/tests)

  endforeach()
else()
  message(STATUS "Testing not enabled for ${libraryTargetName}")
endif()
