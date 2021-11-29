# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

# Linux platform Implementation. This is configured to compile into one library with
# exposed header files. Very importantly, a general alias is exported, allowing
# applications to add a general platform as a dependency

message(STATUS "Linux platform configuration started.")

# The name used for this library target
set(libraryTargetName iotfleetwise.platform.linux)
# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::Platform)

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

# These are public includes so we can expose the headers to other consumers
target_include_directories(
  ${libraryTargetName}
  PUBLIC
  logmanagement/include
  threadingmanagement/include
  timemanagement/include
  resourcemanagement/include
  persistencymanagement/include
  # Linux Kernel less than 5.10 require this header.
  can
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

