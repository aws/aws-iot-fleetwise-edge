# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.

cmake_minimum_required(VERSION 3.10.2)

project(iotfleetwise VERSION 1.3.2)

# FWE uses C++14 for compatibility reasons with Automotive middlewares (Adaptive AUTOSAR, ROS2)
# Note: When built with FWE_FEATURE_ROS2, colcon will override these settings
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Print out the compile commands which is useful for IDEs
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# All build and configuration options are prefaced with FWE_
option(FWE_STATIC_LINK "Statically link external libs" OFF)
option(FWE_CODE_COVERAGE "Enables code coverage" OFF)
option(FWE_VALGRIND "Enable valgrind tests" OFF)
option(FWE_STRIP_SYMBOLS "Strips symbols from output binaries" OFF)
option(FWE_TEST_CLANG_TIDY "Add clang-tidy test" ON)
option(FWE_TEST_FAKETIME "Enable tests that use the faketime library" OFF)
option(FWE_WERROR "Enable -Werror compiler flag" OFF)
option(FWE_SECURITY_COMPILE_FLAGS "Add security related compile options" OFF)
option(FWE_AWS_SDK_SHARED_LIBS "Use AWS SDK shared libs. Needs to be set to the same value of BUILD_SHARED_LIBS that the SDK was compiled with." OFF)
option(FWE_AWS_SDK_EXTRA_LIBS "Extra libs required to link with the AWS SDK. When FWE_STATIC_LINK is ON, setting this to ON will automatically find the standard libs. Can be a space-separated list of libs." ON)
option(FWE_FEATURE_GREENGRASSV2 "Enable Greengrass connection module" OFF)
option(FWE_FEATURE_IWAVE_GPS "Include the IWave GPS example" OFF)
option(FWE_FEATURE_EXTERNAL_GPS "Include the external GPS example" OFF)
option(FWE_FEATURE_AAOS_VHAL "Include the Android Automotive VHAL example" OFF)
option(FWE_FEATURE_S3 "Internal option, not useful on its own. Enables usage of AWS credential provider, and uploading and downloading from S3." OFF)
option(FWE_FEATURE_SCRIPT_ENGINE "Internal option, not useful on its own. Enables usage of CustomFunctionScriptEngine. Implies FWE_FEATURE_S3" OFF)
option(FWE_FEATURE_VISION_SYSTEM_DATA "Include support for vision-system-data sources. Implies FWE_FEATURE_S3." OFF)
option(FWE_FEATURE_ROS2 "Include support for ROS2 as a vision-system-data source. Implies FWE_FEATURE_VISION_SYSTEM_DATA." OFF)
option(FWE_FEATURE_SOMEIP "Include the example SOME/IP data source, the example SOME/IP to CAN bridge, the example SOME/IP command dispatcher, and the example SOME/IP to device shadow proxy. Implies FWE_FEATURE_REMOTE_COMMANDS." OFF)
option(FWE_FEATURE_REMOTE_COMMANDS "Include support for remote commands" OFF)
option(FWE_FEATURE_LAST_KNOWN_STATE "Include support for last known state. Implies FWE_FEATURE_REMOTE_COMMANDS." OFF)
option(FWE_FEATURE_STORE_AND_FORWARD "Include support for store and forward" OFF)
option(FWE_FEATURE_CUSTOM_FUNCTION_EXAMPLES "Include the custom function examples" OFF)
option(FWE_FEATURE_MICROPYTHON "Include support for MicroPython. Implies FWE_FEATURE_SCRIPT_ENGINE." OFF)
option(FWE_FEATURE_CPYTHON "Include support for CPython. FOR TESTING PURPOSES ONLY, NOT FOR PRODUCTION SYSTEMS. Implies FWE_FEATURE_SCRIPT_ENGINE." OFF)
option(FWE_FEATURE_UDS_DTC "Enable UDS DTC information collection" OFF)
option(FWE_FEATURE_UDS_DTC_EXAMPLE "Include example UDS DTC interface for collecting UDS DTC information. Implies FWE_FEATURE_UDS_DTC." OFF)
option(FWE_BUILD_DOC "Build documentation" ON)
option(FWE_BUILD_EXECUTABLE "Build the executable, otherwise build a library" ON)
option(FWE_BUILD_ANDROID_SHARED_LIBRARY "Build the android shared library" OFF)
option(FWE_BUILD_TOOLS "Build helper tools" ON)

if(FWE_FEATURE_ROS2)
  set(FWE_FEATURE_VISION_SYSTEM_DATA ON FORCE)
  find_package(ament_cmake REQUIRED)
  find_package(rclcpp REQUIRED)
  find_package(fastcdr REQUIRED)
  ament_package()
endif()
if(FWE_FEATURE_VISION_SYSTEM_DATA)
  set(FWE_FEATURE_S3 ON FORCE)
endif()
if(FWE_FEATURE_SOMEIP)
  set(FWE_FEATURE_REMOTE_COMMANDS ON FORCE)
endif()
if(FWE_FEATURE_LAST_KNOWN_STATE)
  set(FWE_FEATURE_REMOTE_COMMANDS ON FORCE)
endif()
if(FWE_FEATURE_STORE_AND_FORWARD)
  find_path(STORE_LIBRARY_CPP_INCLUDE_DIR "stream/stream.hpp" PATH_SUFFIXES "aws/store")
  find_library(STORE_LIBRARY_CPP_STREAM NAMES stream)
  find_library(STORE_LIBRARY_CPP_KV NAMES kv)
endif()
if(FWE_FEATURE_UDS_DTC_EXAMPLE)
  set(FWE_FEATURE_UDS_DTC ON FORCE)
endif()
if(FWE_FEATURE_MICROPYTHON)
  set(FWE_FEATURE_SCRIPT_ENGINE ON FORCE)
endif()
if(FWE_FEATURE_CPYTHON)
  set(FWE_FEATURE_SCRIPT_ENGINE ON FORCE)
endif()
if(FWE_FEATURE_SCRIPT_ENGINE)
  set(FWE_FEATURE_S3 ON FORCE)
endif()

if(NOT FWE_BUILD_EXECUTABLE AND FWE_FEATURE_SOMEIP)
  message(FATAL_ERROR "Not possible to build library with the example SOME/IP interfaces, see examples/someip instead")
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
if(FWE_FEATURE_SOMEIP)
  include(cmake/capicxx_gen.cmake)
endif()

# Disallow cycles
set_property(GLOBAL PROPERTY GLOBAL_DEPENDS_NO_CYCLES ON)

# Version
execute_process(COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE FWE_VERSION_GIT_SHA
        ERROR_QUIET)
execute_process(COMMAND git -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --tags --abbrev=0
        OUTPUT_VARIABLE FWE_VERSION_GIT_TAG
        ERROR_QUIET)
string(STRIP "${FWE_VERSION_GIT_SHA}" FWE_VERSION_GIT_SHA)
string(STRIP "${FWE_VERSION_GIT_TAG}" FWE_VERSION_GIT_TAG)
string(TIMESTAMP FWE_VERSION_CURRENT_TIME "%Y-%m-%d %H:%M:%S" UTC)
configure_file(src/IoTFleetWiseVersion.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/IoTFleetWiseVersion.cpp)

# Protobuf
find_package(Protobuf REQUIRED)
set(PROTO_FILES
  interfaces/protobuf/schemas/cloudToEdge/common_types.proto
  interfaces/protobuf/schemas/cloudToEdge/collection_schemes.proto
  interfaces/protobuf/schemas/cloudToEdge/decoder_manifest.proto
  interfaces/protobuf/schemas/edgeToCloud/checkin.proto
  interfaces/protobuf/schemas/edgeToCloud/vehicle_data.proto
)
if(FWE_FEATURE_REMOTE_COMMANDS)
  set(PROTO_FILES ${PROTO_FILES}
    interfaces/protobuf/schemas/cloudToEdge/command_request.proto
    interfaces/protobuf/schemas/edgeToCloud/command_response.proto
  )
endif()
if(FWE_FEATURE_LAST_KNOWN_STATE)
  set(PROTO_FILES ${PROTO_FILES}
    interfaces/protobuf/schemas/cloudToEdge/state_templates.proto
    interfaces/protobuf/schemas/edgeToCloud/last_known_state_data.proto
  )
endif()

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})
add_library(fwe-proto OBJECT
  ${PROTO_SRCS}
  ${PROTO_HDRS}
)
target_include_directories(fwe-proto PUBLIC
  ${Protobuf_INCLUDE_DIRS}
)
# Protobuf does not support -Wall https://github.com/protocolbuffers/protobuf/issues/6781
set_source_files_properties(${PROTO_SRCS} PROPERTIES COMPILE_FLAGS "-Wno-conversion -Wno-pedantic")

if(FWE_FEATURE_SOMEIP)
  find_package(vsomeip3 REQUIRED)
  find_path(VSOMEIP_INCLUDE_DIR "vsomeip/vsomeip.hpp")
  find_package(CommonAPI REQUIRED)
  find_package(CommonAPI-SomeIP REQUIRED)
  # For someipigen:
  find_package(pybind11 CONFIG REQUIRED)

  # CommonAPI code generation
  set(CAPICXX_EXAMPLE_GENERATED_FILES
    v1/commonapi/CommonTypes.hpp
    v1/commonapi/CommonTypesSomeIPDeployment.cpp
    v1/commonapi/CommonTypesSomeIPDeployment.hpp
    v1/commonapi/ExampleSomeipInterface.hpp
    v1/commonapi/ExampleSomeipInterfaceProxy.hpp
    v1/commonapi/ExampleSomeipInterfaceProxyBase.hpp
    v1/commonapi/ExampleSomeipInterfaceSomeIPDeployment.cpp
    v1/commonapi/ExampleSomeipInterfaceSomeIPDeployment.hpp
    v1/commonapi/ExampleSomeipInterfaceSomeIPProxy.cpp
    v1/commonapi/ExampleSomeipInterfaceSomeIPProxy.hpp
    v1/commonapi/ExampleSomeipInterfaceSomeIPStubAdapter.cpp
    v1/commonapi/ExampleSomeipInterfaceSomeIPStubAdapter.hpp
    v1/commonapi/ExampleSomeipInterfaceStub.hpp
    v1/commonapi/ExampleSomeipInterfaceStubDefault.hpp
  )
  capicxx_generate_someip(
    interfaces/someip/fidl/ExampleSomeipInterface.fidl
    interfaces/someip/fidl/ExampleSomeipInterface.fdepl
    "${CAPICXX_EXAMPLE_GENERATED_FILES}"
  )
  add_library(fwe-someip-example OBJECT
    ${CAPICXX_EXAMPLE_GENERATED_FILES}
  )
  target_include_directories(fwe-someip-example PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
  )
  target_include_directories(fwe-someip-example SYSTEM PUBLIC
    ${VSOMEIP_INCLUDE_DIR}
    ${COMMONAPI_INCLUDE_DIRS}
    ${COMMONAPI_SOMEIP_INCLUDE_DIRS}
  )
  set_target_properties(fwe-someip-example
    PROPERTIES POSITION_INDEPENDENT_CODE ON
  )

  set(CAPICXX_DEVICE_SHADOW_GENERATED_FILES
    v1/commonapi/DeviceShadowOverSomeipInterface.hpp
    v1/commonapi/DeviceShadowOverSomeipInterfaceProxy.hpp
    v1/commonapi/DeviceShadowOverSomeipInterfaceProxyBase.hpp
    v1/commonapi/DeviceShadowOverSomeipInterfaceSomeIPDeployment.cpp
    v1/commonapi/DeviceShadowOverSomeipInterfaceSomeIPDeployment.hpp
    v1/commonapi/DeviceShadowOverSomeipInterfaceSomeIPProxy.cpp
    v1/commonapi/DeviceShadowOverSomeipInterfaceSomeIPProxy.hpp
    v1/commonapi/DeviceShadowOverSomeipInterfaceSomeIPStubAdapter.cpp
    v1/commonapi/DeviceShadowOverSomeipInterfaceSomeIPStubAdapter.hpp
    v1/commonapi/DeviceShadowOverSomeipInterfaceStub.hpp
    v1/commonapi/DeviceShadowOverSomeipInterfaceStubDefault.hpp
  )
  capicxx_generate_someip(
    interfaces/someip/fidl/DeviceShadowOverSomeipInterface.fidl
    interfaces/someip/fidl/DeviceShadowOverSomeipInterface.fdepl
    "${CAPICXX_DEVICE_SHADOW_GENERATED_FILES}"
  )
  add_library(fwe-device-shadow-over-someip OBJECT
    ${CAPICXX_DEVICE_SHADOW_GENERATED_FILES}
  )
  target_include_directories(fwe-device-shadow-over-someip PUBLIC
    ${CMAKE_CURRENT_BINARY_DIR}
  )
  target_include_directories(fwe-device-shadow-over-someip SYSTEM PUBLIC
    ${VSOMEIP_INCLUDE_DIR}
    ${COMMONAPI_INCLUDE_DIRS}
    ${COMMONAPI_SOMEIP_INCLUDE_DIRS}
  )
  set_target_properties(fwe-device-shadow-over-someip
    PROPERTIES POSITION_INDEPENDENT_CODE ON
  )
endif()

if(FWE_FEATURE_MICROPYTHON)
  include(cmake/micropython.cmake)
endif()

# Source files
set(SRC_FILES
  src/AwsBootstrap.cpp
  src/AwsIotReceiver.cpp
  src/AwsIotSender.cpp
  src/AwsIotConnectivityModule.cpp
  src/AwsSDKMemoryManager.cpp
  src/CacheAndPersist.cpp
  src/CANDataConsumer.cpp
  src/CANDataSource.cpp
  src/CANDecoder.cpp
  src/CheckinSender.cpp
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
  src/InspectionMatrixExtractor.cpp
  src/IoTFleetWiseConfig.cpp
  src/IoTFleetWiseEngine.cpp
  src/ISOTPOverCANReceiver.cpp
  src/ISOTPOverCANSender.cpp
  src/ISOTPOverCANSenderReceiver.cpp
  src/LoggingModule.cpp
  src/MemoryUsageInfo.cpp
  src/NamedSignalDataSource.cpp
  src/OBDDataDecoder.cpp
  src/OBDOverCANECU.cpp
  src/OBDOverCANModule.cpp
  src/PayloadManager.cpp
  src/Persistency.cpp
  src/RawDataManager.cpp
  src/RemoteProfiler.cpp
  src/RetryThread.cpp
  src/Schema.cpp
  src/TelemetryDataSender.cpp
  src/DataFetchManager.cpp
  src/Thread.cpp
  src/TraceModule.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/IoTFleetWiseVersion.cpp
  $<TARGET_OBJECTS:fwe-proto>
)

# Test files
set(TEST_FILES
  test/unit/AwsIotConnectivityModuleTest.cpp
  test/unit/CacheAndPersistTest.cpp
  test/unit/CANDataSourceTest.cpp
  test/unit/CANDecoderTest.cpp
  test/unit/ClockHandlerTest.cpp
  test/unit/CollectionInspectionEngineTest.cpp
  test/unit/CollectionInspectionWorkerThreadTest.cpp
  test/unit/CollectionSchemeManagerGtest.cpp
  test/unit/CollectionSchemeManagerTest.cpp
  test/unit/CPUUsageInfoTest.cpp
  test/unit/DataSenderManagerTest.cpp
  test/unit/DataSenderManagerWorkerThreadTest.cpp
  test/unit/DataFetchManagerTest.cpp
  test/unit/DataSenderProtoWriterTest.cpp
  test/unit/DecoderDictionaryExtractorTest.cpp
  test/unit/ExternalCANDataSourceTest.cpp
  test/unit/InspectionMatrixExtractorTest.cpp
  test/unit/IoTFleetWiseConfigTest.cpp
  test/unit/IoTFleetWiseEngineTest.cpp
  test/unit/ISOTPOverCANProtocolTest.cpp
  test/unit/LoggingModuleTest.cpp
  test/unit/MemoryUsageInfoTest.cpp
  test/unit/NamedSignalDataSourceTest.cpp
  test/unit/OBDDataDecoderTest.cpp
  test/unit/OBDOverCANModuleTest.cpp
  test/unit/PayloadManagerTest.cpp
  test/unit/PersistencyTest.cpp
  test/unit/RawDataManagerTest.cpp
  test/unit/RemoteProfilerTest.cpp
  test/unit/SchemaTest.cpp
  test/unit/ThreadTest.cpp
  test/unit/TimerTest.cpp
  test/unit/TraceModuleTest.cpp
  test/unit/WaitUntilTest.cpp
)

set(BENCHMARK_TEST_FILES
  test/unit/ClockHandlerBenchmarkTest.cpp
)

# Optional files
if(FWE_FEATURE_IWAVE_GPS)
  set(SRC_FILES ${SRC_FILES} src/IWaveGpsSource.cpp)
  set(TEST_FILES ${TEST_FILES} test/unit/IWaveGpsSourceTest.cpp)
endif()
if(FWE_FEATURE_EXTERNAL_GPS)
  set(SRC_FILES ${SRC_FILES} src/ExternalGpsSource.cpp)
  set(TEST_FILES ${TEST_FILES} test/unit/ExternalGpsSourceTest.cpp)
endif()
if(FWE_FEATURE_AAOS_VHAL)
  set(SRC_FILES ${SRC_FILES} src/AaosVhalSource.cpp)
  set(TEST_FILES ${TEST_FILES} test/unit/AaosVhalSourceTest.cpp)
endif()
if(FWE_FEATURE_GREENGRASSV2)
  set(SRC_FILES ${SRC_FILES}
    src/AwsGreengrassV2ConnectivityModule.cpp
    src/AwsGreengrassV2Receiver.cpp
    src/AwsGreengrassV2Sender.cpp
  )
  set(TEST_FILES ${TEST_FILES} test/unit/AwsGreengrassV2ConnectivityModuleTest.cpp)
endif()
if(FWE_FEATURE_S3)
  set(SRC_FILES ${SRC_FILES} src/Credentials.cpp)
  set(TEST_FILES ${TEST_FILES} test/unit/CredentialsTest.cpp)
endif()
if(FWE_FEATURE_VISION_SYSTEM_DATA)
  set(SRC_FILES ${SRC_FILES}
    src/DataSenderIonWriter.cpp
    src/S3Sender.cpp
    src/VisionSystemDataSender.cpp
  )
  set(TEST_FILES ${TEST_FILES}
    test/unit/DataSenderIonWriterTest.cpp
    test/unit/S3SenderTest.cpp
  )
endif()
if(FWE_FEATURE_ROS2)
  set(SRC_FILES ${SRC_FILES} src/ROS2DataSource.cpp)
endif()
if(FWE_FEATURE_SOMEIP)
  set(SRC_FILES ${SRC_FILES}
    src/DeviceShadowOverSomeip.cpp
    src/SomeipCommandDispatcher.cpp
    src/SomeipDataSource.cpp
    src/SomeipToCanBridge.cpp
    $<TARGET_OBJECTS:fwe-someip-example>
    $<TARGET_OBJECTS:fwe-device-shadow-over-someip>
  )
  set(TEST_FILES ${TEST_FILES}
    test/unit/DeviceShadowOverSomeipTest.cpp
    test/unit/SomeipCommandDispatcherTest.cpp
    test/unit/SomeipDataSourceTest.cpp
    test/unit/SomeipToCanBridgeTest.cpp
  )
endif()
if(FWE_FEATURE_REMOTE_COMMANDS)
  set(SRC_FILES ${SRC_FILES}
    src/ActuatorCommandManager.cpp
    src/CanCommandDispatcher.cpp
    src/CommandResponseDataSender.cpp
    src/CommandSchema.cpp
  )
  set(TEST_FILES ${TEST_FILES}
    test/unit/ActuatorCommandManagerTest.cpp
    test/unit/CanCommandDispatcherTest.cpp
    test/unit/CommandSchemaTest.cpp
  )
endif()
if(FWE_FEATURE_LAST_KNOWN_STATE)
  set(SRC_FILES ${SRC_FILES}
    src/LastKnownStateDataSender.cpp
    src/LastKnownStateIngestion.cpp
    src/LastKnownStateInspector.cpp
    src/LastKnownStateSchema.cpp
    src/LastKnownStateWorkerThread.cpp
  )
  set(TEST_FILES ${TEST_FILES}
    test/unit/LastKnownStateSchemaTest.cpp
    test/unit/LastKnownStateWorkerThreadTest.cpp
    test/unit/LastKnownStateInspectorTest.cpp
  )
endif()
if(FWE_TEST_FAKETIME)
  set(FAKETIME_TEST_FILES test/unit/FakeSystemTimeTest.cpp)
endif()
if(FWE_FEATURE_STORE_AND_FORWARD)
  set(SRC_FILES ${SRC_FILES}
    src/snf/RateLimiter.cpp
    src/snf/StoreFileSystem.cpp
    src/snf/StoreLogger.cpp
    src/snf/StreamManager.cpp
    src/snf/StreamForwarder.cpp
    src/snf/DataSenderProtoReader.cpp
    src/snf/IoTJobsDataRequestHandler.cpp)
  set(TEST_FILES ${TEST_FILES}
    test/unit/snf/RateLimiterTest.cpp
    test/unit/snf/StoreFileSystemTest.cpp
    test/unit/snf/StoreLoggerTest.cpp
    test/unit/snf/StreamManagerTest.cpp
    test/unit/snf/StreamForwarderTest.cpp
    test/unit/snf/DataSenderProtoReaderTest.cpp
    test/unit/snf/IoTJobsDataRequestHandlerTest.cpp)
endif()
if (FWE_FEATURE_UDS_DTC_EXAMPLE)
  set(SRC_FILES ${SRC_FILES}
    src/ExampleUDSInterface.cpp)
  set(TEST_FILES ${TEST_FILES}
    test/unit/ExampleUDSInterfaceTest.cpp)
endif()
if(FWE_FEATURE_UDS_DTC)
  set(SRC_FILES ${SRC_FILES}
    src/RemoteDiagnosticDataSource.cpp)
endif()
if(FWE_FEATURE_CUSTOM_FUNCTION_EXAMPLES)
  set(SRC_FILES ${SRC_FILES}
    src/CustomFunctionMath.cpp
    src/CustomFunctionMultiRisingEdgeTrigger.cpp)
  set(TEST_FILES ${TEST_FILES}
    test/unit/CustomFunctionMathTest.cpp
    test/unit/CustomFunctionMultiRisingEdgeTriggerTest.cpp)
endif()
if(FWE_FEATURE_SCRIPT_ENGINE)
  set(SRC_FILES ${SRC_FILES} src/CustomFunctionScriptEngine.cpp)
endif()
if(FWE_FEATURE_CPYTHON)
  set(SRC_FILES ${SRC_FILES} src/CustomFunctionCPython.cpp)
endif()
if(FWE_FEATURE_MICROPYTHON)
  set(SRC_FILES ${SRC_FILES} src/CustomFunctionMicroPython.cpp)
endif()
if(FWE_FEATURE_CPYTHON OR FWE_FEATURE_MICROPYTHON)
  set(TEST_FILES ${TEST_FILES} test/unit/CustomFunctionPythonTest.cpp)
endif()

# Dependencies
set(REQUIRED_BOOST_COMPONENTS "thread;filesystem;chrono")
add_compile_options("-DBOOST_UUID_NO_SIMD") # For clang compatibility
if(FWE_FEATURE_SOMEIP)
  set(REQUIRED_BOOST_COMPONENTS "${REQUIRED_BOOST_COMPONENTS};system;program_options")
endif()
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
if(FWE_FEATURE_S3)
  set(REQUIRED_AWS_SDK_COMPONENTS "transfer;s3-crt;${REQUIRED_AWS_SDK_COMPONENTS}")
endif()
set(OLD_BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS ${FWE_AWS_SDK_SHARED_LIBS})
# Note: this should only be called once
find_package(AWSSDK REQUIRED COMPONENTS ${REQUIRED_AWS_SDK_COMPONENTS})
set(BUILD_SHARED_LIBS ${OLD_BUILD_SHARED_LIBS})

if(FWE_FEATURE_GREENGRASSV2)
  find_package(GreengrassIpc-cpp REQUIRED)
endif()

if(FWE_FEATURE_SOMEIP OR FWE_FEATURE_CPYTHON)
  # Dynamically link to Python library:
  set(OLD_CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".so")
  find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ${OLD_CMAKE_FIND_LIBRARY_SUFFIXES})
endif()

# Object lib used in output binary and unit tests
add_library(fwe OBJECT
  ${SRC_FILES}
)

if(FWE_FEATURE_IWAVE_GPS)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_IWAVE_GPS)
endif()
if(FWE_FEATURE_EXTERNAL_GPS)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_EXTERNAL_GPS)
endif()
if(FWE_FEATURE_AAOS_VHAL)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_AAOS_VHAL)
endif()
if(FWE_FEATURE_GREENGRASSV2)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_GREENGRASSV2)
endif()
if(FWE_FEATURE_ROS2)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_ROS2)
endif()
if(FWE_FEATURE_VISION_SYSTEM_DATA)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_VISION_SYSTEM_DATA DECNUMDIGITS=34)
endif()
if(FWE_FEATURE_SOMEIP)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_SOMEIP)
endif()
if(FWE_FEATURE_LAST_KNOWN_STATE)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_LAST_KNOWN_STATE)
endif()
if(FWE_FEATURE_REMOTE_COMMANDS)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_REMOTE_COMMANDS)
endif()
if(FWE_FEATURE_STORE_AND_FORWARD)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_STORE_AND_FORWARD)
endif()
if(FWE_FEATURE_UDS_DTC_EXAMPLE)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_UDS_DTC_EXAMPLE)
endif()
if(FWE_FEATURE_UDS_DTC)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_UDS_DTC)
endif()
if(FWE_FEATURE_CUSTOM_FUNCTION_EXAMPLES)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_CUSTOM_FUNCTION_EXAMPLES)
endif()
if(FWE_FEATURE_MICROPYTHON)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_MICROPYTHON)
endif()
if(FWE_FEATURE_CPYTHON)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_CPYTHON)
endif()
if(FWE_FEATURE_SCRIPT_ENGINE)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_SCRIPT_ENGINE)
endif()
if(FWE_FEATURE_S3)
  target_compile_definitions(fwe PUBLIC FWE_FEATURE_S3)
endif()

# Include dirs
target_include_directories(fwe PUBLIC
  ${JSONCPP_INCLUDE_DIR}
  ${SNAPPY_INCLUDE_DIR}
  ${Protobuf_INCLUDE_DIRS}
  $<$<BOOL:${FWE_FEATURE_SOMEIP}>:${VSOMEIP_INCLUDE_DIR} ${COMMONAPI_INCLUDE_DIRS} ${COMMONAPI_SOMEIP_INCLUDE_DIRS}>
  $<$<BOOL:${FWE_FEATURE_STORE_AND_FORWARD}>:${STORE_LIBRARY_CPP_INCLUDE_DIR}>
)
target_include_directories(fwe PRIVATE
  include
  ${CMAKE_CURRENT_BINARY_DIR}
)

# Link libraries
target_link_libraries(fwe
  $<$<BOOL:${FWE_FEATURE_GREENGRASSV2}>:AWS::GreengrassIpc-cpp>
  $<$<BOOL:${FWE_FEATURE_STORE_AND_FORWARD}>:${STORE_LIBRARY_CPP_STREAM}>
  $<$<BOOL:${FWE_FEATURE_STORE_AND_FORWARD}>:${STORE_LIBRARY_CPP_KV}>
  ${AWSSDK_LINK_LIBRARIES}
  ${FWE_AWS_SDK_EXTRA_LIBS}
  ${SNAPPY_LIBRARY}
  ${JSONCPP_LIBRARY}
  ${Protobuf_LIBRARIES}
  Boost::chrono
  Boost::thread
  Boost::filesystem
  $<$<BOOL:${FWE_FEATURE_ROS2}>:fastcdr>
  $<$<BOOL:${FWE_FEATURE_SOMEIP}>:CommonAPI-SomeIP>
  $<$<BOOL:${FWE_FEATURE_SOMEIP}>:CommonAPI>
  $<$<BOOL:${FWE_FEATURE_SOMEIP}>:${VSOMEIP_LIBRARIES}>
  $<$<BOOL:${FWE_FEATURE_SOMEIP}>:Boost::system>
)

if(FWE_FEATURE_VISION_SYSTEM_DATA)
  find_package(IonC)
  target_link_libraries(fwe
    $<$<BOOL:${FWE_STATIC_LINK}>:IonC::ionc_static>
    $<$<BOOL:${FWE_STATIC_LINK}>:IonC::decNumber_static>
    $<$<NOT:$<BOOL:${FWE_STATIC_LINK}>>:IonC::ionc>
    $<$<NOT:$<BOOL:${FWE_STATIC_LINK}>>:IonC::decNumber>
  )
endif()

if(FWE_FEATURE_MICROPYTHON)
  target_include_directories(fwe SYSTEM PUBLIC "${MICROPYTHON_INCLUDE_DIRS}")
  target_link_libraries(fwe micropython_embed)
endif()
if(FWE_FEATURE_CPYTHON)
  target_include_directories(fwe PUBLIC ${Python3_INCLUDE_DIRS})
  target_link_libraries(fwe ${Python3_LIBRARIES})
endif()

if(FWE_FEATURE_ROS2)
  ament_target_dependencies(fwe rclcpp)
endif()

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
  install(DIRECTORY
    include/aws
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(FILES
    ${PROTO_HDRS}
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/aws/iotfleetwise)
  install(TARGETS aws-iot-fleetwise-edge fwe fwe-proto
    EXPORT AwsIotFweTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
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
  $<$<BOOL:${FWE_FEATURE_SOMEIP}>:fwe-someip-example>
  $<$<BOOL:${FWE_FEATURE_SOMEIP}>:fwe-device-shadow-over-someip>
)
target_include_directories(aws-iot-fleetwise-edge PRIVATE
  include
  ${CMAKE_CURRENT_BINARY_DIR}
)
set_target_properties(aws-iot-fleetwise-edge fwe fwe-proto
  PROPERTIES POSITION_INDEPENDENT_CODE ON
)

# Unit tests
if(${BUILD_TESTING})

  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/unit/support/static-config-ok.json           DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/unit/support/static-config-corrupt.json      DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/unit/support/static-config-inline-creds.json DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/test/unit/support/test_module.py                  DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  find_package(GTest REQUIRED)
  find_library(GMOCK_LIB NAMES gmock)
  find_package(benchmark REQUIRED)

  # Google Test executable
  add_executable(fwe-gtest
    ${TEST_FILES}
    test/unit/support/main.cpp
  )

  target_include_directories(fwe-gtest PUBLIC
    include
    test/unit/support
    ${CMAKE_CURRENT_BINARY_DIR}
  )
  target_link_libraries(fwe-gtest
    fwe
    fwe-proto
    $<$<BOOL:${FWE_FEATURE_SOMEIP}>:fwe-someip-example>
    $<$<BOOL:${FWE_FEATURE_SOMEIP}>:fwe-device-shadow-over-someip>
    ${GMOCK_LIB}
    GTest::GTest
  )

  set(TEST_BINARIES_PARALLEL ${TEST_BINARIES_PARALLEL} fwe-gtest)

  # Google Benchmark executable
  add_executable(fwe-benchmark
    ${BENCHMARK_TEST_FILES}
  )
  target_include_directories(fwe-benchmark PUBLIC
    include
    test/unit/support
    ${CMAKE_CURRENT_BINARY_DIR}
  )
  target_link_libraries(fwe-benchmark
    fwe
    fwe-proto
    benchmark::benchmark
  )
  add_test(
    NAME fwe-benchmark
    COMMAND fwe-benchmark --benchmark_out=benchmark-report.txt --benchmark_out_format=console
  )

  if(FWE_TEST_FAKETIME)
    # Faketime test executable
    add_executable(fwe-faketime
      ${FAKETIME_TEST_FILES}
      test/unit/support/main.cpp
    )
    target_include_directories(fwe-faketime PUBLIC
      include
      test/unit/support
      ${CMAKE_CURRENT_BINARY_DIR}
    )
    target_link_libraries(fwe-faketime
      fwe
      fwe-proto
      ${GMOCK_LIB}
      GTest::GTest
    )
    add_unit_test_with_faketime(fwe-faketime)
  endif()

  # TODO: Special case for ROS2DataSourceTest as it uses compile-time mocking
  if(FWE_FEATURE_ROS2)
    add_executable(ROS2DataSourceTest
      test/unit/ROS2DataSourceTest.cpp
      src/ROS2DataSource.cpp
      src/RawDataManager.cpp
      src/Thread.cpp
      src/LoggingModule.cpp
      src/ClockHandler.cpp
      src/ConsoleLogger.cpp
      src/IoTFleetWiseConfig.cpp
      test/unit/support/main.cpp
    )
    target_include_directories(ROS2DataSourceTest PUBLIC
      include
      test/unit/support
      ${CMAKE_CURRENT_BINARY_DIR}
      test/unit/support/ros2-mock-include
    )
    target_link_libraries(ROS2DataSourceTest
      ${JSONCPP_LIBRARY}
      fastcdr
      Boost::chrono
      Boost::thread
      ${GMOCK_LIB}
      GTest::GTest
    )
    target_compile_definitions(ROS2DataSourceTest PUBLIC
      $<TARGET_PROPERTY:fwe,INTERFACE_COMPILE_DEFINITIONS>)
    set(TEST_BINARIES_PARALLEL ${TEST_BINARIES_PARALLEL} ROS2DataSourceTest)
  endif()

  add_parallel_unit_tests(${TEST_BINARIES_PARALLEL})
  add_parallel_valgrind_tests(${TEST_BINARIES_PARALLEL})
endif()

# Tools
if(FWE_BUILD_TOOLS)
  if(FWE_FEATURE_SOMEIP)
    include(tools/can-to-someip/can-to-someip.cmake)
    include(tools/someipigen/someipigen.cmake)
    include(tools/someip_device_shadow_editor/someip_device_shadow_editor.cmake)
  endif()
endif()
