// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "IActiveDecoderDictionaryListener.h"
#include "IDecoderDictionary.h"
#include "MessageTypes.h"
#include "RawDataManager.h"
#include "Signal.h"
#include "SignalTypes.h"
#include "Thread.h"
#include "TimeTypes.h"
#include "Timer.h"
#include "VehicleDataSourceTypes.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fastcdr/Cdr.h>
#include <functional>
#include <json/json.h>
#include <memory>
#include <mutex>
#include <rclcpp/rclcpp.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>
#include <string>
#include <thread>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

enum class CompareToIntrospection
{
    ERROR_AND_FAIL_ON_DIFFERENCE,
    WARN_ON_DIFFERENCE,
    NO_CHECK // does no check so all differences will be ignored
};

class ROS2DataSourceConfig
{
public:
    std::string mInterfaceId;
    uint8_t mExecutorThreads;
    CompareToIntrospection mIntrospectionLibraryCompare;
    size_t mSubscribeQueueLength;

    /**
     * @brief parse from json member in static config
     * @param node pointing to the ros2Interface member
     * @param outputConfig ouput only valid when true is returned
     * @return true if successfully parsed, false otherwise
     */
    static bool parseFromJson( const Json::Value &node, ROS2DataSourceConfig &outputConfig );
};

class ROS2DataSourceNode : public rclcpp::Node
{
public:
    ROS2DataSourceNode();
    ~ROS2DataSourceNode() override = default;

    ROS2DataSourceNode( const ROS2DataSourceNode & ) = delete;
    ROS2DataSourceNode &operator=( const ROS2DataSourceNode & ) = delete;
    ROS2DataSourceNode( ROS2DataSourceNode && ) = delete;
    ROS2DataSourceNode &operator=( ROS2DataSourceNode && ) = delete;

    /**
     * @brief subscribe to ros2 topic, this function catches ros2 exceptions and returns false
     * @param topic the topic to subscribe
     * @param type a type that needs to be known to ros2 library at compile time
     * @param callback this function will get called from multiple thread every time a message arrives on the topic
     * @param queueLength defines over the QoS how many message will be queued before being replaced
     * @return true if subscription was successful, false otherwise
     */
    bool subscribe( std::string topic,
                    std::string type,
                    std::function<void( std::shared_ptr<rclcpp::SerializedMessage> )> callback,
                    size_t queueLength );

    /**
     * @brief deletes all subscriptions, after this the callbacks should not be called anymore
     */
    void
    deleteAllSubscriptions()
    {
        mSubscriptions.clear();
    }

private:
    std::vector<rclcpp::GenericSubscription::SharedPtr> mSubscriptions;
    rclcpp::CallbackGroup::SharedPtr mCallbackGroup;
};

class ROS2DataSource : public IActiveDecoderDictionaryListener
{
public:
    ROS2DataSource( ROS2DataSourceConfig config,
                    SignalBufferPtr signalBufferPtr,
                    std::shared_ptr<RawData::BufferManager> rawDataBufferManager = nullptr );
    ~ROS2DataSource() override;

    ROS2DataSource( const ROS2DataSource & ) = delete;
    ROS2DataSource &operator=( const ROS2DataSource & ) = delete;
    ROS2DataSource( ROS2DataSource && ) = delete;
    ROS2DataSource &operator=( ROS2DataSource && ) = delete;

    /**
     * @brief Start internal thread
     * @return true on success
     */
    bool connect();

    /**
     * @brief Stop internal thread
     * @return true on success
     */
    bool disconnect();

    /**
     * @brief check status
     * @return true on if internal thread still running
     */
    bool
    isAlive()
    {
        return mThread.isValid() && mThread.isActive();
    }

    /**
     * @brief From IActiveDecoderDictionaryListener get called on dictionary updates
     * @param dictionary new dictionary with the topics to subscribe to and information how to decode
     * @param networkProtocol only COMPLEX_DATA will be accepted
     */
    void onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                     VehicleDataSourceProtocol networkProtocol ) override;

private:
    /**
     * @brief main function of the thread that only return when the thread should stop
     * @param data points to ROS2DataSource
     */
    static void doWork( void *data );

    /**
     * @brief does the thread needs to stop
     * @return true if it should stop
     */
    bool shouldStop() const;

    /**
     * @brief should the thread sleep
     * @return true if it should sleep
     */
    bool shouldSleep() const;

    /**
     * @brief start the internal thread with the doWork function
     * @return true if successful
     */
    bool start();

    /**
     * @brief stop the internal thread
     * @return true if successfully stopped
     */
    bool stop();

    /**
     * @brief Try to subscribe to the topics by checking available topics of ROS2.
     * @param toSubscribe will be modified by setting after successful subscription the element to "" and clear the
     * vector if all elements are ""
     */
    void trySubscribe( std::vector<std::string> &toSubscribe );

    /**
     * @brief Sanity check updated dictionary and fill mMissingMessageIdsWaitingForSubscription
     */
    void processNewDecoderManifest();

    /**
     * @brief Deserializes the cdr using decoding information from cloud
     * @param cdr pointing to the current position of the deserialization
     * @param type the current type that comes next in the cdr
     * @param currentPath the current signal path that will be used to check if the deserialized primitive data should
     * be pushed to the signal buffer
     * @param currentPathLength current depth in the tree. The depth is limited to a small size and the recursion will
     * be aborted if over a certain threshold
     * @param currentPathIndex points to one element in the sorted format.mSignalPaths that should be used next
     * @param format has the deserialization information from cloud and the information which primitive data paths
     * should be pushed to the signal buffer
     * @param timestamp the timestamp to use to push signals
     * @param collectedSignalsGroup vector of collected signals to fill out
     */
    void recursiveExtractFromCDR( eprosima::fastcdr::Cdr &cdr,
                                  ComplexDataTypeId type,
                                  SignalPath &currentPath,
                                  uint32_t currentPathLength,
                                  uint32_t &currentPathIndex,
                                  const struct ComplexDataMessageFormat &format,
                                  Timestamp timestamp,
                                  CollectedSignalsGroup &collectedSignalsGroup );

    /**
     * @brief Reads a single primitive data (up to 8 bytes) from cdr and publishes it if needed to the signal buffer.
     * @param cdr point at the start of the primitive data to deserialize
     * @param format the scaling and type information form the cloud
     * @param signalId if this is a valid signal id the read data will be sent to the signal buffer
     * @param timestamp the timestamp to use when sending a valid signal to the signal buffer
     * @param collectedSignalsGroup vector of collected signals to fill out
     */
    static void readPrimitiveDataFromCDR( eprosima::fastcdr::Cdr &cdr,
                                          struct PrimitiveData &format,
                                          SignalID signalId,
                                          Timestamp timestamp,
                                          CollectedSignalsGroup &collectedSignalsGroup );

    /**
     * @brief Will be called from multiple executor threads in parallel
     * @param msg the raw serialized message that will be interpreted as CDR
     * @param dictionaryMessageId is the message id that can be used in to to read the format from mCurrentDict
     */
    void topicCallback( std::shared_ptr<rclcpp::SerializedMessage> msg, std::string dictionaryMessageId );

    /**
     * @brief for debugging purpose return a string in the form [ 1, 2, 3]
     * @param path the path to convert to a string
     * @return the generated string
     */
    static std::string pathToString( const SignalPath &path );

    /**
     * @brief check if decoding information from cloud is the same as from ros2 introspection library compile time
     * @param dictionaryMessageId is the message id that can be used in to to read the format from mCurrentDict
     * @param type this type will be used to get the type information from the ros2 introspection library
     * @return depending on mConfig.mIntrospectionLibraryCompare true if messages of this type should be processed
     */
    bool sanityCheckType( std::string dictionaryMessageId, std::string type );

    /**
     * @brief compares cloud information recursively with type information from introspection library
     * @param startingOffset in bytes normally 0
     * @param messageMembers from introspection library points to all struct member
     * @param format from the cloud
     * @param type the cloud type that should be compares to messageMembers
     * @param currentPath for better debug messages and checking maximal depth
     * @return true if the type information from cloud and introspection library are the same, false if not or something
     * went wrong while comparing
     */
    bool recursiveCompareToIntrospectionTypeTree(
        const uint32_t &startingOffset,
        const rosidl_typesupport_introspection_cpp::MessageMembers *messageMembers,
        const struct ComplexDataMessageFormat &format,
        ComplexDataTypeId type,
        SignalPath &currentPath );

    /**
     * @brief sanity checking and setting up introspection library before calling
     * recursiveCompareToIntrospectionTypeTree
     * @param dictionaryMessageId is the message id that can be used in to to read the format from mCurrentDict
     * @param type ros2 type name to compare
     * @return true if the type information from cloud and introspection library are the same, false if not or something
     * went wrong while comparing
     */
    bool compareToIntrospectionTypeTree( std::string dictionaryMessageId, std::string type );

    /**
     * @brief check if two basic types are the same, this is necessary as ROS2 and cloud dictionary use different enums
     * to describe the types.
     * @param primitiveType cloud type information
     * @param introspectionType cloud type information
     * @return true if both are compatible/same types.
     */
    static bool isCompatibleIntrospectionType( SignalType primitiveType, uint8_t introspectionType );

    /**
     * @brief Sanity checks and then calls isCompatibleIntrospectionType
     * @param type that can be used to access format.mComplexTypeMap
     * @param format from cloud
     * @param introspectionType from ros2 introspection library
     * @return true if both are compatible/same types.
     */
    static bool isTypeIdCompatiblePrimitiveToIntrospectionType( ComplexDataTypeId type,
                                                                const struct ComplexDataMessageFormat &format,
                                                                uint8_t introspectionType );

    /**
     * @brief Sanity check and if successfully set dest to a ComplexArray
     * @param dest output parameter so this will be set if the function returns true
     * @param type cloud type id
     * @param format from the cloud
     * @return true if sanity check successful
     */
    static bool getArrayFromComplexType( struct ComplexArray &dest,
                                         ComplexDataTypeId type,
                                         const struct ComplexDataMessageFormat &format );

    /**
     * @brief compare array size from ros2 introspection library to decoding information from cloud
     * @param  member ros2 introspection library information
     * @param complexArray cloud information
     * @return true if the arrays have the same size, or both have dynamic size. False otherwise
     */
    static bool compareArraySizeToIntrospectionType( const rosidl_typesupport_introspection_cpp::MessageMember &member,
                                                     ComplexArray &complexArray );

    /**
     * @brief string are represent in cloud dictionary as dynamically sized uint8/uint16 arrays so this check
     * information from cloud and introspection library are compatible
     * @param type that can be used to access format.mComplexTypeMap
     * @param member ros2 introspection library information about the string type
     * @param format cloud decoding information about the array
     * @return true if the types are compatible
     */
    static bool compareStringToIntrospectionType( ComplexDataTypeId type,
                                                  const rosidl_typesupport_introspection_cpp::MessageMember &member,
                                                  const struct ComplexDataMessageFormat &format );

    Thread mThread;
    ROS2DataSourceConfig mConfig;

    std::atomic<bool> mShouldStop{ false };
    std::atomic<bool> mShouldSleep{ false };
    mutable std::mutex mThreadMutex;
    Signal mWait;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    SignalBufferPtr mSignalBufferPtr;

    std::mutex mDecoderDictMutex;
    std::atomic<bool> mEventNewDecoderManifestAvailable{ false };

    std::shared_ptr<const ComplexDataDecoderDictionary> mCurrentDict;
    std::shared_ptr<const ComplexDataDecoderDictionary> mAvailableDict;
    std::shared_ptr<ROS2DataSourceNode> mNode;

    std::unique_ptr<rclcpp::executors::MultiThreadedExecutor> mROS2Executor;
    std::thread mExecutorSpinThread;

    std::vector<std::string> mMissingMessageIdsWaitingForSubscription;
    Timer mCyclicTopicRetry;
    std::shared_ptr<RawData::BufferManager> mRawBufferManager;
};

} // namespace IoTFleetWise
} // namespace Aws
