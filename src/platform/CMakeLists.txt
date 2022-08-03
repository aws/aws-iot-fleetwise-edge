# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

# AWS IoT FleetWise Platforms. This configuration file selects the Platform
# for the build based on the setting of the PLATFORM_TYPE option.

# This is a list of available platforms
set(
  platformTypes
  # Supported Platforms:
  Linux
  # Yocto
  # AwesomeCarHardware1
  # AwesomeCarHardware2
)

# Because CMake only supports boolean options, PLATFORM_TYPE is set as CACHE STRING
set(
  PLATFORM_TYPE Linux
  CACHE STRING 
  "The type of platform selected for the build. Currently only Linux is support."
)

# Allowable values for PLATFORM_TYPE to be the ones listed in the platformTypes variable
set_property(CACHE PLATFORM_TYPE PROPERTY STRINGS ${platformTypes})

# Enforce a valid setting
if(NOT PLATFORM_TYPE IN_LIST platformTypes)
  message(FATAL_ERROR "PLATFORM_TYPE must be one of ${platformTypes}")
endif()

# Add the selected platform subdirectory
if(${PLATFORM_TYPE} STREQUAL Linux)
  message(STATUS "Adding Linux platform")
  add_subdirectory( linux )
endif()

add_subdirectory( utility )
