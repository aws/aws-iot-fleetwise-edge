// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#include "ROS2DataSource.h"
#include "CollectionInspectionAPITypes.h"
#include "IDecoderDictionary.h"
#include "MessageTypes.h"
#include "RawDataManager.h"
#include "SignalTypes.h"
#include "TimeTypes.h"
#include "VehicleDataSourceTypes.h"
#include "WaitUntil.h"
#include <array>
#include <atomic>
#include <boost/optional/optional.hpp>
#include <boost/variant.hpp>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fastcdr/Cdr.h>
#include <fastcdr/FastBuffer.h>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <map>
#include <memory>
#include <ratio>
#include <rclcpp/rclcpp.hpp>
#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rosidl_typesupport_introspection_cpp
{

const char *typesupport_identifier = "dummyMock";

} // namespace rosidl_typesupport_introspection_cpp

namespace rclcpp
{

rosidl_message_type_support_t message;
MultiThreadedExecutorMock *multiThreadedExecutorMock;
NodeMock *nodeMock;
TypeSupportMock *typeSupportMock;

} // namespace rclcpp

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::AtLeast;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;
using ::testing::Throw;

class RawBufferManagerMock : public RawData::BufferManager
{
public:
    RawBufferManagerMock()
        : RawData::BufferManager( RawData::BufferManagerConfig::create().get() )
    {
    }

    // NOLINTNEXTLINE
    MOCK_METHOD( RawData::BufferHandle,
                 push,
                 ( uint8_t * data, size_t size, Timestamp receiveTimestamp, RawData::BufferTypeId typeId ),
                 ( override ) );
};

class ROS2DataSourceTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        rclcpp::multiThreadedExecutorMock = &multiThreadedExecutorMock;
        rclcpp::nodeMock = &nodeMock;
        rclcpp::typeSupportMock = &typeSupportMock;
    }

    void
    TearDown() override
    {
    }

    void
    startCountingSubscribeCalls( std::string messageId, std::string typeId )
    {
        auto subscription = std::make_shared<rclcpp::GenericSubscription>();
        EXPECT_CALL( nodeMock, create_generic_subscription( messageId, typeId, _, _, _ ) )
            .Times( AtLeast( 1 ) )
            .WillRepeatedly(
                ( [subscription,
                   this]( const std::string &topic_name,
                          const std::string &topic_type,
                          const rclcpp::QoS &qos,
                          std::function<void( std::shared_ptr<rclcpp::SerializedMessage> )> callback,
                          const rclcpp::SubscriptionOptions &options ) -> rclcpp::GenericSubscription::SharedPtr {
                    static_cast<void>( topic_name ); // UNUSED
                    static_cast<void>( topic_type ); // UNUSED
                    static_cast<void>( qos );        // UNUSED
                    static_cast<void>( options );    // UNUSED
                    this->lastSubscribedCallback = callback;
                    this->subscribeCallsCounter++;
                    return subscription;
                } ) );
    }

    void
    addStruct( eprosima::fastcdr::Cdr &cdr )
    {
        uint8_t u8 = 22;
        cdr << u8;
        int8_t i8 = 33;
        cdr << i8;
        uint16_t u16 = 4444;
        cdr << u16;
        int16_t i16 = 5555;
        cdr << i16;
        uint32_t u32 = 777777U;
        cdr << u32;
        int32_t i32 = 8888888;
        cdr << i32;
        uint64_t u64 = 9999999999UL;
        cdr << u64;
        int64_t i64 = 1010101010L;
        cdr << i64;
        float f32 = 0.00002222f;
        cdr << f32;
        double d64 = 0.000000003333;
        cdr << d64;
        bool b1 = true;
        cdr << b1;
    }
    void
    fillDefaultSerializedMessage()
    {
        defaultSerializedMessageBuffer.reserve( 4096 ); // 4KB should be enough for default message
        eprosima::fastcdr::Cdr cdr(
            defaultSerializedMessageBuffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::Cdr::DDS_CDR );
        cdr.serialize_encapsulation();
        int32_t firstPrimivtive = 0x13579246; // primitive INT32
        cdr << firstPrimivtive;
        std::array<int32_t, 10> arrayOfPrimitive = {
            0, 1, 2, 3, 4, 5, 6, 7, 8, 9 }; // array of primitive with constant size 10
        cdr << arrayOfPrimitive;
        std::vector<int32_t> vectorOfPrimitive = { 10, 11, 12, 13, 14, 15, 16, 17 };
        cdr.serialize( vectorOfPrimitive );

        addStruct( cdr );

        uint32_t sizeOfDynamicSizeArray = 3;
        cdr << sizeOfDynamicSizeArray;
        for ( uint32_t i = 0; i < sizeOfDynamicSizeArray; i++ )
        {
            addStruct( cdr );
        }
        cdr << std::string( "Dummy mocked string" );
        for ( int i = 0; i < 3; i++ )
        {
            cdr << std::string( "Dummy mocked array of strings" );
        }

        defaultSerializedMessage = std::make_shared<rclcpp::SerializedMessage>();
        defaultSerializedMessage->message.buffer = defaultSerializedMessageBuffer.getBuffer();
        defaultSerializedMessage->message.buffer_length = cdr.getSerializedDataLength();
    }

    void
    fillDefaultMessageType()
    {
        auto &messageFormat = defaultMessageFormat;

        messageFormat.mCollectRaw = true;
        messageFormat.mSignalId = 123;
        messageFormat.mRootTypeId = 100;
        ComplexStruct topLevelStruct;
        topLevelStruct.mOrderedTypeIds.push_back( 110 ); // primitive INT32
        topLevelStruct.mOrderedTypeIds.push_back( 120 ); // array of primitive with constant size 10
        topLevelStruct.mOrderedTypeIds.push_back( 130 ); // array of primitive with dynamic size
        topLevelStruct.mOrderedTypeIds.push_back( 140 ); // struct
        topLevelStruct.mOrderedTypeIds.push_back( 150 ); // array of struct dynamic size
        topLevelStruct.mOrderedTypeIds.push_back( 160 ); // string = array of uint8 with dynamic size
        topLevelStruct.mOrderedTypeIds.push_back( 170 ); // array (constant size 3) of strings = array of array of uint8
        messageFormat.mComplexTypeMap[100] = topLevelStruct;
        messageFormat.mSignalPaths.resize( 4 );
        messageFormat.mSignalPaths[0].mSignalPath = { 0 };
        messageFormat.mSignalPaths[0].mPartialSignalID = 0x80001111;
        messageFormat.mSignalPaths[1].mSignalPath = { 1, 5 };
        messageFormat.mSignalPaths[1].mPartialSignalID = 0x80001112;
        messageFormat.mSignalPaths[2].mSignalPath = { 2, 0 };
        messageFormat.mSignalPaths[2].mPartialSignalID = 0x80001113;
        messageFormat.mSignalPaths[3].mSignalPath = {
            6, 2, 1 }; // Second character of last string of array in last element of struct
        messageFormat.mSignalPaths[3].mPartialSignalID = 0x80001114;

        PrimitiveData primitive;
        primitive.mOffset = 0;
        primitive.mScaling = 1;
        primitive.mPrimitiveType = SignalType::INT32;
        messageFormat.mComplexTypeMap[110] = primitive;

        ComplexArray arrayOfPrimitive;
        arrayOfPrimitive.mRepeatedTypeId = 110;
        arrayOfPrimitive.mSize = 10;
        messageFormat.mComplexTypeMap[120] = arrayOfPrimitive;

        ComplexArray arrayOfPrimitiveDynamicSize;
        arrayOfPrimitiveDynamicSize.mRepeatedTypeId = 110;
        arrayOfPrimitiveDynamicSize.mSize = 0;
        messageFormat.mComplexTypeMap[130] = arrayOfPrimitiveDynamicSize;

        ComplexStruct nestedStruct;
        nestedStruct.mOrderedTypeIds.push_back( 141 );
        nestedStruct.mOrderedTypeIds.push_back( 142 );
        nestedStruct.mOrderedTypeIds.push_back( 143 );
        nestedStruct.mOrderedTypeIds.push_back( 144 );
        nestedStruct.mOrderedTypeIds.push_back( 145 );
        nestedStruct.mOrderedTypeIds.push_back( 146 );
        nestedStruct.mOrderedTypeIds.push_back( 147 );
        nestedStruct.mOrderedTypeIds.push_back( 148 );
        nestedStruct.mOrderedTypeIds.push_back( 149 );
        nestedStruct.mOrderedTypeIds.push_back( 1491 );
        nestedStruct.mOrderedTypeIds.push_back( 1492 );
        messageFormat.mComplexTypeMap[140] = nestedStruct;
        static PrimitiveData memberPrimitive;
        memberPrimitive.mOffset = 0;
        memberPrimitive.mScaling = 1;
        memberPrimitive.mPrimitiveType = SignalType::UINT8;
        messageFormat.mComplexTypeMap[141] = memberPrimitive;
        memberPrimitive.mPrimitiveType = SignalType::INT8;
        messageFormat.mComplexTypeMap[142] = memberPrimitive;
        memberPrimitive.mPrimitiveType = SignalType::UINT16;
        messageFormat.mComplexTypeMap[143] = memberPrimitive;
        memberPrimitive.mPrimitiveType = SignalType::INT16;
        messageFormat.mComplexTypeMap[144] = memberPrimitive;
        memberPrimitive.mPrimitiveType = SignalType::UINT32;
        messageFormat.mComplexTypeMap[145] = memberPrimitive;
        memberPrimitive.mPrimitiveType = SignalType::INT32;
        messageFormat.mComplexTypeMap[146] = memberPrimitive;
        memberPrimitive.mPrimitiveType = SignalType::UINT64;
        messageFormat.mComplexTypeMap[147] = memberPrimitive;
        memberPrimitive.mPrimitiveType = SignalType::INT64;
        messageFormat.mComplexTypeMap[148] = memberPrimitive;
        memberPrimitive.mPrimitiveType = SignalType::FLOAT;
        messageFormat.mComplexTypeMap[149] = memberPrimitive;
        memberPrimitive.mPrimitiveType = SignalType::DOUBLE;
        messageFormat.mComplexTypeMap[1491] = memberPrimitive;
        memberPrimitive.mPrimitiveType = SignalType::BOOLEAN;
        messageFormat.mComplexTypeMap[1492] = memberPrimitive;

        ComplexArray arrayOfStructsDynamicSize;
        arrayOfStructsDynamicSize.mRepeatedTypeId = 140;
        arrayOfStructsDynamicSize.mSize = 0;
        messageFormat.mComplexTypeMap[150] = arrayOfStructsDynamicSize;

        ComplexArray stringAsArray;
        stringAsArray.mRepeatedTypeId = 141; // uint8
        stringAsArray.mSize = 0;
        messageFormat.mComplexTypeMap[160] = stringAsArray;

        ComplexArray arrayOfStrings;
        arrayOfStrings.mRepeatedTypeId = 160; // string
        arrayOfStrings.mSize = 3;
        messageFormat.mComplexTypeMap[170] = arrayOfStrings;

        static rosidl_typesupport_introspection_cpp::MessageMembers topLevelMembers; // needs to survive function call
        defaultRos2TopLevelMessage.data = &topLevelMembers;

        topLevelMembers.member_count_ = 7;
        topLevelMembers.members_.resize( 7 );
        topLevelMembers.members_[0].type_id_ = rosidl_typesupport_introspection_cpp::ROS_TYPE_INT32;
        topLevelMembers.members_[0].is_array_ = false;
        topLevelMembers.members_[1].type_id_ = rosidl_typesupport_introspection_cpp::ROS_TYPE_INT32;
        topLevelMembers.members_[1].is_array_ = true;
        topLevelMembers.members_[1].array_size_ = 10;
        topLevelMembers.members_[2].type_id_ = rosidl_typesupport_introspection_cpp::ROS_TYPE_INT32;
        topLevelMembers.members_[2].is_array_ = true;
        topLevelMembers.members_[2].array_size_ = 0;

        static rosidl_typesupport_introspection_cpp::MessageMembers structMembers;

        topLevelMembers.members_[3].type_id_ = rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE;
        topLevelMembers.members_[3].is_array_ = false;
        topLevelMembers.members_[3].array_size_ = 0;
        topLevelMembers.members_[3].members_ = &structMembers;
        structMembers.data = &structMembers;
        structMembers.member_count_ = 11;
        structMembers.members_.emplace_back( rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT8 );
        structMembers.members_.emplace_back( rosidl_typesupport_introspection_cpp::ROS_TYPE_INT8 );
        structMembers.members_.emplace_back( rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT16 );
        structMembers.members_.emplace_back( rosidl_typesupport_introspection_cpp::ROS_TYPE_INT16 );
        structMembers.members_.emplace_back( rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT32 );
        structMembers.members_.emplace_back( rosidl_typesupport_introspection_cpp::ROS_TYPE_INT32 );
        structMembers.members_.emplace_back( rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT64 );
        structMembers.members_.emplace_back( rosidl_typesupport_introspection_cpp::ROS_TYPE_INT64 );
        structMembers.members_.emplace_back( rosidl_typesupport_introspection_cpp::ROS_TYPE_FLOAT );
        structMembers.members_.emplace_back( rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE );
        structMembers.members_.emplace_back( rosidl_typesupport_introspection_cpp::ROS_TYPE_BOOLEAN );

        topLevelMembers.members_[4].type_id_ = rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE;
        topLevelMembers.members_[4].is_array_ = true;
        topLevelMembers.members_[4].array_size_ = 0;
        topLevelMembers.members_[4].members_ = &structMembers;

        topLevelMembers.members_[5].type_id_ = rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING;
        topLevelMembers.members_[5].is_array_ = false;
        topLevelMembers.members_[5].array_size_ = 0;

        topLevelMembers.members_[6].type_id_ = rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING;
        topLevelMembers.members_[6].is_array_ = true;
        topLevelMembers.members_[6].array_size_ = 3;
    }

    void
    typeSupportReturnDefaultMessage( std::string typeId )
    {
        EXPECT_CALL( typeSupportMock, get_typesupport_library( std::string( typeId ), _ ) )
            .Times( AtLeast( 1 ) )
            .WillRepeatedly( Return( std::make_shared<rcpputils::SharedLibrary>() ) );
        EXPECT_CALL( typeSupportMock, get_typesupport_handle( std::string( typeId ), _, _ ) )
            .Times( AtLeast( 1 ) )
            .WillRepeatedly( Return( &defaultRos2TopLevelMessage ) );
    }

    ComplexDataMessageFormat defaultMessageFormat;
    rosidl_message_type_support_t defaultRos2TopLevelMessage;
    std::shared_ptr<rclcpp::SerializedMessage> defaultSerializedMessage;
    eprosima::fastcdr::FastBuffer defaultSerializedMessageBuffer;

    std::function<void( std::shared_ptr<rclcpp::SerializedMessage> )> lastSubscribedCallback;

    NiceMock<rclcpp::MultiThreadedExecutorMock> multiThreadedExecutorMock;
    NiceMock<rclcpp::NodeMock> nodeMock;
    NiceMock<rclcpp::TypeSupportMock> typeSupportMock;
    std::shared_ptr<NiceMock<RawBufferManagerMock>> rawBufferManagerMock =
        std::make_shared<NiceMock<RawBufferManagerMock>>();
    const int MINIMUM_WAIT_TIME_ONE_CYCLE_MS = 300;
    SignalBufferPtr signalBufferPtr = std::make_shared<SignalBuffer>( 100 );

public:
    std::atomic<int> subscribeCallsCounter{ 0 };
};

TEST_F( ROS2DataSourceTest, ConfigTest )
{
    ROS2DataSourceConfig config;
    Json::Value testinput;
    ASSERT_FALSE( ROS2DataSourceConfig::parseFromJson( testinput, config ) );
    testinput["ros2Interface"]["executorThreads"] = 0;
    ASSERT_FALSE( ROS2DataSourceConfig::parseFromJson( testinput, config ) );
    testinput["ros2Interface"]["executorThreads"] = 5;
    testinput["ros2Interface"]["subscribeQueueLength"] = 0;
    ASSERT_FALSE( ROS2DataSourceConfig::parseFromJson( testinput, config ) );
    testinput["ros2Interface"]["subscribeQueueLength"] = 100000;
    testinput["ros2Interface"]["introspectionLibraryCompare"] = "UNKNOWN";
    ASSERT_FALSE( ROS2DataSourceConfig::parseFromJson( testinput, config ) );
    testinput["ros2Interface"]["introspectionLibraryCompare"] = "ErrorAndFail";
    ASSERT_FALSE( ROS2DataSourceConfig::parseFromJson( testinput, config ) );
    testinput["ros2Interface"]["introspectionLibraryCompare"] = "Warn";
    ASSERT_FALSE( ROS2DataSourceConfig::parseFromJson( testinput, config ) );
    testinput["ros2Interface"]["introspectionLibraryCompare"] = "Ignore";
    ASSERT_FALSE( ROS2DataSourceConfig::parseFromJson( testinput, config ) );
    testinput["interfaceId"] = "";
    ASSERT_FALSE( ROS2DataSourceConfig::parseFromJson( testinput, config ) );
    testinput["interfaceId"] = "1";
    ASSERT_TRUE( ROS2DataSourceConfig::parseFromJson( testinput, config ) );
}

TEST_F( ROS2DataSourceTest, NodeTest )
{
    // ROS2DataSourceNode has base class Node. Only function from base class are mocked
    ROS2DataSourceNode ros2Node;
    EXPECT_CALL( nodeMock, create_generic_subscription ).WillRepeatedly( Return( nullptr ) );
    ASSERT_EQ( ros2Node.subscribe(
                   "topictest", "typetest", []( std::shared_ptr<rclcpp::SerializedMessage> ) {}, 100 ),
               false );
    ROS2DataSourceConfig config{
        std::string( "interface1" ), 50, CompareToIntrospection::ERROR_AND_FAIL_ON_DIFFERENCE, 100 };

    EXPECT_CALL( nodeMock, create_generic_subscription ).WillRepeatedly( Throw( std::exception() ) );
    ASSERT_EQ( ros2Node.subscribe(
                   "topictest", "typetest", []( std::shared_ptr<rclcpp::SerializedMessage> ) {}, 100 ),
               false );

    auto subscription = std::make_shared<rclcpp::GenericSubscription>();
    EXPECT_CALL( nodeMock, create_generic_subscription ).WillRepeatedly( Return( subscription ) );
    ASSERT_EQ( ros2Node.subscribe(
                   "topictest", "typetest", []( std::shared_ptr<rclcpp::SerializedMessage> ) {}, 100 ),
               true );
}

TEST_F( ROS2DataSourceTest, SpinOnlyWithAvailableDictionary )
{
    ROS2DataSourceConfig config{ std::string( "interface1" ), 2, CompareToIntrospection::WARN_ON_DIFFERENCE, 100 };
    EXPECT_CALL( multiThreadedExecutorMock, spin ).Times( 0 );
    ROS2DataSource ros2DataSource( config, signalBufferPtr );
    ros2DataSource.connect();
    // Make sure thread does not start spinning so sleep for some time
    std::this_thread::sleep_for( 2 * std::chrono::milliseconds( MINIMUM_WAIT_TIME_ONE_CYCLE_MS ) );

    EXPECT_CALL( multiThreadedExecutorMock, spin ).Times( 1 );
    auto dictionary = std::make_shared<ComplexDataDecoderDictionary>();
    ros2DataSource.onChangeOfActiveDictionary( dictionary, VehicleDataSourceProtocol::COMPLEX_DATA );
    std::this_thread::sleep_for( std::chrono::milliseconds( MINIMUM_WAIT_TIME_ONE_CYCLE_MS ) );
    ros2DataSource.disconnect();
}

TEST_F( ROS2DataSourceTest, FailedSanityCheckWithCompareOptionWarn )
{
    auto subscription = std::make_shared<rclcpp::GenericSubscription>();
    EXPECT_CALL( nodeMock, create_generic_subscription ).Times( 1 ).WillRepeatedly( Return( subscription ) );
    EXPECT_CALL( typeSupportMock, get_typesupport_library( std::string( "messageIdTypeTest" ), _ ) )
        .Times( 1 )
        .WillRepeatedly( Return( nullptr ) );

    ROS2DataSourceConfig config{ std::string( "interface1" ), 2, CompareToIntrospection::WARN_ON_DIFFERENCE, 100 };
    ROS2DataSource ros2DataSource( config, signalBufferPtr );
    ros2DataSource.connect();
    auto dictionary = std::make_shared<ComplexDataDecoderDictionary>();
    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest:messageIdTypeTest"].mCollectRaw = true;
    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest:messageIdTypeTest"].mSignalId = 123;

    ros2DataSource.onChangeOfActiveDictionary( dictionary, VehicleDataSourceProtocol::COMPLEX_DATA );
    std::this_thread::sleep_for( std::chrono::milliseconds( MINIMUM_WAIT_TIME_ONE_CYCLE_MS ) );
    ros2DataSource.disconnect();
}

TEST_F( ROS2DataSourceTest, FailedSanityCheckWithCompareOptionError )
{
    EXPECT_CALL( nodeMock, create_generic_subscription ).Times( 0 );
    EXPECT_CALL( typeSupportMock, get_typesupport_library( std::string( "messageIdTypeTest" ), _ ) )
        .Times( 1 )
        .WillRepeatedly( Return( nullptr ) );

    ROS2DataSourceConfig config{
        std::string( "interface1" ), 2, CompareToIntrospection::ERROR_AND_FAIL_ON_DIFFERENCE, 100 };
    ROS2DataSource ros2DataSource( config, signalBufferPtr );
    ros2DataSource.connect();
    auto dictionary = std::make_shared<ComplexDataDecoderDictionary>();
    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest:messageIdTypeTest"].mCollectRaw = true;
    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest:messageIdTypeTest"].mSignalId = 123;

    ros2DataSource.onChangeOfActiveDictionary( dictionary, VehicleDataSourceProtocol::COMPLEX_DATA );
    std::this_thread::sleep_for( std::chrono::milliseconds( MINIMUM_WAIT_TIME_ONE_CYCLE_MS ) );
    ros2DataSource.disconnect();
}

TEST_F( ROS2DataSourceTest, FailedSanityCheckWithCompareOptionIgnore )
{
    auto subscription = std::make_shared<rclcpp::GenericSubscription>();
    EXPECT_CALL( nodeMock, create_generic_subscription ).Times( 1 ).WillRepeatedly( Return( subscription ) );
    EXPECT_CALL( typeSupportMock, get_typesupport_library( std::string( "messageIdTypeTest" ), _ ) ).Times( 0 );

    ROS2DataSourceConfig config{ std::string( "interface1" ), 2, CompareToIntrospection::NO_CHECK, 100 };
    ROS2DataSource ros2DataSource( config, signalBufferPtr );
    ros2DataSource.connect();
    auto dictionary = std::make_shared<ComplexDataDecoderDictionary>();
    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest:messageIdTypeTest"].mCollectRaw = true;
    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest:messageIdTypeTest"].mSignalId = 123;

    ros2DataSource.onChangeOfActiveDictionary( dictionary, VehicleDataSourceProtocol::COMPLEX_DATA );
    std::this_thread::sleep_for( std::chrono::milliseconds( MINIMUM_WAIT_TIME_ONE_CYCLE_MS ) );
    ros2DataSource.disconnect();
}

TEST_F( ROS2DataSourceTest, MessageIdWithColon )
{
    auto subscription = std::make_shared<rclcpp::GenericSubscription>();
    EXPECT_CALL( nodeMock, create_generic_subscription( "messageIdTopicTest", "messageIdTypeTest", _, _, _ ) )
        .Times( 1 )
        .WillRepeatedly( Return( subscription ) );

    ROS2DataSourceConfig config{ std::string( "interface1" ), 2, CompareToIntrospection::WARN_ON_DIFFERENCE, 100 };
    ROS2DataSource ros2DataSource( config, signalBufferPtr );
    ros2DataSource.connect();
    auto dictionary = std::make_shared<ComplexDataDecoderDictionary>();
    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest:messageIdTypeTest"].mCollectRaw = true;
    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest:messageIdTypeTest"].mSignalId = 123;

    EXPECT_CALL( typeSupportMock, get_typesupport_library( std::string( "messageIdTypeTest" ), _ ) )
        .Times( AtLeast( 1 ) )
        .WillRepeatedly( Return( std::make_shared<rcpputils::SharedLibrary>() ) );
    ros2DataSource.onChangeOfActiveDictionary( dictionary, VehicleDataSourceProtocol::COMPLEX_DATA );
    std::this_thread::sleep_for( std::chrono::milliseconds( MINIMUM_WAIT_TIME_ONE_CYCLE_MS ) );
    ros2DataSource.disconnect();
}

TEST_F( ROS2DataSourceTest, MessageIdWithoutTypeTryUntilTypeFound )
{
    auto subscription = std::make_shared<rclcpp::GenericSubscription>();

    ROS2DataSourceConfig config{ std::string( "interface1" ), 2, CompareToIntrospection::NO_CHECK, 100 };
    ROS2DataSource ros2DataSource( config, signalBufferPtr );
    ros2DataSource.connect();
    auto dictionary = std::make_shared<ComplexDataDecoderDictionary>();
    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest"].mCollectRaw = true;
    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest"].mSignalId = 123;

    // Without finding the type subscribe should never be called
    EXPECT_CALL( nodeMock, create_generic_subscription( _, _, _, _, _ ) ).Times( 0 );
    std::map<std::string, std::vector<std::string>> ros2TopicsAndTypes{
        { "DifferentMessageIdTopicTest", { "DifferentTypeSeenInROS2" } } };
    EXPECT_CALL( nodeMock, get_topic_names_and_types() ).Times( AtLeast( 1 ) ).WillRepeatedly( [ros2TopicsAndTypes]() {
        return ros2TopicsAndTypes;
    } );

    ros2DataSource.onChangeOfActiveDictionary( dictionary, VehicleDataSourceProtocol::COMPLEX_DATA );
    std::this_thread::sleep_for( std::chrono::milliseconds( MINIMUM_WAIT_TIME_ONE_CYCLE_MS ) );

    // Now provide matching type from ROS2 get_topic_names_and_types so subscribe should be called
    startCountingSubscribeCalls( "EXPANDED__messageIdTopicTest", "typeSeenInROS2" );

    std::map<std::string, std::vector<std::string>> ros2TopicsAndTypes2{
        { "EXPANDED__messageIdTopicTest", { "typeSeenInROS2" } } };
    EXPECT_CALL( nodeMock, get_topic_names_and_types() )
        .Times( AtLeast( 1 ) )
        .WillRepeatedly( Return( ros2TopicsAndTypes2 ) );

    WAIT_ASSERT_EQ( subscribeCallsCounter.load(), 1 );

    ros2DataSource.disconnect();
}

TEST_F( ROS2DataSourceTest, FailIntrospectionCompareDifferentStructMemberCounts )
{
    EXPECT_CALL( nodeMock, create_generic_subscription( _, _, _, _, _ ) ).Times( 0 );

    ROS2DataSourceConfig config{
        std::string( "interface1" ), 2, CompareToIntrospection::ERROR_AND_FAIL_ON_DIFFERENCE, 100 };
    ROS2DataSource ros2DataSource( config, signalBufferPtr );
    ros2DataSource.connect();
    auto dictionary = std::make_shared<ComplexDataDecoderDictionary>();
    fillDefaultMessageType();

    boost::get<ComplexStruct>( defaultMessageFormat.mComplexTypeMap[140] )
        .mOrderedTypeIds.resize( 1 ); // change struct size

    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest:messageIdTypeTest"] =
        defaultMessageFormat;
    typeSupportReturnDefaultMessage( "messageIdTypeTest" );
    ros2DataSource.onChangeOfActiveDictionary( dictionary, VehicleDataSourceProtocol::COMPLEX_DATA );

    std::this_thread::sleep_for( 2 * std::chrono::milliseconds( MINIMUM_WAIT_TIME_ONE_CYCLE_MS ) );

    ros2DataSource.disconnect();
}

TEST_F( ROS2DataSourceTest, FailIntrospectionCompareUnknownComplexType )
{
    EXPECT_CALL( nodeMock, create_generic_subscription( _, _, _, _, _ ) ).Times( 0 );

    ROS2DataSourceConfig config{
        std::string( "interface1" ), 2, CompareToIntrospection::ERROR_AND_FAIL_ON_DIFFERENCE, 100 };
    ROS2DataSource ros2DataSource( config, signalBufferPtr );
    ros2DataSource.connect();
    auto dictionary = std::make_shared<ComplexDataDecoderDictionary>();
    fillDefaultMessageType();

    boost::get<ComplexStruct>( defaultMessageFormat.mComplexTypeMap[140] ).mOrderedTypeIds[0] = 9999999; // unknown type

    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest:messageIdTypeTest"] =
        defaultMessageFormat;
    typeSupportReturnDefaultMessage( "messageIdTypeTest" );
    ros2DataSource.onChangeOfActiveDictionary( dictionary, VehicleDataSourceProtocol::COMPLEX_DATA );

    std::this_thread::sleep_for( 2 * std::chrono::milliseconds( MINIMUM_WAIT_TIME_ONE_CYCLE_MS ) );

    ros2DataSource.disconnect();
}

TEST_F( ROS2DataSourceTest, FailIntrospectionCompareNoStructInDecodingInformation )
{
    EXPECT_CALL( nodeMock, create_generic_subscription( _, _, _, _, _ ) ).Times( 0 );

    ROS2DataSourceConfig config{
        std::string( "interface1" ), 2, CompareToIntrospection::ERROR_AND_FAIL_ON_DIFFERENCE, 100 };
    ROS2DataSource ros2DataSource( config, signalBufferPtr );
    ros2DataSource.connect();
    auto dictionary = std::make_shared<ComplexDataDecoderDictionary>();
    fillDefaultMessageType();

    boost::get<ComplexStruct>( defaultMessageFormat.mComplexTypeMap[100] ).mOrderedTypeIds[3] =
        110; // top level expects at forth position a struct instead give a primitive type

    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest:messageIdTypeTest"] =
        defaultMessageFormat;
    typeSupportReturnDefaultMessage( "messageIdTypeTest" );
    ros2DataSource.onChangeOfActiveDictionary( dictionary, VehicleDataSourceProtocol::COMPLEX_DATA );

    std::this_thread::sleep_for( 2 * std::chrono::milliseconds( MINIMUM_WAIT_TIME_ONE_CYCLE_MS ) );

    ros2DataSource.disconnect();
}

TEST_F( ROS2DataSourceTest, FailIntrospectionCompareNullptr )
{
    EXPECT_CALL( nodeMock, create_generic_subscription( _, _, _, _, _ ) ).Times( 0 );

    ROS2DataSourceConfig config{
        std::string( "interface1" ), 2, CompareToIntrospection::ERROR_AND_FAIL_ON_DIFFERENCE, 100 };
    ROS2DataSource ros2DataSource( config, signalBufferPtr );
    ros2DataSource.connect();
    auto dictionary = std::make_shared<ComplexDataDecoderDictionary>();
    fillDefaultMessageType();

    defaultRos2TopLevelMessage.data = nullptr;

    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest:messageIdTypeTest"] =
        defaultMessageFormat;
    typeSupportReturnDefaultMessage( "messageIdTypeTest" );
    ros2DataSource.onChangeOfActiveDictionary( dictionary, VehicleDataSourceProtocol::COMPLEX_DATA );

    std::this_thread::sleep_for( 2 * std::chrono::milliseconds( MINIMUM_WAIT_TIME_ONE_CYCLE_MS ) );

    ros2DataSource.disconnect();
}

TEST_F( ROS2DataSourceTest, FailIntrospectionCompareDifferentStringPrimitiveType )
{
    EXPECT_CALL( nodeMock, create_generic_subscription( _, _, _, _, _ ) ).Times( 0 );

    ROS2DataSourceConfig config{
        std::string( "interface1" ), 2, CompareToIntrospection::ERROR_AND_FAIL_ON_DIFFERENCE, 100 };
    ROS2DataSource ros2DataSource( config, signalBufferPtr );
    ros2DataSource.connect();
    auto dictionary = std::make_shared<ComplexDataDecoderDictionary>();
    fillDefaultMessageType();

    boost::get<ComplexArray>( defaultMessageFormat.mComplexTypeMap[160] ).mRepeatedTypeId =
        148; // string always expects uint8_t bit 148 is FLOAT

    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest:messageIdTypeTest"] =
        defaultMessageFormat;
    typeSupportReturnDefaultMessage( "messageIdTypeTest" );
    ros2DataSource.onChangeOfActiveDictionary( dictionary, VehicleDataSourceProtocol::COMPLEX_DATA );

    std::this_thread::sleep_for( 2 * std::chrono::milliseconds( MINIMUM_WAIT_TIME_ONE_CYCLE_MS ) );

    ros2DataSource.disconnect();
}

TEST_F( ROS2DataSourceTest, FailIntrospectionCompareDifferentStringLength )
{
    EXPECT_CALL( nodeMock, create_generic_subscription( _, _, _, _, _ ) ).Times( 0 );

    ROS2DataSourceConfig config{
        std::string( "interface1" ), 2, CompareToIntrospection::ERROR_AND_FAIL_ON_DIFFERENCE, 100 };
    ROS2DataSource ros2DataSource( config, signalBufferPtr );
    ros2DataSource.connect();
    auto dictionary = std::make_shared<ComplexDataDecoderDictionary>();
    fillDefaultMessageType();

    boost::get<ComplexArray>( defaultMessageFormat.mComplexTypeMap[160] ).mSize =
        7; // string always expects dynamic sized array

    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest:messageIdTypeTest"] =
        defaultMessageFormat;
    typeSupportReturnDefaultMessage( "messageIdTypeTest" );
    ros2DataSource.onChangeOfActiveDictionary( dictionary, VehicleDataSourceProtocol::COMPLEX_DATA );

    std::this_thread::sleep_for( 2 * std::chrono::milliseconds( MINIMUM_WAIT_TIME_ONE_CYCLE_MS ) );

    ros2DataSource.disconnect();
}

TEST_F( ROS2DataSourceTest, SuccessfullyDecodeComplexMessage )
{
    ROS2DataSourceConfig config{
        std::string( "interface1" ), 50, CompareToIntrospection::ERROR_AND_FAIL_ON_DIFFERENCE, 100 };
    ROS2DataSource ros2DataSource( config, signalBufferPtr, rawBufferManagerMock );
    ros2DataSource.connect();
    auto dictionary = std::make_shared<ComplexDataDecoderDictionary>();

    fillDefaultMessageType();
    dictionary->complexMessageDecoderMethod["interface1"]["messageIdTopicTest:messageIdTypeTest"] =
        defaultMessageFormat;

    typeSupportReturnDefaultMessage( "messageIdTypeTest" );

    std::vector<std::pair<SignalID, size_t>> dataElements;
    EXPECT_CALL( *rawBufferManagerMock, push( _, _, _, _ ) )
        .Times( AtLeast( 1 ) )
        .WillRepeatedly( ( [&dataElements]( uint8_t *data,
                                            size_t size,
                                            Timestamp receiveTimestamp,
                                            RawData::BufferTypeId typeId ) -> RawData::BufferHandle {
            static_cast<void>( data );             // Unused
            static_cast<void>( receiveTimestamp ); // Unused
            dataElements.emplace_back( typeId, size );
            return 7890;
        } ) );

    startCountingSubscribeCalls( "messageIdTopicTest", "messageIdTypeTest" );
    ros2DataSource.onChangeOfActiveDictionary( dictionary, VehicleDataSourceProtocol::COMPLEX_DATA );
    WAIT_ASSERT_EQ( subscribeCallsCounter.load(), 1 );

    fillDefaultSerializedMessage();
    lastSubscribedCallback.operator()( defaultSerializedMessage );
    WAIT_ASSERT_EQ( dataElements.size(), 1U ); // Default message has mCollectRaw = true;
    ASSERT_EQ( dataElements.back().first, 123 );

    CollectedDataFrame dataFrame;
    WAIT_ASSERT_TRUE( signalBufferPtr->pop( dataFrame ) );

    auto signalGroup = dataFrame.mCollectedSignals;
    auto signal1 = signalGroup[0];
    ASSERT_EQ( signal1.signalID, 0x80001111 );

    auto signal2 = signalGroup[1];
    ASSERT_EQ( signal2.signalID, 0x80001112 );
    ASSERT_EQ( signal2.getValue().value.doubleVal, static_cast<double>( 5 ) );

    auto signal3 = signalGroup[2];
    ASSERT_EQ( signal3.signalID, 0x80001113 );
    ASSERT_EQ( signal3.getValue().value.doubleVal, static_cast<double>( 10 ) );

    auto signal4 = signalGroup[3];
    ASSERT_EQ( signal4.signalID, 0x80001114 );
    ASSERT_EQ( signal4.getValue().value.doubleVal, static_cast<double>( 'u' ) );

    auto signal5 = signalGroup[4];
    ASSERT_EQ( signal5.signalID, 123 ); // first the raw buffer handle
    ASSERT_EQ( signal5.getValue().value.uint32Val, 7890 );

    ros2DataSource.disconnect();
}

} // namespace IoTFleetWise
} // namespace Aws
