// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "collection_schemes.pb.h"
#include <climits>
#include <cstdint>
#include <memory>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

struct FetchInformation
{
    /**
     * @brief   ID of signal to be fetched.
     */
    SignalID signalID{ 0U };

    /**
     * @brief   AST root node to specify condition to fetch data (nullptr for time-based fetching).
     */
    ExpressionNode *condition{ nullptr };

    /**
     * @brief   Specify if data fetching should occur only on rising edge of condition (false to true).
     */
    bool triggerOnlyOnRisingEdge{ false };

    /**
     * @brief   Max number of action executions (per action?) can occur per interval (refer executionIntervalMs).
     */
    uint64_t maxExecutionPerInterval{ 0U };

    /**
     * @brief   Action execution period - between consecutive action executions (per action?).
     */
    uint64_t executionPeriodMs{ 0U };

    /**
     * @brief   Action execution interval (per action?) (refer maxExecutionPerInterval).
     */
    uint64_t executionIntervalMs{ 0U };

    /**
     * @brief   Actions to be executed on fetching.
     */
    std::vector<ExpressionNode *> actions;
};

struct SignalCollectionInfo
{
    /**
     * @brief Unique Signal ID provided by Cloud or Partial Signal Id generated internally see
     * INTERNAL_SIGNAL_ID_BITMASK
     */
    SignalID signalID{ 0 };

    /**
     * @brief The size of the ring buffer that will contain the data points for this signal
     */
    uint32_t sampleBufferSize{ 0 };

    /**
     * @brief Minimum time period in milliseconds that must elapse between collecting samples. Samples
     * arriving faster than this period will be dropped. A value of 0 will collect samples as fast
     * as they arrive.
     */
    uint32_t minimumSampleIntervalMs{ 0 };

    /**
     * @brief The size of a fixed window in milliseconds which will be used by aggregate condition
     * functions to calculate min/max/avg etc.
     */
    uint32_t fixedWindowPeriod{ 0 };

    /**
     * @brief When true, this signal will not be collected and sent to cloud. It will only be used in the
     * condition logic with its associated fixed_window_period_ms. Default is false.
     */
    bool isConditionOnlySignal{ false };

#ifdef FWE_FEATURE_STORE_AND_FORWARD
    /**
     * @brief The Id of the partition where this signal should be stored.
     * This Id will be used to index into the partition configuration array.
     */
    uint32_t dataPartitionId{ 0 };
#endif
};

enum class ExpressionNodeType
{
    FLOAT = 0,
    SIGNAL, // Node_Signal_ID
    BOOLEAN,
    STRING,
    OPERATOR_SMALLER, // NodeOperator
    OPERATOR_BIGGER,
    OPERATOR_SMALLER_EQUAL,
    OPERATOR_BIGGER_EQUAL,
    OPERATOR_EQUAL,
    OPERATOR_NOT_EQUAL,
    OPERATOR_LOGICAL_AND,
    OPERATOR_LOGICAL_OR,
    OPERATOR_LOGICAL_NOT,
    OPERATOR_ARITHMETIC_PLUS,
    OPERATOR_ARITHMETIC_MINUS,
    OPERATOR_ARITHMETIC_MULTIPLY,
    OPERATOR_ARITHMETIC_DIVIDE,
    WINDOW_FUNCTION, // NodeFunction
    CUSTOM_FUNCTION,
    IS_NULL_FUNCTION,
    NONE
};

enum class WindowFunction
{
    LAST_FIXED_WINDOW_AVG = 0,
    PREV_LAST_FIXED_WINDOW_AVG,
    LAST_FIXED_WINDOW_MIN,
    PREV_LAST_FIXED_WINDOW_MIN,
    LAST_FIXED_WINDOW_MAX,
    PREV_LAST_FIXED_WINDOW_MAX,
    NONE
};

struct ExpressionFunction
{
    /**
     * @brief   Specify which window function to be used (in case ExpressionNodeType is WINDOW_FUNCTION).
     */
    WindowFunction windowFunction{ WindowFunction::NONE };

    /**
     * @brief   Specify custom function name and params (in case ExpressionNodeType is CUSTOM_FUNCTION).
     */
    std::string customFunctionName;
    std::vector<ExpressionNode *> customFunctionParams;
    CustomFunctionInvocationID customFunctionInvocationId{};
};

// Instead of c style struct an object oriented interface could be implemented with different
// Implementations like LeafNodes like Signal or Float all inheriting from expressionNode.
struct ExpressionNode
{
    /**
     * @brief Indicated the action of the node if its an operator or a variable
     */
    ExpressionNodeType nodeType{ ExpressionNodeType::FLOAT };

    /**
     * @brief AST Construction Left Side
     */
    ExpressionNode *left{ nullptr };

    /**
     * @brief AST Construction Right Side
     */
    ExpressionNode *right{ nullptr };

    /**
     * @brief Node Value is a floating point
     */
    double floatingValue{ 0 };

    /**
     * @brief Node Value is boolean
     */
    bool booleanValue{ false };

    /**
     * @brief Node Value is string
     */
    std::string stringValue;

    /**
     * @brief Unique Signal ID provided by Cloud
     */
    SignalID signalID{ 0 };

    /**
     * @brief Function operation on the nodes
     */
    ExpressionFunction function;
};

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
struct S3UploadMetadata
{
    /**
     * @brief Bucket name for S3 upload
     */
    std::string bucketName;

    /**
     * @brief Prefix to add to every upload object
     */
    std::string prefix;

    /**
     * @brief Region of the S3 bucket for upload
     */
    std::string region;

    /**
     * @brief Account ID of bucket owner
     */
    std::string bucketOwner;

public:
    /**
     * @brief Overload of the == operator for S3UploadMetadata
     * @param other Other S3UploadMetadata object to compare
     * @return true if ==, false otherwise
     */
    bool
    operator==( const S3UploadMetadata &other ) const
    {
        return ( bucketName == other.bucketName ) && ( prefix == other.prefix ) && ( region == other.region ) &&
               ( bucketOwner == other.bucketOwner );
    }

    /**
     * @brief Overloaded != operator for S3UploadMetadata.
     * @param other Other S3UploadMetadata to compare to.
     * @return True if !=, false otherwise.
     */
    bool
    operator!=( const S3UploadMetadata &other ) const
    {
        return !( *this == other );
    }
};
#endif

#ifdef FWE_FEATURE_STORE_AND_FORWARD
struct StorageOptions
{
    /**
     * @brief The total amount of space allocated to this campaign including all overhead. uint64 can support up to 8GB.
     */
    uint64_t maximumSizeInBytes{ 0 };

    /**
     * @brief Specifies where the data should be stored withing the device.
     * Implementation is defined by the user who integrates FWE with their filesystem library.
     */
    std::string storageLocation;

    /**
     * @brief The minimum amount of time to keep data on disk after it is collected.
     * When this TTL expires, data may be deleted, but it is not guaranteed to be deleted immediately after expiry.
     * Can hold TTL more than 132 years.
     */
    uint32_t minimumTimeToLiveInSeconds{ 0 };
};

struct UploadOptions
{
    /**
     * @brief Root condition node for the Abstract Syntax Tree.
     */
    ExpressionNode *conditionTree{ nullptr };
};

struct PartitionConfiguration
{
    /**
     * @brief Optional Store and Forward storage options.
     * If not specified, data in this partition will be uploaded in realtime.
     */
    StorageOptions storageOptions;

    /**
     * @brief Store and Forward upload options defines when the stored data may be uploaded.
     * It is only used when spoolingMode=TO_DISK. May be non-null only when spoolingMode=TO_DISK.
     */
    UploadOptions uploadOptions;
};
#endif

/**
 * @brief ICollectionScheme is used to exchange CollectionScheme between components
 *
 * This interface exist to abstract from the underlying data format which means no protobuf
 * or jsoncpp headers etc. are exposed through this API.
 * For example if protobuf is used this interface header abstract from this and presents another
 * view to the data. This means in most scenarios the underlying type is changed (i.e protobuf schemas)
 * also this interface needs to be changed.
 * This abstraction increases overhead compared passing the raw low level format but increases
 * decoupling. If this is identified to be a bottleneck it will be replaced by the low level data
 * view.
 *
 */
class ICollectionScheme
{
public:
    /**
     * @brief Signals_t is vector of IDs provided by Cloud that is unique across all signals found in the vehicle
     * regardless of the network bus.
     */
    using Signals_t = std::vector<SignalCollectionInfo>;
    const Signals_t INVALID_COLLECTED_SIGNALS = std::vector<SignalCollectionInfo>();

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    /**
     * @brief Complex signals can have multiple PartialSignalID. As complex signals are represented as a tree
     *          the SignalPath give the path inside the SignalID that contains the PartialSignalID.
     */
    using PartialSignalIDLookup = std::unordered_map<PartialSignalID, std::pair<SignalID, SignalPath>>;
    const PartialSignalIDLookup INVALID_PARTIAL_SIGNAL_ID_LOOKUP = PartialSignalIDLookup();

    const S3UploadMetadata INVALID_S3_UPLOAD_METADATA = S3UploadMetadata();
#endif

#ifdef FWE_FEATURE_STORE_AND_FORWARD
    /**
     * @brief Configuration of store and forward campaign.
     */
    using StoreAndForwardConfig = std::vector<PartitionConfiguration>;
    const StoreAndForwardConfig INVALID_STORE_AND_FORWARD_CONFIG = std::vector<PartitionConfiguration>();
#endif

    /**
     * @brief ExpressionNode_t is a vector that represents the AST Expression Tree per collectionScheme provided.
     */
    using ExpressionNode_t = std::vector<ExpressionNode>;
    const ExpressionNode_t INVALID_EXPRESSION_NODES = std::vector<ExpressionNode>();

    /**
     * @brief   FetchInformation_t is a vector that represents all fetch informations in the CollectionScheme.
     */
    using FetchInformation_t = std::vector<FetchInformation>;
    const FetchInformation_t INVALID_FETCH_INFORMATIONS = std::vector<FetchInformation>();

    const uint64_t INVALID_COLLECTION_SCHEME_START_TIME = std::numeric_limits<uint64_t>::max();
    const uint64_t INVALID_COLLECTION_SCHEME_EXPIRY_TIME = std::numeric_limits<uint64_t>::max();
    const uint32_t INVALID_MINIMUM_PUBLISH_TIME = std::numeric_limits<uint32_t>::max();
    const uint32_t INVALID_AFTER_TRIGGER_DURATION = std::numeric_limits<uint32_t>::max();
    const uint32_t INVALID_PRIORITY_LEVEL = std::numeric_limits<uint32_t>::max();
    const SyncID INVALID_COLLECTION_SCHEME_ID = SyncID();
    const SyncID INVALID_DECODER_MANIFEST_ID = SyncID();
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    const std::string INVALID_CAMPAIGN_ARN = std::string();
#endif

    virtual bool operator==( const ICollectionScheme &other ) const = 0;

    virtual bool operator!=( const ICollectionScheme &other ) const = 0;

    /**
     * @brief indicates if the decoder manifest is prepared to be used for example by calling getters
     *
     * @return true if ready and false if not ready then build function must be called first
     * */
    virtual bool isReady() const = 0;

    /**
     * @brief Build internal structures from raw input so lazy initialization is possible
     *
     * @return true if build succeeded false if the collectionScheme is corrupted and can not be used
     * */
    virtual bool build() = 0;

    /**
     * @brief Get the unique collectionScheme ID in the form of an Amazon Resource Name
     *
     * @return A string containing the unique ID of this collectionScheme.
     */
    virtual const SyncID &getCollectionSchemeID() const = 0;

#ifdef FWE_FEATURE_STORE_AND_FORWARD
    /**
     * @brief Get the Amazon Resource Name of the campaign this collectionScheme is part of
     *
     * @return A string containing the Campaign Arn of this collectionScheme
     */
    virtual const std::string &getCampaignArn() const = 0;
#endif

    /**
     * @brief Get the associated Decoder Manifest ID of this collectionScheme
     *
     * @return A string containing the unique Decoder Manifest of this collectionScheme.
     */
    virtual const SyncID &getDecoderManifestID() const = 0;

    /**
     * @brief Get Activation timestamp of the collectionScheme
     *
     * @return time since epoch in milliseconds
     */
    virtual uint64_t getStartTime() const = 0;

    /**
     * @brief Get Deactivation timestamp of the collectionScheme
     *
     * @return time since epoch in milliseconds
     */
    virtual uint64_t getExpiryTime() const = 0;

    /**
     * @brief Limit the rate at which a  collection scheme triggers
     *
     * If a  collection scheme condition is true but the last time the condition was triggered
     * is less than this interval ago the trigger will be ignored
     *
     * @return interval in milliseconds
     */
    virtual uint32_t getMinimumPublishIntervalMs() const = 0;

    /**
     * @brief Limit the rate at which a  collection scheme triggers
     *
     * If a  collection scheme condition is true but the last time the condition was triggered
     * is less than this interval ago the trigger will be ignored
     *
     * @return interval in milliseconds
     */
    virtual uint32_t getAfterDurationMs() const = 0;

    /**
     * @brief Should all active DTCs be included when sending out the collectionScheme
     */
    virtual bool isActiveDTCsIncluded() const = 0;

    /**
     * @brief Should the collectionScheme trigger only once when the collectionScheme condition changes from false to
     * true
     *
     * @return true: trigger on change from false to true, false: trigger as long condition is true
     */
    virtual bool isTriggerOnlyOnRisingEdge() const = 0;

    /**
     * @brief get priority which is used by the offboard offboardconnectivity module
     * A smaller value collectionScheme has more priority (takes precedence) over larger values
     */
    virtual uint32_t getPriority() const = 0;

    /**
     * @brief Is it needed to persist the data
     */
    virtual bool isPersistNeeded() const = 0;

    /**
     * @brief Is it needed to compress data before it is sent
     */
    virtual bool isCompressionNeeded() const = 0;

    /**
     * @brief Returns the condition to trigger the collectionScheme
     *
     * @return the returned pointer is either nullptr or valid as long as ICollectionScheme is alive
     */
    virtual const ExpressionNode *getCondition() const = 0;

    /**
     * @brief Returns all signals that this collectionScheme wants to collect
     *
     * @return if not ready an empty vector
     */
    virtual const Signals_t &getCollectSignals() const = 0;

    /**
     * @brief Returns all of the Expression Nodes
     *
     * @return if not ready an empty vector
     */
    virtual const ExpressionNode_t &getAllExpressionNodes() const = 0;

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    /**
     * @brief Returns a lookup table to translate internal PartialSignalId to the signal id and path
     *         provided by cloud
     *
     * @return if not ready an empty unordered_map
     */
    virtual const PartialSignalIDLookup &getPartialSignalIdToSignalPathLookupTable() const = 0;

    /**
     * @brief Returns S3 Upload Metadata
     *
     * @return if not ready empty struct
     */
    virtual S3UploadMetadata getS3UploadMetadata() const = 0;
#endif

#ifdef FWE_FEATURE_STORE_AND_FORWARD
    /**
     * @brief Returns store and forward campaign configuration
     *
     * @return if not ready an empty vector
     */
    virtual const StoreAndForwardConfig &getStoreAndForwardConfiguration() const = 0;
#endif

    /**
     * @brief   Get all expression nodes used for fetching conditions. Return empty vector if it is not ready.
     */
    virtual const ExpressionNode_t &getAllExpressionNodesForFetchCondition() const = 0;

    /**
     * @brief   Get all expression nodes used for fetching actions. Return empty vector if it is not ready.
     */
    virtual const ExpressionNode_t &getAllExpressionNodesForFetchAction() const = 0;

    /**
     * @brief   Get all fetch informations. Return empty vector if it is not ready.
     */
    virtual const FetchInformation_t &getAllFetchInformations() const = 0;

    virtual ~ICollectionScheme() = default;
};

} // namespace IoTFleetWise
} // namespace Aws
