# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

cmake_minimum_required(VERSION 3.10.2)

project(iotfleetwise VERSION 1.0.8)

# FWE uses C++14 for compatibility reasons with Automotive middlewares (Adaptive AUTOSAR, ROS2)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Print out the compile commands which is useful for IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# All build and configuration options are prefaced with FWE_
option(FWE_STATIC_LINK "Statically link external libs" OFF)
option(FWE_CODE_COVERAGE "Enables code coverage" OFF)
option(FWE_VALGRIND "Enable valgrind tests" OFF)
option(FWE_BUILD_DOC "Build documentation" ON)
option(FWE_STRIP_SYMBOLS "Strips symbols from output binaries" OFF)
option(FWE_TEST_CLANG_TIDY "Add clang-tidy test" ON)
option(FWE_TEST_FAKETIME "Enable tests that use the faketime library" OFF)
option(FWE_WERROR "Enable -Werror compiler flag" OFF)
option(FWE_SECURITY_COMPILE_FLAGS "Add security related compile options" OFF)
option(FWE_AWS_SDK_SHARED_LIBS "Use AWS SDK shared libs. Needs to be set to the same value of BUILD_SHARED_LIBS that the SDK was compiled with." OFF)
option(FWE_AWS_SDK_EXTRA_LIBS "Extra libs required to link with the AWS SDK. When FWE_STATIC_LINK is ON, setting this to ON will automatically find the standard libs. Can be a space-separated list of libs." ON)
option(FWE_FEATURE_GREENGRASSV2 "Enable Greengrass connection module" OFF)
option(FWE_FEATURE_CUSTOM_DATA_SOURCE "Include the custom data source interface" OFF)
option(FWE_FEATURE_IWAVE_GPS "Include the IWave GPS example for a custom data source" OFF)
option(FWE_FEATURE_EXTERNAL_GPS "Include the external GPS example for a custom data source" OFF)
option(FWE_FEATURE_AAOS_VHAL "Include the Android Automotive VHAL example for a custom data source" OFF)
option(FWE_BUILD_EXECUTABLE "Build the executable, otherwise build a library" ON)
option(FWE_BUILD_ANDROID_SHARED_LIBRARY "Build the android shared library" OFF)
if(FWE_FEATURE_IWAVE_GPS)
  add_compile_options("-DFWE_FEATURE_IWAVE_GPS")
endif()
if(FWE_FEATURE_EXTERNAL_GPS)
  add_compile_options("-DFWE_FEATURE_EXTERNAL_GPS")
endif()
if(FWE_FEATURE_AAOS_VHAL)
  add_compile_options("-DFWE_FEATURE_AAOS_VHAL")
endif()
if(FWE_FEATURE_GREENGRASSV2)
  add_compile_options("-DFWE_FEATURE_GREENGRASSV2")
endif()

# Define the default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(cmake/compiler_gcc.cmake)
include(GNUInstallDirs)
if(FWE_BUILD_DOC)
  include(cmake/graphviz.cmake)
  include(cmake/doxygen.cmake)
endif()
include(CTest)
if(BUILD_TESTING)
  include(cmake/unit_test.cmake)
  include(cmake/valgrind.cmake)
  include(cmake/clang_tidy.cmake)
endif()

# Disallow cycles
set_property(GLOBAL PROPERTY GLOBAL_DEPENDS_NO_CYCLES ON)

# Version
execute_process(COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE VERSION_GIT_SHA
        ERROR_QUIET)
execute_process(COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --tags --abbrev=0
        OUTPUT_VARIABLE VERSION_GIT_TAG
        ERROR_QUIET)
string(STRIP "${VERSION_GIT_SHA}" VERSION_GIT_SHA)
string(STRIP "${VERSION_GIT_TAG}" VERSION_GIT_TAG)
string(TIMESTAMP VERSION_CURRENT_TIME "%Y-%m-%d %H:%M:%S" UTC)
configure_file(src/IoTFleetWiseVersion.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/IoTFleetWiseVersion.cpp)

# Protobuf
find_package(Protobuf REQUIRED)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
  interfaces/protobuf/schemas/cloudToEdge/collection_schemes.proto
  interfaces/protobuf/schemas/cloudToEdge/decoder_manifest.proto
  interfaces/protobuf/schemas/edgeToCloud/checkin.proto
  interfaces/protobuf/schemas/edgeToCloud/vehicle_data.proto
)
add_library(fwe-proto OBJECT
  ${PROTO_SRCS}
  ${PROTO_HDRS}
)
target_include_directories(fwe-proto PUBLIC
  ${Protobuf_INCLUDE_DIRS}
)
# Protobuf does not support -Wall https://github.com/protocolbuffers/protobuf/issues/6781
set_source_files_properties(${PROTO_SRCS} PROPERTIES COMPILE_FLAGS "-Wno-conversion -Wno-pedantic")

set(HEADER_FILES
  src/AwsBootstrap.h
  src/AwsIotChannel.h
  src/AwsIotConnectivityModule.h
  src/AwsSDKMemoryManager.h
  src/CacheAndPersist.h
  src/CANDataConsumer.h
  src/CANDataSource.h
  src/CANDataTypes.h
  src/CANDecoder.h
  src/CANInterfaceIDTranslator.h
  src/Clock.h
  src/ClockHandler.h
  src/CollectionInspectionAPITypes.h
  src/CollectionInspectionEngine.h
  src/CollectionInspectionWorkerThread.h
  src/CollectionSchemeIngestion.h
  src/CollectionSchemeIngestionList.h
  src/CollectionSchemeManagementListener.h
  src/CollectionSchemeManager.h
  src/ConsoleLogger.h
  src/CPUUsageInfo.h
  src/DataReduction.h
  src/DataSenderManager.h
  src/DataSenderManagerWorkerThread.h
  src/DataSenderProtoWriter.h
  src/DecoderManifestIngestion.h
  src/EnumUtility.h
  src/EventTypes.h
  src/ExternalCANDataSource.h
  src/GeohashFunctionNode.h
  src/Geohash.h
  src/GeohashInfo.h
  src/IActiveCollectionSchemesListener.h
  src/IActiveConditionProcessor.h
  src/IActiveDecoderDictionaryListener.h
  src/ICollectionScheme.h
  src/ICollectionSchemeList.h
  src/ICollectionSchemeManager.h
  src/IConnectionTypes.h
  src/IConnectivityChannel.h
  src/IConnectivityModule.h
  src/IDataReadyToPublishListener.h
  src/IDecoderDictionary.h
  src/IDecoderManifest.h
  src/ILogger.h
  src/InspectionEventListener.h
  src/IoTFleetWiseConfig.h
  src/IoTFleetWiseEngine.h
  src/IoTFleetWiseVersion.h
  src/IReceiver.h
  src/ISender.h
  src/ISOTPOverCANOptions.h
  src/ISOTPOverCANReceiver.h
  src/ISOTPOverCANSender.h
  src/ISOTPOverCANSenderReceiver.h
  src/Listener.h
  src/LoggingModule.h
  src/LogLevel.h
  src/MemoryUsageInfo.h
  src/MessageTypes.h
  src/MqttClientWrapper.h
  src/MqttConnectionWrapper.h
  src/OBDDataDecoder.h
  src/OBDDataTypes.h
  src/OBDOverCANECU.h
  src/OBDOverCANModule.h
  src/PayloadManager.h
  src/RemoteProfiler.h
  src/RetryThread.h
  src/Schema.h
  src/SchemaListener.h
  src/Signal.h
  src/SignalTypes.h
  src/Thread.h
  src/Timer.h
  src/TimeTypes.h
  src/TraceModule.h
  src/VehicleDataSourceTypes.h
)

# Source files
set(SRC_FILES
  src/AwsBootstrap.cpp
  src/AwsIotChannel.cpp
  src/AwsIotConnectivityModule.cpp
  src/AwsSDKMemoryManager.cpp
  src/CacheAndPersist.cpp
  src/CANDataConsumer.cpp
  src/CANDataSource.cpp
  src/CANDecoder.cpp
  src/CheckinAndPersistency.cpp
  src/ClockHandler.cpp
  src/CollectionInspectionEngine.cpp
  src/CollectionInspectionWorkerThread.cpp
  src/CollectionSchemeIngestion.cpp
  src/CollectionSchemeIngestionList.cpp
  src/CollectionSchemeManager.cpp
  src/ConsoleLogger.cpp
  src/CPUUsageInfo.cpp
  src/DataSenderManager.cpp
  src/DataSenderManagerWorkerThread.cpp
  src/DataSenderProtoWriter.cpp
  src/DecoderDictionaryExtractor.cpp
  src/DecoderManifestIngestion.cpp
  src/ExternalCANDataSource.cpp
  src/Geohash.cpp
  src/GeohashFunctionNode.cpp
  src/InspectionMatrixExtractor.cpp
  src/IoTFleetWiseConfig.cpp
  src/IoTFleetWiseEngine.cpp
  src/ISOTPOverCANReceiver.cpp
  src/ISOTPOverCANSender.cpp
  src/ISOTPOverCANSenderReceiver.cpp
  src/LoggingModule.cpp
  src/MemoryUsageInfo.cpp
  src/OBDDataDecoder.cpp
  src/OBDOverCANECU.cpp
  src/OBDOverCANModule.cpp
  src/PayloadManager.cpp
  src/RemoteProfiler.cpp
  src/RetryThread.cpp
  src/Schema.cpp
  src/Thread.cpp
  src/TraceModule.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/IoTFleetWiseVersion.cpp
)

# Test files
set(TEST_FILES
  test/unit/AwsIotConnectivityModuleTest.cpp
  test/unit/CacheAndPersistTest.cpp
  test/unit/CANDataSourceTest.cpp
  test/unit/CANDecoderTest.cpp
  test/unit/CheckinAndPersistencyTest.cpp
  test/unit/ClockHandlerBenchmarkTest.cpp
  test/unit/ClockHandlerTest.cpp
  test/unit/CollectionInspectionEngineTest.cpp
  test/unit/CollectionInspectionWorkerThreadTest.cpp
  test/unit/CollectionSchemeManagerGtest.cpp
  test/unit/CollectionSchemeManagerTest.cpp
  test/unit/CPUUsageInfoTest.cpp
  test/unit/DataSenderManagerTest.cpp
  test/unit/DataSenderManagerWorkerThreadTest.cpp
  test/unit/DataSenderProtoWriterTest.cpp
  test/unit/DecoderDictionaryExtractorTest.cpp
  test/unit/ExternalCANDataSourceTest.cpp
  test/unit/GeohashFunctionNodeTest.cpp
  test/unit/GeohashTest.cpp
  test/unit/InspectionMatrixExtractorTest.cpp
  test/unit/IoTFleetWiseConfigTest.cpp
  test/unit/IoTFleetWiseEngineTest.cpp
  test/unit/ISOTPOverCANProtocolTest.cpp
  test/unit/LoggingModuleTest.cpp
  test/unit/MemoryUsageInfoTest.cpp
  test/unit/OBDDataDecoderTest.cpp
  test/unit/OBDOverCANModuleTest.cpp
  test/unit/PayloadManagerTest.cpp
  test/unit/RemoteProfilerTest.cpp
  test/unit/SchemaTest.cpp
  test/unit/ThreadTest.cpp
  test/unit/TimerTest.cpp
  test/unit/TraceModuleTest.cpp
  test/unit/WaitUntilTest.cpp
)

set(BENCHMARK_FILES # Also in list TEST_FILES above, but will be tested using Benchmark
  test/unit/ClockHandlerBenchmarkTest.cpp
)

# Optional files
if(FWE_FEATURE_IWAVE_GPS)
  set(SRC_FILES ${SRC_FILES} src/IWaveGpsSource.cpp)
  set(TEST_FILES ${TEST_FILES} test/unit/IWaveGpsSourceTest.cpp)
  set(HEADER_FILES ${HEADER_FILES} src/IWaveGpsSource.h)
endif()
if(FWE_FEATURE_EXTERNAL_GPS)
  set(SRC_FILES ${SRC_FILES} src/ExternalGpsSource.cpp)
  set(TEST_FILES ${TEST_FILES} test/unit/ExternalGpsSourceTest.cpp)
  set(HEADER_FILES ${HEADER_FILES} src/ExternalGpsSource.h)
endif()
if(FWE_FEATURE_AAOS_VHAL)
  set(SRC_FILES ${SRC_FILES} src/AaosVhalSource.cpp)
  set(TEST_FILES ${TEST_FILES} test/unit/AaosVhalSourceTest.cpp)
  set(HEADER_FILES ${HEADER_FILES} src/AaosVhalSource.h)
endif()
if(FWE_FEATURE_CUSTOM_DATA_SOURCE)
  set(SRC_FILES ${SRC_FILES} src/CustomDataSource.cpp)
  set(TEST_FILES ${TEST_FILES} test/unit/CustomDataSourceTest.cpp)
  set(HEADER_FILES ${HEADER_FILES} src/CustomDataSource.h)
endif()
if(FWE_FEATURE_GREENGRASSV2)
  set(SRC_FILES ${SRC_FILES} src/AwsGGConnectivityModule.cpp src/AwsGGChannel.cpp)
  set(HEADER_FILES ${HEADER_FILES} src/AwsGGChannel.h src/AwsGGConnectivityModule.h)
endif()
if(FWE_TEST_FAKETIME)
  set(TEST_FILES ${TEST_FILES} test/unit/FakeSystemTimeTest.cpp)
  set(FAKETIME_FILES # Also in list TEST_FILES above, but will be tested using FakeTime
    test/unit/FakeSystemTimeTest.cpp
  )
endif()

# Dependencies
set(REQUIRED_BOOST_COMPONENTS "thread;filesystem")
find_package(Boost 1.71.0 REQUIRED COMPONENTS ${REQUIRED_BOOST_COMPONENTS})

find_path(JSONCPP_INCLUDE_DIR "json/json.h" PATH_SUFFIXES "jsoncpp")
find_library(JSONCPP_LIBRARY NAMES jsoncpp)

find_path(SNAPPY_INCLUDE_DIR "snappy.h")
find_library(SNAPPY_LIBRARY NAMES snappy)

# Extra libraries are required to statically link with the AWS SDK. These are not always found by CMake, hence:
# - When FWE_STATIC_LINK is ON and FWE_AWS_SDK_EXTRA_LIBS is ON, automatically find the standard libraries: libcurl, libssl, libcrypto, libz
# - When FWE_AWS_SDK_EXTRA_LIBS is a list of libs, use those
# - When FWE_AWS_SDK_EXTRA_LIBS is OFF, don't link any extra libs
if(FWE_AWS_SDK_EXTRA_LIBS STREQUAL "ON")
  if(NOT FWE_STATIC_LINK)
    set(FWE_AWS_SDK_EXTRA_LIBS "")
  else()
    set(OLD_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
    find_library(CURL_LIBRARY NAMES curl)
    find_library(OPENSSL_SSL_LIBRARY NAMES ssl)
    find_library(OPENSSL_CRYPTO_LIBRARY NAMES crypto)
    find_package(ZLIB REQUIRED)
    set(FWE_AWS_SDK_EXTRA_LIBS "${CURL_LIBRARY} ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} ZLIB::ZLIB")
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_CMAKE_FIND_LIBRARY_SUFFIXES})
  endif()
elseif(FWE_AWS_SDK_EXTRA_LIBS STREQUAL "OFF")
  set(FWE_AWS_SDK_EXTRA_LIBS "")
endif()
separate_arguments(FWE_AWS_SDK_EXTRA_LIBS)

set(REQUIRED_AWS_SDK_COMPONENTS "core")
set(OLD_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS ${FWE_AWS_SDK_SHARED_LIBS})
# Note: this should only be called once
find_package(AWSSDK REQUIRED COMPONENTS ${REQUIRED_AWS_SDK_COMPONENTS})
set(BUILD_SHARED_LIBS ${OLD_BUILD_SHARED_LIBS})

if(FWE_FEATURE_GREENGRASSV2)
  find_package(GreengrassIpc-cpp REQUIRED)
endif()

# Object lib used in output binary and unit tests
add_library(fwe OBJECT
  ${SRC_FILES}
)

# Include dirs
target_include_directories(fwe PUBLIC
  ${JSONCPP_INCLUDE_DIR}
  ${SNAPPY_INCLUDE_DIR}
  ${Protobuf_INCLUDE_DIRS}
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

# Link libraries
target_link_libraries(fwe
  $<$<BOOL:${FWE_FEATURE_GREENGRASSV2}>:AWS::GreengrassIpc-cpp>
  ${AWSSDK_LINK_LIBRARIES}
  ${FWE_AWS_SDK_EXTRA_LIBS}
  ${SNAPPY_LIBRARY}
  ${JSONCPP_LIBRARY}
  ${Protobuf_LIBRARIES}
  Boost::thread
  Boost::filesystem
)

# Executable or library output binary
if(FWE_BUILD_EXECUTABLE)
  add_executable(aws-iot-fleetwise-edge
    src/main.cpp
  )
  install(TARGETS aws-iot-fleetwise-edge
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
  target_link_libraries(aws-iot-fleetwise-edge
    -Xlinker
    -Map=aws-iot-fleetwise-edge.map
  )
elseif(FWE_BUILD_ANDROID_SHARED_LIBRARY)
  add_library(aws-iot-fleetwise-edge SHARED
    src/android_shared_library.cpp
  )
  target_link_libraries(aws-iot-fleetwise-edge
    log
    android
  )
else()
  add_library(aws-iot-fleetwise-edge
    $<TARGET_OBJECTS:fwe>
  )
  set_target_properties(aws-iot-fleetwise-edge
    PROPERTIES PUBLIC_HEADER "${HEADER_FILES};${PROTO_HDRS}"
  )
  install(TARGETS aws-iot-fleetwise-edge fwe fwe-proto
    EXPORT AwsIotFweTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    PUBLIC_HEADER DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/aws/iotfleetwise"
  )
  install(EXPORT AwsIotFweTargets
        FILE AwsIotFweTargets.cmake
        NAMESPACE AwsIotFwe::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AwsIotFwe
  )
  include(CMakePackageConfigHelpers)
  configure_package_config_file(cmake/AwsIotFweConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/AwsIotFweConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AwsIotFwe
  )
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/AwsIotFweConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
  )
  install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/AwsIotFweConfig.cmake
          ${CMAKE_CURRENT_BINARY_DIR}/AwsIotFweConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/AwsIotFwe
  )
endif()
target_link_libraries(aws-iot-fleetwise-edge
  fwe
  fwe-proto
)
set_target_properties(aws-iot-fleetwise-edge fwe fwe-proto
  PROPERTIES POSITION_INDEPENDENT_CODE ON
)

# Unit tests
if(${BUILD_TESTING})
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/unit/support/static-config-ok.json           DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/unit/support/static-config-corrupt.json      DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/unit/support/static-config-inline-creds.json DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  find_package(benchmark REQUIRED)
  find_package(GTest REQUIRED)
  find_library(GMOCK_LIB NAMES gmock)
  foreach(TEST_FILE ${TEST_FILES})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
    set(TEST_SRC ${TEST_FILE})
    set(TEST_LIBS
      fwe
      fwe-proto
    )
    if(${TEST_FILE} IN_LIST BENCHMARK_FILES)
      set(TEST_LIBS ${TEST_LIBS} benchmark::benchmark)
    else()
      set(TEST_SRC ${TEST_SRC} test/unit/support/main.cpp)
      set(TEST_LIBS ${TEST_LIBS} ${GMOCK_LIB} GTest::GTest)
    endif()
    add_executable(${TEST_NAME} ${TEST_SRC})
    target_include_directories(${TEST_NAME} PUBLIC
      src
      test/unit/support
      ${CMAKE_CURRENT_BINARY_DIR}
    )
    target_link_libraries(${TEST_NAME} ${TEST_LIBS})
    if(${TEST_FILE} IN_LIST FAKETIME_FILES)
      if(FWE_TEST_FAKETIME)
        add_unit_test_with_faketime(${TEST_NAME})
      endif()
    elseif(${TEST_FILE} IN_LIST BENCHMARK_FILES)
      add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME} --benchmark_out=benchmark-report-${TEST_NAME}.txt --benchmark_out_format=console)
    else()
      add_unit_test(${TEST_NAME})
      add_valgrind_test(${TEST_NAME})
    endif()
  endforeach()
endif()
