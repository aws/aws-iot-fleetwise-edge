// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CollectionInspectionAPITypes.h"
#include "SignalTypes.h"
#include "collection_schemes.pb.h"
#include <climits>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

struct SignalCollectionInfo
{
    /**
     * @brief Unique Signal ID provided by Cloud
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
};

struct CanFrameCollectionInfo
{

    /**
     * @brief CAN Message ID to collect. This Raw CAN message will be collected. Whatever number of bytes
     * present on the bus for this message ID will be collected.
     */
    CANRawFrameID frameID{ 0 };

    /**
     * @brief The Interface Id specified by the Decoder Manifest. This will contain the physical channel
     * id of the hardware CAN Bus the frame is present on.
     */
    CANInterfaceID interfaceID{ INVALID_CAN_INTERFACE_ID };

    /**
     * @brief Ring buffer size used to store these sampled CAN frames. One CAN Frame is a sample.
     */
    uint32_t sampleBufferSize{ 0 };

    /**
     * @brief Minimum time period in milliseconds that must elapse between collecting samples. Samples
     * arriving faster than this period will be dropped. A value of 0 will collect samples as fast
     * as they arrive.
     */
    uint32_t minimumSampleIntervalMs{ 0 };
};

enum class ExpressionNodeType
{
    FLOAT = 0,
    SIGNAL,          // Node_Signal_ID
    WINDOWFUNCTION,  // NodeFunction
    GEOHASHFUNCTION, // GEOHASH
    BOOLEAN,
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
    OPERATOR_ARITHMETIC_DIVIDE
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

struct GeohashFunction
{
    enum class GPSUnitType
    {
        DECIMAL_DEGREE = 0,
        MICROARCSECOND,
        MILLIARCSECOND,
        ARCSECOND,
        MAX
    };
    SignalID latitudeSignalID{ 0 };
    SignalID longitudeSignalID{ 0 };
    uint8_t precision{ 0 };
    GPSUnitType gpsUnitType{ GPSUnitType::DECIMAL_DEGREE };
};

struct ExpressionFunction
{
    GeohashFunction geohashFunction;
    WindowFunction windowFunction{ WindowFunction::NONE };
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
     * @brief Unique Signal ID provided by Cloud
     */
    SignalID signalID{ 0 };

    /**
     * @brief Function operation on the nodes
     */
    ExpressionFunction function;
};

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

    /**
     * @brief RawCanFrames_t is vector representing metadata for CAN collected DATA.
     */
    using RawCanFrames_t = std::vector<CanFrameCollectionInfo>;
    const RawCanFrames_t INVALID_RAW_CAN_COLLECTED_SIGNALS = std::vector<CanFrameCollectionInfo>();

    /**
     * @brief Signals_t is a vector that represents the AST Expression Tree per collectionScheme provided.
     */
    using ExpressionNode_t = std::vector<ExpressionNode>;
    const ExpressionNode_t INVALID_EXPRESSION_NODE = std::vector<ExpressionNode>();

    const uint64_t INVALID_COLLECTION_SCHEME_START_TIME = std::numeric_limits<uint64_t>::max();
    const uint64_t INVALID_COLLECTION_SCHEME_EXPIRY_TIME = std::numeric_limits<uint64_t>::max();
    const uint32_t INVALID_MINIMUM_PUBLISH_TIME = std::numeric_limits<uint32_t>::max();
    const uint32_t INVALID_AFTER_TRIGGER_DURATION = std::numeric_limits<uint32_t>::max();
    const uint32_t INVALID_PRIORITY_LEVEL = std::numeric_limits<uint32_t>::max();
    const double INVALID_PROBABILITY_TO_SEND = 0.0;
    const std::string INVALID_COLLECTION_SCHEME_ID = std::string();
    const std::string INVALID_DECODER_MANIFEST_ID = std::string();

    const double DEFAULT_PROBABILITY_TO_SEND = 1.0;

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
    virtual const std::string &getCollectionSchemeID() const = 0;

    /**
     * @brief Get the associated Decoder Manifest ID of this collectionScheme
     *
     * @return A string containing the unique Decoder Manifest of this collectionScheme.
     */
    virtual const std::string &getDecoderManifestID() const = 0;

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
     * @brief At which probability should the triggered and collected data be sent out
     *
     * @return double 0: send never, send always
     */
    virtual double getProbabilityToSend() const = 0;

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
     * @brief Returns all raw can frames that this collectionScheme wants to collect
     *
     * @return if not ready an empty vector
     */
    virtual const RawCanFrames_t &getCollectRawCanFrames() const = 0;

    /**
     * @brief Returns all of the Expression Nodes
     *
     * @return if not ready an empty vector
     */
    virtual const ExpressionNode_t &getAllExpressionNodes() const = 0;

    virtual ~ICollectionScheme() = default;
};

} // namespace IoTFleetWise
} // namespace Aws
