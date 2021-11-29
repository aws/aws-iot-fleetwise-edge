# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
set(libraryTargetName iotfleetwise.datainspection)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::Datainspection)

set(SRCS
  src/CollectionInspectionEngine.cpp
  src/CollectionInspectionWorkerThread.cpp
  src/location/Geohash.cpp
  src/location/GeohashFunctionNode.cpp
  src/diag/OBDOverCANSessionManager.cpp
  src/diag/OBDOverCANModule.cpp
  src/vehiclenetwork/NetworkChannelBinder.cpp
  src/vehiclenetwork/NetworkChannelConsumer.cpp
  $<$<BOOL:${FWE_FEATURE_CAMERA}>:src/dds/DataOverDDSModule.cpp>
)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  ${SRCS}
)


find_path(JSONCPP_INCLUDE_DIR "json/json.h" PATH_SUFFIXES "jsoncpp")
find_library(JSONCPP_LIBRARY NAMES jsoncpp)

target_include_directories(${libraryTargetName} PUBLIC
  include
  ${JSONCPP_INCLUDE_DIR}
)

target_link_libraries(
  ${libraryTargetName}
  IoTFleetWise::Platform
  IoTFleetWise::Vehiclenetwork
  IoTFleetWise::Datamanagement
  ${JSONCPP_LIBRARY}
)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### Install ###

install(TARGETS ${libraryTargetName} DESTINATION lib)

install(
  FILES
  include/Geohash.h
  include/GeohashFunctionNode.h
  include/NetworkChannelConsumerListener.h
  include/NetworkChannelBinder.h
  include/INetworkChannelConsumer.h
  include/NetworkChannelConsumer.h
  include/OBDOverCANSessionManager.h
  include/OBDOverCANModule.h
  $<$<BOOL:${FWE_FEATURE_CAMERA}>:include/DataOverDDSModule.h>
  DESTINATION include
)


