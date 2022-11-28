# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

set(libraryTargetName iotfleetwise.offboardconnectivityawsbootstrap)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::OffboardConnectivityAwsBootstrap)

set(librarySrc
  src/AwsBootstrap.cpp
  src/AwsSDKMemoryManager.cpp
)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  ${librarySrc}
)

set(OLD_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS ${FWE_AWS_SDK_SHARED_LIBS})
find_package(AWSSDK REQUIRED COMPONENTS core)
set(BUILD_SHARED_LIBS ${OLD_BUILD_SHARED_LIBS})

target_include_directories(${libraryTargetName} PUBLIC include)

target_link_libraries(
  ${libraryTargetName}
  IoTFleetWise::Platform::Linux
  ${AWSSDK_LINK_LIBRARIES}
  ${FWE_AWS_SDK_EXTRA_LIBS}
)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})
