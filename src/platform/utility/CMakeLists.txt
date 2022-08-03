# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

set(libraryTargetName iotfleetwise.platform.utility)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::Platform::Utility)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  src/EnumUtility.cpp
)

target_include_directories(${libraryTargetName} PUBLIC include)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### Install ###

# No Install targets necessary

### Tests ###

if(${BUILD_TESTING})
# None so far
endif()

