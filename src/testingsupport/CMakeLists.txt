# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

set(libraryTargetName iotfleetwise.testingsupport)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::TestingSupport)

add_library(
  ${libraryTargetName}
  INTERFACE # Note: Header only library - remove this line if sources are added
)

target_include_directories(${libraryTargetName}
  INTERFACE # Note: Header only library - remove this line if sources are added above
  include
)

target_link_libraries(
  ${libraryTargetName}
  INTERFACE # Note: Header only library - remove this line if sources are added above
  IoTFleetWise::Platform::Linux
)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### Install ###

install(TARGETS ${libraryTargetName} DESTINATION lib)

install(
  FILES
  include/Faketime.h
  DESTINATION include
)

### Tests ###

if(${BUILD_TESTING} AND FWE_TEST_FAKETIME)
  message(STATUS "Building tests for ${libraryTargetName}")

  find_package(GTest REQUIRED)

  set(
    testSources
    test/FakeSystemTimeTest.cpp
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
    )

    add_unit_test_with_faketime(${testName})
    install(TARGETS ${testName} RUNTIME DESTINATION bin/tests)

  endforeach()

endif()

