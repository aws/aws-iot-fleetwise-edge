# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

set(libraryTargetName iotfleetwise.datamanagement)

# The alias name is what other targets will use as a dependency
set(libraryAliasName IoTFleetWise::Datamanagement)

add_library(
  ${libraryTargetName}
  # STATIC or SHARED left out to depend on BUILD_SHARED_LIBS
  src/CANDecoder.cpp
  src/CollectionScheme.cpp
  src/CollectionSchemeJSONParser.cpp
  src/DataCollectionJSONWriter.cpp
  src/DataCollectionProtoWriter.cpp
  src/DataCollectionSender.cpp
  src/OBDDataDecoder.cpp
  src/Schema.cpp
  src/CollectionSchemeIngestion.cpp
  src/DecoderManifestIngestion.cpp
  src/CollectionSchemeIngestionList.cpp
  src/CollectionSchemeManager/CollectionSchemeManager.cpp
  src/CollectionSchemeManager/CheckinAndPersistency.cpp
  src/CollectionSchemeManager/DecoderDictionaryExtractor.cpp
  src/CollectionSchemeManager/InspectionMatrixExtractor.cpp
)

find_path(JSONCPP_INCLUDE_DIR "json/json.h" PATH_SUFFIXES "jsoncpp")
find_library(JSONCPP_LIBRARY NAMES jsoncpp)

target_include_directories(${libraryTargetName} PUBLIC
  include
  ${JSONCPP_INCLUDE_DIR}
)

target_link_libraries(
  ${libraryTargetName}
  # From the Platform, this is what is used: logmanagement timemanagement
  IoTFleetWise::Platform
  IoTFleetWise::Vehiclenetwork
  ${JSONCPP_LIBRARY}
  stdc++fs
  IoTFleetWise::Proto
  IoTFleetWise::OffboardConnectivity
  IoTFleetWise::Datainspection
)

add_library(${libraryAliasName} ALIAS ${libraryTargetName})

### Install ###

# No Install targets necessary

