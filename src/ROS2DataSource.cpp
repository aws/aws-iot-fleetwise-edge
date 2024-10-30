// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ROS2DataSource.h"
#include "LoggingModule.h"
#include "QueueTypes.h"
#include <boost/variant.hpp>
#include <chrono>
#include <exception>
#include <fastcdr/FastBuffer.h>
#include <map>
#include <rclcpp/rclcpp.hpp>
#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <rosidl_typesupport_introspection_cpp/identifier.hpp>
#include <unordered_map>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

using std::placeholders::_1;

static constexpr int ROS2_DATA_SOURCE_INTERVAL_MS = 100;
static constexpr int ROS2_RETRY_TOPIC_SUBSCRIBE_MS = 100;
static constexpr int MAX_TYPE_TREE_DEPTH = 100;

constexpr uint32_t DEFAULT_STARTING_OFFSET = 0;

bool
ROS2DataSourceConfig::parseFromJson( const IoTFleetWiseConfig &node, ROS2DataSourceConfig &outputConfig )
{
    try
    {
        outputConfig.mExecutorThreads =
            static_cast<uint8_t>( node["ros2Interface"]["executorThreads"].asU32Required() );
        outputConfig.mSubscribeQueueLength =
            static_cast<size_t>( node["ros2Interface"]["subscribeQueueLength"].asU32Required() );
        auto introspectionLibraryCompareString =
            node["ros2Interface"]["introspectionLibraryCompare"].asStringRequired();
        if ( introspectionLibraryCompareString == "ErrorAndFail" )
        {
            outputConfig.mIntrospectionLibraryCompare = CompareToIntrospection::ERROR_AND_FAIL_ON_DIFFERENCE;
        }
        else if ( introspectionLibraryCompareString == "Warn" )
        {
            outputConfig.mIntrospectionLibraryCompare = CompareToIntrospection::WARN_ON_DIFFERENCE;
        }
        else if ( introspectionLibraryCompareString == "Ignore" )
        {
            outputConfig.mIntrospectionLibraryCompare = CompareToIntrospection::NO_CHECK;
        }
        else
        {
            FWE_LOG_ERROR( "introspectionLibraryCompare must be set to either ErrorAndFail, Warn or Ignore" );
            return false;
        }

        outputConfig.mInterfaceId = node["interfaceId"].asStringRequired();
        if ( outputConfig.mInterfaceId.empty() )
        {
            FWE_LOG_ERROR( "interfaceId must be set" );
            return false;
        }
    }
    catch ( const std::exception &e )
    {
        FWE_LOG_ERROR( std::string( ( e.what() == nullptr ? "" : e.what() ) ) );
        return false;
    }
    return true;
}

ROS2DataSourceNode::ROS2DataSourceNode()
    : Node( "FWEros2DataSourceNode" )
{
    mCallbackGroup = this->create_callback_group( rclcpp::CallbackGroupType::Reentrant );
}

bool
ROS2DataSourceNode::subscribe( std::string topic,
                               std::string type,
                               std::function<void( std::shared_ptr<rclcpp::SerializedMessage> )> callback,
                               size_t queueLength )
{
    try
    {
        rclcpp::SubscriptionOptions sub_options;
        sub_options.callback_group = mCallbackGroup;

        auto subscription = this->create_generic_subscription( topic, type, queueLength, callback, sub_options );
        if ( subscription )
        {
            mSubscriptions.push_back( subscription );
            FWE_LOG_INFO( "Subscribed to topic: '" + topic + "' with type: '" + type + "'" );
            return true;
        }
        else
        {
            FWE_LOG_ERROR( "Empty Subscription for topic: '" + topic + "' with type: '" + type + "'" );
            return false;
        }
    }
    catch ( const std::exception &e )
    {
        FWE_LOG_ERROR( "Exception while subscribing to topic: '" + topic + "' with type: '" + type +
                       "' : " + std::string( ( e.what() == nullptr ? "" : e.what() ) ) );
        return false;
    }
    catch ( ... )
    {
        FWE_LOG_ERROR( "Unknown Exception while subscribing to topic: '" + topic + "' with type: '" + type + "'" );
        return false;
    }
    return false;
}

ROS2DataSource::ROS2DataSource( ROS2DataSourceConfig config,
                                SignalBufferDistributorPtr signalBufferDistributor,
                                std::shared_ptr<RawData::BufferManager> rawDataBufferManager )
    : mConfig( std::move( config ) )
    , mSignalBufferDistributor( std::move( signalBufferDistributor ) )
    , mRawBufferManager( std::move( rawDataBufferManager ) )
{
    mNode = std::make_shared<ROS2DataSourceNode>();
    mROS2Executor = std::make_unique<rclcpp::executors::MultiThreadedExecutor>(
        rclcpp::ExecutorOptions(), mConfig.mExecutorThreads, false, std::chrono::milliseconds( 500 ) );
    mROS2Executor->add_node( mNode );
}

void
ROS2DataSource::onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                            VehicleDataSourceProtocol networkProtocol )
{
    if ( networkProtocol == VehicleDataSourceProtocol::COMPLEX_DATA )
    {
        FWE_LOG_TRACE( "Decoder Dictionary with complex data update" );
        auto decoderDictionaryPtr = std::dynamic_pointer_cast<const ComplexDataDecoderDictionary>( dictionary );
        {
            std::lock_guard<std::mutex> lock( mDecoderDictMutex );
            mAvailableDict = decoderDictionaryPtr;
            mEventNewDecoderManifestAvailable = true;
        }
        mShouldSleep = false;
        mWait.notify();
    }
}

// Name the executer threads the first time they process a callback. Until then they will have the name of the main
// thread fwVNROS2main
static thread_local bool gAlreadyNamedThread{ false }; // NOLINT Thread local thread named flag
static std::atomic<int> gThreadCounter{ 1 };           // NOLINT Global atomic thread counter

/*
 * called from multiple threads in parallel as callback group is Reentrant
 */
void
ROS2DataSource::topicCallback( std::shared_ptr<rclcpp::SerializedMessage> msg, std::string dictionaryMessageId )
{
    if ( !gAlreadyNamedThread )
    {
        // coverity[misra_cpp_2008_rule_5_2_10_violation] For std::atomic this must be performed in a single statement
        // coverity[autosar_cpp14_m5_2_10_violation] For std::atomic this must be performed in a single statement
        Thread::setCurrentThreadName( "fwVNROS2exec" + std::to_string( gThreadCounter++ ) );
        gAlreadyNamedThread = true;
    }
    FWE_LOG_TRACE( "Received message with size:" + std::to_string( msg->get_rcl_serialized_message().buffer_length ) +
                   " for message id: '" + dictionaryMessageId + "'" );
    std::shared_ptr<const ComplexDataDecoderDictionary> dict;
    {
        std::lock_guard<std::mutex> lock( mDecoderDictMutex );
        dict = mCurrentDict;
    }
    auto interface = dict->complexMessageDecoderMethod.find( mConfig.mInterfaceId );
    if ( interface == dict->complexMessageDecoderMethod.end() )
    {
        FWE_LOG_WARN( "Received message without decoder dictionary for interface: '" + mConfig.mInterfaceId + "'" );
        return;
    }
    auto message = interface->second.find( dictionaryMessageId );
    if ( message == interface->second.end() )
    {
        FWE_LOG_WARN( "Received message without decoder dictionary message id: '" + dictionaryMessageId + "'" );
        return;
    }
    auto timestamp = mClock->systemTimeSinceEpochMs();
    const struct ComplexDataMessageFormat &messageFormat = message->second;

    CollectedSignalsGroup collectedSignalsGroup;
    if ( !messageFormat.mSignalPaths.empty() )
    {
        SignalPath emptyPath;
        uint32_t currentPathIndex = 0;
        try
        {
            eprosima::fastcdr::FastBuffer buffer( reinterpret_cast<char *>( msg->get_rcl_serialized_message().buffer ),
                                                  msg->get_rcl_serialized_message().buffer_length );
            eprosima::fastcdr::Cdr cdr(
                buffer, eprosima::fastcdr::Cdr::DEFAULT_ENDIAN, eprosima::fastcdr::Cdr::DDS_CDR );
            cdr.read_encapsulation();
            recursiveExtractFromCDR( cdr,
                                     messageFormat.mRootTypeId,
                                     emptyPath,
                                     0,
                                     currentPathIndex,
                                     messageFormat,
                                     timestamp,
                                     collectedSignalsGroup );
        }
        catch ( const std::exception &e )
        {
            FWE_LOG_ERROR( "Could not read message '" + dictionaryMessageId +
                           "' because of error: " + std::string( e.what() ) );
        }
        if ( currentPathIndex < messageFormat.mSignalPaths.size() )
        {
            FWE_LOG_WARN( "Not all paths matched in message '" + dictionaryMessageId + "'. Next path to match: " +
                          pathToString( messageFormat.mSignalPaths[currentPathIndex].mSignalPath ) );
        }
    }
    if ( messageFormat.mCollectRaw )
    {
        if ( mRawBufferManager == nullptr )
        {
            FWE_LOG_WARN( "Raw message id: '" + dictionaryMessageId + "' can not be handed over to RawBufferManager" );
        }
        else
        {
            auto bufferHandle =
                mRawBufferManager->push( reinterpret_cast<uint8_t *>( msg->get_rcl_serialized_message().buffer ),
                                         msg->get_rcl_serialized_message().buffer_length,
                                         timestamp,
                                         messageFormat.mSignalId );
            if ( bufferHandle == RawData::INVALID_BUFFER_HANDLE )
            {
                FWE_LOG_WARN( "Raw message id: '" + dictionaryMessageId + "' was rejected by RawBufferManager" );
            }
            else
            {
                // immediately set usage hint so buffer handle does not get directly deleted again
                mRawBufferManager->increaseHandleUsageHint(
                    messageFormat.mSignalId,
                    bufferHandle,
                    RawData::BufferHandleUsageStage::COLLECTED_NOT_IN_HISTORY_BUFFER );
                auto collectedSignal =
                    CollectedSignal( messageFormat.mSignalId, timestamp, bufferHandle, SignalType::COMPLEX_SIGNAL );

                collectedSignalsGroup.push_back( collectedSignal );
            }
        }
    }
    mSignalBufferDistributor->push( CollectedDataFrame( collectedSignalsGroup ) );
}

void
ROS2DataSource::readPrimitiveDataFromCDR( eprosima::fastcdr::Cdr &cdr,
                                          struct PrimitiveData &format,
                                          SignalID signalId,
                                          Timestamp timestamp,
                                          CollectedSignalsGroup &collectedSignalsGroup )
{
    struct CollectedSignal collectedSignal;
    bool unknownType = false;

    switch ( format.mPrimitiveType )
    {
    case SignalType::BOOLEAN: {
        bool b = false;
        // coverity[misra_cpp_2008_rule_14_8_2_violation] - FastCDR header defines both template and type overloads
        cdr >> b;
        collectedSignal = CollectedSignal( signalId, timestamp, b, format.mPrimitiveType );
    }
    break;
    case SignalType::UINT8: {
        uint8_t u8 = 0;
        // coverity[misra_cpp_2008_rule_14_8_2_violation] - FastCDR header defines both template and type overloads
        cdr >> u8;
        u8 = static_cast<uint8_t>( static_cast<uint8_t>( format.mOffset ) +
                                   u8 * static_cast<uint8_t>( format.mScaling ) );
        collectedSignal = CollectedSignal( signalId, timestamp, u8, format.mPrimitiveType );
    }
    break;
    case SignalType::UINT16: {
        uint16_t u16 = 0;
        // coverity[misra_cpp_2008_rule_14_8_2_violation] - FastCDR header defines both template and type overloads
        cdr >> u16;
        u16 = static_cast<uint16_t>( static_cast<uint16_t>( format.mOffset ) +
                                     u16 * static_cast<uint16_t>( format.mScaling ) );
        collectedSignal = CollectedSignal( signalId, timestamp, u16, format.mPrimitiveType );
    }
    break;
    case SignalType::UINT32: {
        uint32_t u32 = 0;
        // coverity[misra_cpp_2008_rule_14_8_2_violation] - FastCDR header defines both template and type overloads
        cdr >> u32;
        u32 *= static_cast<uint32_t>( format.mScaling );
        u32 += static_cast<uint32_t>( format.mOffset );
        collectedSignal = CollectedSignal( signalId, timestamp, u32, format.mPrimitiveType );
    }
    break;
    case SignalType::UINT64: {
        uint64_t u64 = 0;
        // coverity[misra_cpp_2008_rule_14_8_2_violation] - FastCDR header defines both template and type overloads
        cdr >> u64;
        u64 *= static_cast<uint64_t>( format.mScaling );
        u64 += static_cast<uint64_t>( format.mOffset );
        collectedSignal = CollectedSignal( signalId, timestamp, u64, format.mPrimitiveType );
    }
    break;
    case SignalType::INT8: {
        int8_t i8 = 0;
        // coverity[misra_cpp_2008_rule_14_8_2_violation] - FastCDR header defines both template and type overloads
        cdr >> i8;
        i8 = static_cast<int8_t>( static_cast<int8_t>( format.mOffset ) + i8 * static_cast<int8_t>( format.mScaling ) );
        collectedSignal = CollectedSignal( signalId, timestamp, i8, format.mPrimitiveType );
    }
    break;
    case SignalType::INT16: {
        int16_t i16 = 0;
        // coverity[misra_cpp_2008_rule_14_8_2_violation] - FastCDR header defines both template and type overloads
        cdr >> i16;
        i16 = static_cast<int16_t>( static_cast<int16_t>( format.mOffset ) +
                                    i16 * static_cast<int16_t>( format.mScaling ) );
        collectedSignal = CollectedSignal( signalId, timestamp, i16, format.mPrimitiveType );
    }
    break;
    case SignalType::INT32: {
        int32_t i32 = 0;
        // coverity[misra_cpp_2008_rule_14_8_2_violation] - FastCDR header defines both template and type overloads
        cdr >> i32;
        i32 *= static_cast<int32_t>( format.mScaling );
        i32 += static_cast<int32_t>( format.mOffset );
        collectedSignal = CollectedSignal( signalId, timestamp, i32, format.mPrimitiveType );
    }
    break;
    case SignalType::INT64: {
        int64_t i64 = 0;
        // coverity[misra_cpp_2008_rule_14_8_2_violation] - FastCDR header defines both template and type overloads
        cdr >> i64;
        i64 *= static_cast<int64_t>( format.mScaling );
        i64 += static_cast<int64_t>( format.mOffset );
        collectedSignal = CollectedSignal( signalId, timestamp, i64, format.mPrimitiveType );
    }
    break;
    case SignalType::FLOAT: {
        float f = 0;
        // coverity[misra_cpp_2008_rule_14_8_2_violation] - FastCDR header defines both template and type overloads
        cdr >> f;
        f *= static_cast<float>( format.mScaling );
        f += static_cast<float>( format.mOffset );
        collectedSignal = CollectedSignal( signalId, timestamp, f, format.mPrimitiveType );
    }
    break;
    case SignalType::DOUBLE: {
        double d = 0;
        // coverity[misra_cpp_2008_rule_14_8_2_violation] - FastCDR header defines both template and type overloads
        cdr >> d;
        d *= format.mScaling;
        d += format.mOffset;
        collectedSignal = CollectedSignal( signalId, timestamp, d, format.mPrimitiveType );
    }
    break;
    default:
        unknownType = true;
        break;
    }
    if ( ( !unknownType ) && ( signalId != INVALID_SIGNAL_ID ) && ( collectedSignal.signalID != INVALID_SIGNAL_ID ) )
    {
        collectedSignalsGroup.push_back( collectedSignal );
    }
}

bool
ROS2DataSource::sanityCheckType( std::string dictionaryMessageId, std::string type )
{
    if ( mConfig.mIntrospectionLibraryCompare == CompareToIntrospection::NO_CHECK )
    {
        return true;
    }
    try
    {
        bool isSameAsIntrospection = compareToIntrospectionTypeTree( dictionaryMessageId, type );
        if ( isSameAsIntrospection )
        {
            return true;
        }
    }
    catch ( const std::exception &e )
    {
        FWE_LOG_ERROR( "Exception while comparing '" + type +
                       "' to introspection library : " + std::string( e.what() ) );
        return false;
    }
    if ( mConfig.mIntrospectionLibraryCompare == CompareToIntrospection::ERROR_AND_FAIL_ON_DIFFERENCE )
    {
        FWE_LOG_ERROR( "A difference between the decoder information from cloud and the information from introspection "
                       "was found for message '" +
                       dictionaryMessageId + "'. Because of configuration these messages will never be processed" );
        return false;
    }
    if ( mConfig.mIntrospectionLibraryCompare == CompareToIntrospection::WARN_ON_DIFFERENCE )
    {
        FWE_LOG_WARN( "A difference between the decoder information from cloud and the information from introspection "
                      "was found for message '" +
                      dictionaryMessageId + "'. Because of configuration these messages will still be processed" );
        return true;
    }
    return true;
}

bool
ROS2DataSource::compareToIntrospectionTypeTree( std::string dictionaryMessageId, std::string type )
{

    auto interface = mCurrentDict->complexMessageDecoderMethod.find( mConfig.mInterfaceId );
    if ( interface == mCurrentDict->complexMessageDecoderMethod.end() )
    {
        FWE_LOG_WARN( "Received message without decoder dictionary for interface: '" + mConfig.mInterfaceId + "'" );
        return false;
    }
    auto message = interface->second.find( dictionaryMessageId );
    if ( message == interface->second.end() )
    {
        FWE_LOG_WARN( "Received message without decoder dictionary message id: '" + dictionaryMessageId + "'" );
        return false;
    }
    const struct ComplexDataMessageFormat &messageFormat = message->second;

    auto introspectionSharedLibrary =
        rclcpp::get_typesupport_library( type,
                                         ( rosidl_typesupport_introspection_cpp::typesupport_identifier == nullptr
                                               ? ""
                                               : rosidl_typesupport_introspection_cpp::typesupport_identifier ) );
    if ( !introspectionSharedLibrary )
    {
        FWE_LOG_ERROR( "Can not load introspection library" );
        return false;
    }
    auto introspectionTypeSupport = rclcpp::get_typesupport_handle(
        type, rosidl_typesupport_introspection_cpp::typesupport_identifier, *introspectionSharedLibrary );

    if ( introspectionTypeSupport == nullptr )
    {
        FWE_LOG_ERROR( "Can not get typesupport handle" );
        return false;
    }
    auto members =
        static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>( introspectionTypeSupport->data );

    SignalPath emtpyPath;
    return recursiveCompareToIntrospectionTypeTree(
        DEFAULT_STARTING_OFFSET, members, messageFormat, messageFormat.mRootTypeId, emtpyPath );
}

bool
ROS2DataSource::compareArraySizeToIntrospectionType( const rosidl_typesupport_introspection_cpp::MessageMember &member,
                                                     ComplexArray &complexArray )
{
    return !( ( ( ( member.array_size_ != 0 ) && ( !member.is_upper_bound_ ) ) || ( complexArray.mSize > 0 ) ) &&
              ( static_cast<int64_t>( member.array_size_ ) != complexArray.mSize ) );
}

bool
ROS2DataSource::compareStringToIntrospectionType( ComplexDataTypeId type,
                                                  const rosidl_typesupport_introspection_cpp::MessageMember &member,
                                                  const struct ComplexDataMessageFormat &format )
{
    ComplexArray complexArrayString;
    if ( !getArrayFromComplexType( complexArrayString, type, format ) )
    {
        FWE_LOG_WARN( "Introspection library expected string but no array found." );
        return false;
    }
    if ( complexArrayString.mSize > 0 )
    {
        FWE_LOG_WARN( "Introspection library expected string with dynamic size but provided size: " +
                      std::to_string( complexArrayString.mSize ) );
        return false;
    }
    uint8_t expectedRosType = ( member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING
                                    ? rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT8
                                    // ROS2 implementation uses uint32 for utf-16 (wstring) code units
                                    : rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT32 );
    if ( !isTypeIdCompatiblePrimitiveToIntrospectionType(
             complexArrayString.mRepeatedTypeId, format, expectedRosType ) )
    {
        FWE_LOG_WARN( "Introspection library expects string, which translates to array of type: " +
                      std::to_string( expectedRosType ) + " but dictionary provides incompatible type id: " +
                      std::to_string( complexArrayString.mRepeatedTypeId ) );
        return false;
    }
    return true;
}

bool
ROS2DataSource::recursiveCompareToIntrospectionTypeTree(
    const uint32_t &startingOffset,
    const rosidl_typesupport_introspection_cpp::MessageMembers *messageMembers,
    const struct ComplexDataMessageFormat &format,
    ComplexDataTypeId type,
    SignalPath &currentPath )
{
    if ( currentPath.size() >= MAX_TYPE_TREE_DEPTH )
    {
        FWE_LOG_ERROR( "Complex Tree to deep. Potentially circle in tree. Type id: " + std::to_string( type ) +
                       " at path " + pathToString( currentPath ) );
        return false;
    }
    auto complexType = format.mComplexTypeMap.find( type );
    if ( complexType == format.mComplexTypeMap.end() )
    {
        FWE_LOG_WARN( "Unknown complex type id: " + std::to_string( type ) );
        return false;
    }
    if ( complexType->second.type() != typeid( ComplexStruct ) )
    {
        FWE_LOG_WARN( "Introspection library expects struct at path: " + pathToString( currentPath ) );
        return false;
    }
    auto complexStruct = boost::get<ComplexStruct>( complexType->second );

    if ( messageMembers == nullptr )
    {
        FWE_LOG_WARN( "Introspection library provided nullptr as messageMembers at path: " +
                      pathToString( currentPath ) );
        return false;
    }

    if ( complexStruct.mOrderedTypeIds.size() != messageMembers->member_count_ )
    {
        FWE_LOG_WARN( "Introspection library expected struct with " + std::to_string( messageMembers->member_count_ ) +
                      " members but dictionary provides " + std::to_string( complexStruct.mOrderedTypeIds.size() ) +
                      " at path: " + pathToString( currentPath ) );
        return false;
    }
    auto currentPathSize = currentPath.size();
    for ( size_t i = 0; i < messageMembers->member_count_; i++ )
    {
        currentPath.resize( currentPathSize );
        currentPath.push_back( static_cast<uint32_t>( i ) );
        const rosidl_typesupport_introspection_cpp::MessageMember &member = messageMembers->members_[i];

        // Only for debug: +pathToString(currentPath)
        FWE_LOG_TRACE( "From introspection library type info: is_array_: " + std::to_string( member.is_array_ ) +
                       ", type_id_: " + std::to_string( member.type_id_ ) + ", array_size_: " +
                       std::to_string( member.array_size_ ) + " at path " + pathToString( currentPath ) );
        if ( ( !member.is_array_ ) && ( member.type_id_ != rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING ) &&
             ( member.type_id_ != rosidl_typesupport_introspection_cpp::ROS_TYPE_WSTRING ) &&
             ( member.type_id_ != rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE ) )
        {
            auto primitiveData = boost::get<ComplexStruct>( complexType->second );
            if ( !isTypeIdCompatiblePrimitiveToIntrospectionType(
                     complexStruct.mOrderedTypeIds[i], format, member.type_id_ ) )
            {
                FWE_LOG_WARN( "Introspection library expects primitive type: " + std::to_string( member.type_id_ ) +
                              " but dictionary provides incompatible type id: " +
                              std::to_string( complexStruct.mOrderedTypeIds[i] ) +
                              " at path: " + pathToString( currentPath ) );
                return false;
            }
        }
        else if ( ( !member.is_array_ ) &&
                  ( member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE ) )
        {
            bool ret = recursiveCompareToIntrospectionTypeTree(
                startingOffset,
                static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>( member.members_->data ),
                format,
                complexStruct.mOrderedTypeIds[i],
                currentPath );
            if ( !ret )
            {
                return false;
            }
        }
        else if ( ( member.is_array_ ) &&
                  ( member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_MESSAGE ) )
        {
            ComplexArray complexArray;
            if ( !getArrayFromComplexType( complexArray, complexStruct.mOrderedTypeIds[i], format ) )
            {
                FWE_LOG_WARN( "Introspection library expected array at path: " + pathToString( currentPath ) );
                return false;
            }
            if ( !compareArraySizeToIntrospectionType( member, complexArray ) )
            {
                FWE_LOG_WARN( "Introspection library expected array with size" + std::to_string( member.array_size_ ) +
                              " but provided size: " + std::to_string( complexArray.mSize ) +
                              " path: " + pathToString( currentPath ) );
                return false;
            }
            currentPath.push_back( 0 ); // Check only first element of array as they are all the same
            bool ret = recursiveCompareToIntrospectionTypeTree(
                startingOffset,
                static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers *>( member.members_->data ),
                format,
                complexArray.mRepeatedTypeId,
                currentPath );
            if ( !ret )
            {
                return false;
            }
        }
        else if ( member.is_array_ &&
                  ( ( member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING ) ||
                    ( member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_WSTRING ) ) )
        {
            ComplexArray complexArray;
            if ( !getArrayFromComplexType( complexArray, complexStruct.mOrderedTypeIds[i], format ) )
            {
                FWE_LOG_WARN( "Introspection library expected array of strings at path: " +
                              pathToString( currentPath ) );
                return false;
            }
            if ( !compareArraySizeToIntrospectionType( member, complexArray ) )
            {
                FWE_LOG_WARN( "Introspection library expected array of strings with size" +
                              std::to_string( member.array_size_ ) + " but provided size: " +
                              std::to_string( complexArray.mSize ) + " path: " + pathToString( currentPath ) );
                return false;
            }
            if ( !compareStringToIntrospectionType( complexArray.mRepeatedTypeId, member, format ) )
            {
                FWE_LOG_WARN( "Introspection library expected array of string at path: " +
                              pathToString( currentPath ) );
                return false;
            }
        }
        else if ( member.is_array_ )
        {
            ComplexArray complexArray;
            if ( !getArrayFromComplexType( complexArray, complexStruct.mOrderedTypeIds[i], format ) )
            {
                FWE_LOG_WARN( "Introspection library expected array of primitives at path: " +
                              pathToString( currentPath ) );
                return false;
            }
            if ( !compareArraySizeToIntrospectionType( member, complexArray ) )
            {
                FWE_LOG_WARN( "Introspection library expected array of primitives with size" +
                              std::to_string( member.array_size_ ) + " but provided size: " +
                              std::to_string( complexArray.mSize ) + " path: " + pathToString( currentPath ) );
                return false;
            }
            if ( !isTypeIdCompatiblePrimitiveToIntrospectionType(
                     complexArray.mRepeatedTypeId, format, member.type_id_ ) )
            {
                FWE_LOG_WARN(
                    "Introspection library expects array type " + std::to_string( member.type_id_ ) +
                    " but dictionary provides incompatible type id: " + std::to_string( complexArray.mRepeatedTypeId ) +
                    " at path: " + pathToString( currentPath ) );
                return false;
            }
        }
        else if ( ( !member.is_array_ ) &&
                  ( ( member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_STRING ) ||
                    ( member.type_id_ == rosidl_typesupport_introspection_cpp::ROS_TYPE_WSTRING ) ) )
        {
            if ( !compareStringToIntrospectionType( complexStruct.mOrderedTypeIds[i], member, format ) )
            {
                FWE_LOG_WARN( "Introspection library expected string at path: " + pathToString( currentPath ) );
                return false;
            }
        }
        else
        {
            FWE_LOG_WARN( "Introspection library gives unexpected type " + std::to_string( member.type_id_ ) +
                          "  at path: " + pathToString( currentPath ) );
            return false;
        }
    }
    return true;
}

bool
ROS2DataSource::getArrayFromComplexType( struct ComplexArray &dest,
                                         ComplexDataTypeId type,
                                         const struct ComplexDataMessageFormat &format )
{
    auto complexTypeArray = format.mComplexTypeMap.find( type );
    if ( complexTypeArray == format.mComplexTypeMap.end() )
    {
        FWE_LOG_WARN( "Unknown complex type id: " + std::to_string( type ) );
        return false;
    }
    if ( complexTypeArray->second.type() != typeid( ComplexArray ) )
    {
        return false;
    }
    dest = boost::get<ComplexArray>( complexTypeArray->second );
    return true;
}

bool
ROS2DataSource::isTypeIdCompatiblePrimitiveToIntrospectionType( ComplexDataTypeId type,
                                                                const struct ComplexDataMessageFormat &format,
                                                                uint8_t introspectionType )
{
    auto complexType = format.mComplexTypeMap.find( type );
    if ( complexType == format.mComplexTypeMap.end() )
    {
        FWE_LOG_WARN( "Unknown complex type id: " + std::to_string( type ) );
        return false;
    }
    if ( complexType->second.type() != typeid( PrimitiveData ) )
    {
        FWE_LOG_WARN( "Introspection library expects primitive data" );
        return false;
    }
    auto primitiveData = boost::get<PrimitiveData>( complexType->second );
    return isCompatibleIntrospectionType( primitiveData.mPrimitiveType, introspectionType );
}

bool
ROS2DataSource::isCompatibleIntrospectionType( SignalType primitiveType, uint8_t introspectionType )
{

    switch ( introspectionType )
    {
    case rosidl_typesupport_introspection_cpp::ROS_TYPE_FLOAT:
        return primitiveType == SignalType::FLOAT;
    case rosidl_typesupport_introspection_cpp::ROS_TYPE_DOUBLE:
        return primitiveType == SignalType::DOUBLE;
    case rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT8:
        return primitiveType == SignalType::UINT8;
    case rosidl_typesupport_introspection_cpp::ROS_TYPE_INT8:
        return primitiveType == SignalType::INT8;
    case rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT16:
        return primitiveType == SignalType::UINT16;
    case rosidl_typesupport_introspection_cpp::ROS_TYPE_INT16:
        return primitiveType == SignalType::INT16;
    case rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT32:
        return primitiveType == SignalType::UINT32;
    case rosidl_typesupport_introspection_cpp::ROS_TYPE_INT32:
        return primitiveType == SignalType::INT32;
    case rosidl_typesupport_introspection_cpp::ROS_TYPE_UINT64:
        return primitiveType == SignalType::UINT64;
    case rosidl_typesupport_introspection_cpp::ROS_TYPE_INT64:
        return primitiveType == SignalType::INT64;
    case rosidl_typesupport_introspection_cpp::ROS_TYPE_BOOLEAN:
        return primitiveType == SignalType::BOOLEAN;
    case rosidl_typesupport_introspection_cpp::ROS_TYPE_OCTET:
        return primitiveType == SignalType::UINT8;
    case rosidl_typesupport_introspection_cpp::ROS_TYPE_CHAR:
        return primitiveType == SignalType::UINT8;
    default:
        return false;
    }
}

// TODO: optimize for arrays with fixed length members to skip all elements without paths
void
ROS2DataSource::recursiveExtractFromCDR( eprosima::fastcdr::Cdr &cdr,
                                         ComplexDataTypeId type,
                                         SignalPath &currentPath,
                                         uint32_t currentPathLength,
                                         uint32_t &currentPathIndex,
                                         const struct ComplexDataMessageFormat &format,
                                         Timestamp timestamp,
                                         CollectedSignalsGroup &collectedSignalsGroup )
{
    if ( currentPathLength >= MAX_TYPE_TREE_DEPTH )
    {
        FWE_LOG_ERROR( "Complex Tree to deep. Potentially circle in tree. Type id: " + std::to_string( type ) );
        return;
    }
    bool isPathMatched = false;
    uint32_t matchedPathIndex = currentPathIndex;
    if ( currentPathIndex < format.mSignalPaths.size() )
    {
        isPathMatched = format.mSignalPaths[currentPathIndex].equals( currentPath );
        if ( isPathMatched )
        {
            currentPathIndex++;
        }
        else
        {
            while ( ( currentPathIndex < format.mSignalPaths.size() ) &&
                    format.mSignalPaths[currentPathIndex].isSmaller( currentPath ) )
            {
                FWE_LOG_WARN( "Skipped path : " + pathToString( format.mSignalPaths[currentPathIndex].mSignalPath ) );
                currentPathIndex++;
            }
        }
    }

    if ( ( currentPathIndex >= format.mSignalPaths.size() ) && ( !isPathMatched ) )
    {
        // No path to match anymore so early exit
        return;
    }

    auto complexType = format.mComplexTypeMap.find( type );
    if ( complexType == format.mComplexTypeMap.end() )
    {
        FWE_LOG_WARN( "Unknown complex type id: " + std::to_string( type ) );
        return;
    }
    if ( complexType->second.type() == typeid( ComplexStruct ) )
    {
        auto complexStruct = boost::get<ComplexStruct>( complexType->second );
        currentPath.resize( currentPathLength + 1 );
        for ( unsigned int i = 0; i < complexStruct.mOrderedTypeIds.size(); i++ )
        {

            currentPath[currentPathLength] = i;
            recursiveExtractFromCDR( cdr,
                                     complexStruct.mOrderedTypeIds[i],
                                     currentPath,
                                     currentPathLength + 1,
                                     currentPathIndex,
                                     format,
                                     timestamp,
                                     collectedSignalsGroup );
            if ( ( currentPathIndex >= format.mSignalPaths.size() ) && ( !isPathMatched ) )
            {
                // No path to match anymore so early exit
                return;
            }
        }
        currentPath.resize( currentPathLength );
    }
    else if ( complexType->second.type() == typeid( ComplexArray ) )
    {
        auto complexArray = boost::get<ComplexArray>( complexType->second );
        currentPath.resize( currentPathLength + 1 );
        auto size = complexArray.mSize;
        if ( size <= 0 )
        {
            uint32_t length = 0; // CDR uses 32 bit for array sizes
            // coverity[misra_cpp_2008_rule_14_8_2_violation] - FastCDR header defines both template and type overloads
            cdr >> length;
            size = static_cast<decltype( size )>( length );
        }
        // TODO: consider capacity sized arrays
        for ( uint32_t i = 0; i < size; i++ )
        {
            currentPath[currentPathLength] = i;
            recursiveExtractFromCDR( cdr,
                                     complexArray.mRepeatedTypeId,
                                     currentPath,
                                     currentPathLength + 1,
                                     currentPathIndex,
                                     format,
                                     timestamp,
                                     collectedSignalsGroup );
            if ( ( currentPathIndex >= format.mSignalPaths.size() ) && ( !isPathMatched ) )
            {
                // No path to match anymore so early exit
                return;
            }
        }
        currentPath.resize( currentPathLength );
    }
    else if ( complexType->second.type() == typeid( PrimitiveData ) )
    {
        auto primitiveData = boost::get<PrimitiveData>( complexType->second );
        SignalID signalId = INVALID_SIGNAL_ID;
        if ( isPathMatched )
        {
            signalId = format.mSignalPaths[matchedPathIndex].mPartialSignalID;
        }
        readPrimitiveDataFromCDR( cdr, primitiveData, signalId, timestamp, collectedSignalsGroup );
    }
}

void
ROS2DataSource::processNewDecoderManifest()
{
    {
        std::lock_guard<std::mutex> lock( mDecoderDictMutex );
        mCurrentDict = mAvailableDict;
        mEventNewDecoderManifestAvailable = false;
    }

    if ( !mCurrentDict )
    {
        FWE_LOG_TRACE( "Decoder Dictionary with complex data is null so dont subscribe anything" );
        mWait.notify();
        return;
    }

    auto interface = mCurrentDict->complexMessageDecoderMethod.find( mConfig.mInterfaceId );
    if ( interface == mCurrentDict->complexMessageDecoderMethod.end() )
    {
        FWE_LOG_TRACE( "Decoder Dictionary has no information for Interface Id:'" + mConfig.mInterfaceId + "'" );
        mWait.notify();
        return;
    }
    for ( auto m : interface->second )
    {
        mMissingMessageIdsWaitingForSubscription.push_back( m.first );
    }
    FWE_LOG_TRACE( "Finished processing Decoder Dictionary for Interface Id:'" + mConfig.mInterfaceId + "'" );
    mWait.notify();
}

std::string
ROS2DataSource::pathToString( const SignalPath &path )
{
    std::string outPath = "[";
    for ( auto i : path )
    {
        outPath += std::to_string( i ) + ", ";
    }
    outPath += "]";
    return outPath;
}

void
ROS2DataSource::trySubscribe( std::vector<std::string> &toSubscribe )
{
    if ( toSubscribe.empty() )
    {
        return;
    }
    auto topicAndTypes = mNode->get_topic_names_and_types();

    bool allEmpty = true;
    for ( auto &s : toSubscribe )
    {
        if ( !s.empty() )
        {
            auto firstColon = s.find( COMPLEX_DATA_MESSAGE_ID_SEPARATOR );
            if ( firstColon != std::string::npos )
            {
                // Colon found in message id so treat messageID as 'topic:type'
                auto topic = s.substr( 0, firstColon );
                auto type = s.substr( firstColon + 1, s.length() );

                // create_generic_subscription
                if ( ( !sanityCheckType( s, type ) ) ||
                     mNode->subscribe( topic,
                                       type,
                                       // coverity[autosar_cpp14_a18_9_1_violation] std::bind is a standard way to use
                                       // subscribe for ROS2
                                       std::bind( &ROS2DataSource::topicCallback, this, _1, s ),
                                       mConfig.mSubscribeQueueLength ) )
                {
                    s = ""; // Set to empty after successful subscribe or failed sanity check
                }
            }
            else
            {
                // expand topic for example relative topic 'topic1' will get expanded to '/topic1'
                std::string nodeName = mNode->get_name() == nullptr ? "" : mNode->get_name();
                std::string namespaceName = mNode->get_namespace() == nullptr ? "" : mNode->get_namespace();
                auto expandedTopic = rclcpp::expand_topic_or_service_name( s, nodeName, namespaceName, false );
                for ( auto &found : topicAndTypes )
                {
                    if ( ( found.first == expandedTopic ) && ( found.second.size() == 1 ) )
                    {
                        auto type = found.second[0]; // take first type for this topic
                        // create_generic_subscription
                        if ( ( !sanityCheckType( s, type ) ) ||
                             mNode->subscribe( found.first,
                                               type,
                                               // coverity[autosar_cpp14_a18_9_1_violation] std::bind is a standard way
                                               // to use subscribe for ROS2
                                               std::bind( &ROS2DataSource::topicCallback, this, _1, s ),
                                               mConfig.mSubscribeQueueLength ) )
                        {
                            s = ""; // Set to empty after successful subscribe or failed sanity check
                        }
                        break;
                    }
                }
            }
        }
        // Check again as s could have been set to "" meanwhile
        if ( !s.empty() )
        {
            allEmpty = false;
        }
    }
    if ( allEmpty )
    {
        toSubscribe.clear();
    }
}

bool
ROS2DataSource::connect()
{
    return start();
}

bool
ROS2DataSource::disconnect()
{
    return stop();
}

bool
ROS2DataSource::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( mThreadMutex );
    // On multi core systems the shared variable mShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    mShouldStop.store( false );
    // Make sure the thread goes into sleep immediately to wait for
    // the manifest to be available
    mShouldSleep.store( true );
    if ( !mThread.create( doWork, this ) )
    {
        FWE_LOG_TRACE( "Thread failed to start" );
    }
    else
    {
        FWE_LOG_TRACE( "Thread started" );
        mThread.setThreadName( "fwVNROS2main" );
    }

    return mThread.isActive() && mThread.isValid();
}

bool
ROS2DataSource::stop()
{
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true );
    mWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    FWE_LOG_TRACE( "Thread stopped" );
    if ( mExecutorSpinThread.joinable() )
    {
        mROS2Executor->cancel();
        mExecutorSpinThread.join();
        mNode->deleteAllSubscriptions();
    }
    return !mThread.isActive();
}

bool
ROS2DataSource::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

bool
ROS2DataSource::shouldSleep() const
{
    return mShouldSleep.load( std::memory_order_relaxed );
}

ROS2DataSource::~ROS2DataSource()
{
    if ( isAlive() )
    {
        stop();
    }
    // coverity[cert_err50_cpp_violation] false positive - join is called to exit the previous thread
}

void
ROS2DataSource::doWork( void *data )
{
    ROS2DataSource *ros2DataSource = static_cast<ROS2DataSource *>( data );
    ros2DataSource->mCyclicTopicRetry.reset();
    while ( !ros2DataSource->shouldStop() )
    {
        if ( ros2DataSource->shouldSleep() )
        {
            // We either just started or there was a decoder manifest update that we can't use
            // We should sleep
            FWE_LOG_TRACE( "No valid decoding dictionary available ROS2 Data Source goes to sleep" );
            ros2DataSource->mWait.wait( Signal::WaitWithPredicate );
        }
        bool newDecoderDictionaryEvent = false;
        if ( ros2DataSource->mEventNewDecoderManifestAvailable )
        {
            // If thread is active cancel spinning and wait for it to finish
            if ( ros2DataSource->mExecutorSpinThread.joinable() )
            {
                ros2DataSource->mROS2Executor->cancel();
                ros2DataSource->mExecutorSpinThread.join();
                ros2DataSource->mNode->deleteAllSubscriptions();
            }
            ros2DataSource->processNewDecoderManifest();
            newDecoderDictionaryEvent = true;
        }
        if ( newDecoderDictionaryEvent ||
             ( ros2DataSource->mCyclicTopicRetry.getElapsedMs().count() > ROS2_RETRY_TOPIC_SUBSCRIBE_MS ) )
        {
            ros2DataSource->trySubscribe( ros2DataSource->mMissingMessageIdsWaitingForSubscription );
            ros2DataSource->mCyclicTopicRetry.reset();
        }
        if ( ros2DataSource->mCurrentDict )
        {
            // If thread not active start a new thread
            if ( !ros2DataSource->mExecutorSpinThread.joinable() )
            {
                // coverity[autosar_cpp14_a15_5_2_violation] false positive - join is called to exit the previous thread
                // coverity[cert_err50_cpp_violation] false positive - join is called to exit the previous thread
                gThreadCounter = 1;
                ros2DataSource->mExecutorSpinThread = std::thread( [ros2DataSource]() {
                    FWE_LOG_TRACE( "Executor start spinning" );
                    ros2DataSource->mROS2Executor->spin();
                    FWE_LOG_TRACE( "Executor stopped spinning" );
                } );
            }
        }
        ros2DataSource->mWait.wait( ROS2_DATA_SOURCE_INTERVAL_MS );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
