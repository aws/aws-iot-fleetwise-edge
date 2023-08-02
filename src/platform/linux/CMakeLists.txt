# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

# Linux platform Implementation. This is configured to compile into one library with
# exposed header files. Very importantly, a general alias is exported, allowing
# applications to add a general platform as a dependency

message(STATUS "Linux platform configuration started.")

# The name used for this library target
set(libraryTargetName iotfleetwise.platform.linux)
# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::Platform::Linux)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  logmanagement/src/ConsoleLogger.cpp
  logmanagement/src/LoggingModule.cpp
  logmanagement/src/TraceModule.cpp
  threadingmanagement/src/Thread.cpp
  timemanagement/src/ClockHandler.cpp
  resourcemanagement/src/MemoryUsageInfo.cpp
  resourcemanagement/src/CPUUsageInfo.cpp
  persistencymanagement/src/CacheAndPersist.cpp
)

find_path(JSONCPP_INCLUDE_DIR "json/json.h" PATH_SUFFIXES "jsoncpp")
find_library(JSONCPP_LIBRARY NAMES jsoncpp)
find_package(Boost 1.65.1 REQUIRED COMPONENTS filesystem)

# These are public includes so we can expose the headers to other consumers
target_include_directories(
  ${libraryTargetName}
  PUBLIC
  logmanagement/include
  threadingmanagement/include
  timemanagement/include
  resourcemanagement/include
  persistencymanagement/include
  ${JSONCPP_INCLUDE_DIR}
)

target_link_libraries(
  ${libraryTargetName}
  # From the Platform, this is what is used: logmanagement timemanagement
  IoTFleetWise::Platform::Utility
  Boost::filesystem
  ${JSONCPP_LIBRARY}
)

# This allows the preprocessor to enable the code in the libraries
target_compile_definitions(
  ${libraryTargetName}
  PUBLIC
)

# We create an alias to a general platform library so that consuming apps
# do not need to be coupled to a specific implementation of the platform
add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### INSTALL ###

install(
  TARGETS ${libraryTargetName}
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
  ARCHIVE DESTINATION lib
)

# Currently not necessary, but if we want to install the headers, here's how
install(
  FILES
  threadingmanagement/include/Listener.h
  threadingmanagement/include/Signal.h
  threadingmanagement/include/Thread.h
  timemanagement/include/ClockHandler.h
  timemanagement/include/Timer.h
  timemanagement/include/Clock.h
  resourcemanagement/include/CPUUsageInfo.h
  resourcemanagement/include/MemoryUsageInfo.h
  logmanagement/include/LoggingModule.h
  logmanagement/include/ConsoleLogger.h
  logmanagement/include/LogLevel.h
  persistencymanagement/include/CacheAndPersist.h
  DESTINATION include
)

### Tests ###

# This is a list of tests that will compiled.
# If adding a test, simply add the source file here
set(
  testSources
  logmanagement/test/TraceModuleTest.cpp
  threadingmanagement/test/ThreadTest.cpp
  timemanagement/test/TimerTest.cpp
  timemanagement/test/ClockHandlerTest.cpp
  resourcemanagement/test/CPUUsageInfoTest.cpp
  resourcemanagement/test/MemoryUsageInfoTest.cpp
  persistencymanagement/test/CacheAndPersistTest.cpp
)

set(
  benchmarkSources
  timemanagement/test/ClockHandlerBenchmarkTest.cpp
)


if(${BUILD_TESTING})
  message(STATUS "Building tests for ${libraryTargetName}")

  find_package(benchmark REQUIRED)

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

  # Add the executable benchmark targets
  foreach(testSource ${benchmarkSources})
    # Need a name for each exec so use filename w/o extension
    get_filename_component(testName ${testSource} NAME_WE)

    add_executable(${testName} ${testSource})

    # Link to the project library and testing library main
    target_link_libraries(
      ${testName}
      PRIVATE
      ${libraryTargetName}
      benchmark::benchmark
    )

    add_test(NAME ${testName} COMMAND ${testName} --benchmark_out=benchmark-report-${testName}.txt --benchmark_out_format=console)
    install(TARGETS ${testName} RUNTIME DESTINATION bin/tests)

  endforeach()
else()
  message(STATUS "Testing not enabled for ${libraryTargetName}")
endif()
