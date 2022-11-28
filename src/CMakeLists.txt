# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

# In this configuration file, applications and libraries are added to the
# compilation configuration. This is where CMake options can be added to switch
# parts on/off

option(FWE_BUILD_PLATFORM "Build a Platform, type based on PLATFORM_TYPE." ON)
option(FWE_BUILD_VEHICLE_NETWORK "Build the vehiclenetwork library" ON)
option(FWE_BUILD_DATA_MANAGEMENT "Build the datamanagement library" ON)
option(FWE_BUILD_APP_TEST "Builds the sample app-test" ON)
option(FWE_BUILD_EXECUTION_MANAGEMENT "Build the Execution management library and application" ON)
option(FWE_BUILD_CONNECTIVITY "Build the off board connectivity module library" ON)


# set(CMAKE_CXX_CLANG_TIDY clang-tidy)

if(FWE_BUILD_PLATFORM)
  add_subdirectory(platform)
endif()

if(FWE_BUILD_VEHICLE_NETWORK)
  add_subdirectory(vehiclenetwork)
endif()

if(FWE_BUILD_DATA_MANAGEMENT)
  add_subdirectory(datamanagement)
endif()

if(FWE_BUILD_EXECUTION_MANAGEMENT)
  add_subdirectory(executionmanagement)
endif()

if(FWE_BUILD_CONNECTIVITY)
  add_subdirectory(offboardconnectivity)
endif()

if(BUILD_TESTING)
  add_subdirectory(testingsupport)
endif()
