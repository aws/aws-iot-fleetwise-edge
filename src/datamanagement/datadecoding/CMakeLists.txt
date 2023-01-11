# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

set(libraryTargetName iotfleetwise.datadecoding)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::DataDecoding)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  src/CANDecoder.cpp
  src/DecoderManifestIngestion.cpp
  src/OBDDataDecoder.cpp
)

target_include_directories(${libraryTargetName} PUBLIC
  include
)

find_package(Boost 1.65.1 REQUIRED COMPONENTS filesystem)

target_link_libraries(
  ${libraryTargetName}
  Boost::filesystem
  IoTFleetWise::DataCollection
  #IoTFleetWise::DataManagementTypes
  IoTFleetWise::Platform::Linux
  IoTFleetWise::Vehiclenetwork
)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### Install ###
install(TARGETS ${libraryTargetName} DESTINATION lib)
install(
  FILES
  include/CANDecoder.h
  include/DecoderManifestIngestion.h
  include/IActiveDecoderDictionaryListener.h
  include/IDecoderDictionary.h
  include/IDecoderManifest.h
  include/OBDDataDecoder.h
  DESTINATION include
)

### Tests ###

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
      test/CANDecoderTest.cpp
      test/OBDDataDecoderTest.cpp
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

    add_test(NAME ${testName} COMMAND ${testName} --gtest_output=xml:report-${testName}.xml)
    add_valgrind_test(${testName})
    install(TARGETS ${testName} RUNTIME DESTINATION bin/tests)

  endforeach()

endif()
