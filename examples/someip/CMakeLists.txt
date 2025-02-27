# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

cmake_minimum_required(VERSION 3.10.2)

project(fwe-example-someip)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(../../cmake/capicxx_gen.cmake)

find_package(Boost 1.71.0 REQUIRED COMPONENTS "system")
find_package(AwsIotFwe REQUIRED)
find_package(vsomeip3 REQUIRED)
find_path(VSOMEIP_INCLUDE_DIR "vsomeip/vsomeip.hpp")
find_package(CommonAPI REQUIRED)
find_package(CommonAPI-SomeIP REQUIRED)

set(CAPICXX_GENERATED_FILES
  v1/commonapi/MySomeipInterface.hpp
  v1/commonapi/MySomeipInterfaceProxy.hpp
  v1/commonapi/MySomeipInterfaceProxyBase.hpp
  v1/commonapi/MySomeipInterfaceSomeIPDeployment.cpp
  v1/commonapi/MySomeipInterfaceSomeIPDeployment.hpp
  v1/commonapi/MySomeipInterfaceSomeIPProxy.cpp
  v1/commonapi/MySomeipInterfaceSomeIPProxy.hpp
  v1/commonapi/MySomeipInterfaceSomeIPStubAdapter.cpp
  v1/commonapi/MySomeipInterfaceSomeIPStubAdapter.hpp
  v1/commonapi/MySomeipInterfaceStub.hpp
  v1/commonapi/MySomeipInterfaceStubDefault.hpp
)
capicxx_generate_someip(
  fidl/MySomeipInterface.fidl
  fidl/MySomeipInterface.fdepl
  "${CAPICXX_GENERATED_FILES}"
)
add_library(fwe-example-someip-capicxx OBJECT
  ${CAPICXX_GENERATED_FILES}
)
target_include_directories(fwe-example-someip-capicxx PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}
)
target_include_directories(fwe-example-someip-capicxx SYSTEM PUBLIC
  ${VSOMEIP_INCLUDE_DIR}
  ${COMMONAPI_INCLUDE_DIRS}
  ${COMMONAPI_SOMEIP_INCLUDE_DIRS}
)
set_target_properties(fwe-example-someip-capicxx
  PROPERTIES POSITION_INDEPENDENT_CODE ON
)

add_executable(fwe-example-someip
  MySomeipDataSource.cpp
  MySomeipInterfaceWrapper.cpp
  main.cpp
)
target_include_directories(fwe-example-someip PUBLIC
  ${AwsIotFwe_INCLUDE_DIR}
  ${VSOMEIP_INCLUDE_DIR}
  ${COMMONAPI_INCLUDE_DIRS}
  ${COMMONAPI_SOMEIP_INCLUDE_DIRS}
)
target_link_libraries(fwe-example-someip
  AwsIotFwe::aws-iot-fleetwise-edge
  fwe-example-someip-capicxx
  CommonAPI-SomeIP
  CommonAPI
  ${VSOMEIP_LIBRARIES}
  Boost::system
)
