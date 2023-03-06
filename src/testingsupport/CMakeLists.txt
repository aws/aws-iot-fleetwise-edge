# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

set(libraryTargetName iotfleetwise.testingsupport)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::TestingSupport)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  src/main.cpp
)

find_package(GTest REQUIRED)

target_include_directories(${libraryTargetName}
  PUBLIC
  include
)

target_link_libraries(
  ${libraryTargetName}
  IoTFleetWise::Platform::Linux
  GTest::GTest
)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### Install ###

install(TARGETS ${libraryTargetName} DESTINATION lib)

install(
  FILES
  include/Faketime.h
  include/Testing.h
  include/WaitUntil.h
  DESTINATION include
)

### Tests ###

if(${BUILD_TESTING})
  message(STATUS "Building tests for ${libraryTargetName}")

  if(FWE_TEST_FAKETIME)
    set(EXTRA_TEST_FILES ${EXTRA_TEST_FILES} test/FakeSystemTimeTest.cpp)
  endif()
  set(
    testSources
    test/WaitUntilTest.cpp
    ${EXTRA_TEST_FILES}
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

    if(FWE_TEST_FAKETIME)
      add_unit_test_with_faketime(${testName})
    endif()
    install(TARGETS ${testName} RUNTIME DESTINATION bin/tests)

  endforeach()

endif()
