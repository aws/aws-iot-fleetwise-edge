/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#pragma once

#include "IDecoderManifest.h"
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
namespace DataManagement
{

// Camera Data collection related types
enum class ImageCollectionType
{
    TIME_BASED,
    FRAME_BASED,
    NONE
};

struct ImageCollectionInfo
{
    ImageCollectionInfo()
        : deviceID( 0 )
        , imageFormat( 0 )
        , collectionType( ImageCollectionType::NONE )
        , beforeDurationMs( 0 )
    {
    }
    ImageCollectionInfo( ImageDeviceID id, uint32_t format, ImageCollectionType cType, uint32_t beforeDurationMs )
        : deviceID( id )
        , imageFormat( format )
        , collectionType( cType )
        , beforeDurationMs( beforeDurationMs )
    {
    }
    ImageDeviceID deviceID;             // Unique Identifier of the image sensor in the system
    uint32_t imageFormat;               // Image format expected from the System e.g. PNG.
                                        // Exact ids of the type will end up in an enum in the
                                        // CollectionScheme decoder.
    ImageCollectionType collectionType; // Whether Images are collected from the device based on
                                        // a timewindow or based on frame number.
    uint32_t beforeDurationMs;          // Amount of time in ms to be collected from the
                                        // image sensor buffer. This time is counted before a
                                        // condition is met and thus can be used to create
                                        // a time interval before and after a certain condition is
                                        // met in the system.
};

struct SignalCollectionInfo
{
    SignalCollectionInfo()
        : signalID( 0 )
        , sampleBufferSize( 0 )
        , minimumSampleIntervalMs( 0 )
        , fixedWindowPeriod( 0 )
        , isConditionOnlySignal( false )
    {
    }
    /**
     * @brief Unique Signal ID provided by Cloud
     */
    SignalID signalID;

    /**
     * @brief The size of the ring buffer that will contain the data points for this signal
     */
    uint32_t sampleBufferSize;

    /**
     * @brief Minimum time period in milliseconds that must elapse between collecting samples. Samples
     * arriving faster than this period will be dropped. A value of 0 will collect samples as fast
     * as they arrive.
     */
    uint32_t minimumSampleIntervalMs;

    /**
     * @brief The size of a fixed window in milliseconds which will be used by aggregate condition
     * functions to calculate min/max/avg etc.
     */
    uint32_t fixedWindowPeriod;

    /**
     * @brief When true, this signal will not be collected and sent to cloud. It will only be used in the
     * condition logic with its associated fixed_window_period_ms. Default is false.
     */
    bool isConditionOnlySignal;
};

struct CanFrameCollectionInfo
{
    CanFrameCollectionInfo()
        : frameID( 0 )
        , interfaceID( INVALID_CAN_INTERFACE_ID )
        , sampleBufferSize( 0 )
        , minimumSampleIntervalMs( 0 )
    {
    }

    /**
     * @brief CAN Message ID to collect. This Raw CAN message will be collected. Whatever number of bytes
     * present on the bus for this message ID will be collected.
     */
    CANRawFrameID frameID;

    /**
     * @brief The Interface Id specified by the Decoder Manifest. This will contain the physical channel
     * id of the hardware CAN Bus the frame is present on.
     */
    CANInterfaceID interfaceID;

    /**
     * @brief Ring buffer size used to store these sampled CAN frames. One CAN Frame is a sample.
     */
    uint32_t sampleBufferSize;

    /**
     * @brief Minimum time period in milliseconds that must elapse between collecting samples. Samples
     * arriving faster than this period will be dropped. A value of 0 will collect samples as fast
     * as they arrive.
     */
    uint32_t minimumSampleIntervalMs;
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
    GeohashFunction()
        : latitudeSignalID( 0 )
        , longitudeSignalID( 0 )
        , precision( 0 )
        , gpsUnitType( GPSUnitType::DECIMAL_DEGREE )
    {
    }
    enum class GPSUnitType
    {
        DECIMAL_DEGREE = 0,
        MICROARCSECOND,
        MILLIARCSECOND,
        ARCSECOND
    };
    SignalID latitudeSignalID;
    SignalID longitudeSignalID;
    uint8_t precision;
    GPSUnitType gpsUnitType;
};

struct ExpressionFunction
{
    ExpressionFunction()
        : geohashFunction()
        , windowFunction( WindowFunction::NONE )
    {
    }
    GeohashFunction geohashFunction;
    WindowFunction windowFunction;
};

// Instead of c style struct an object oriented interface could be implemented with different
// Implementations like LeafNodes like Signal or Float all inheriting from expressionNode.
struct ExpressionNode
{
    ExpressionNode()
        : nodeType( ExpressionNodeType::FLOAT )
        , left( nullptr )
        , right( nullptr )
        , floatingValue( 0 )
        , booleanValue( false )
        , signalID( 0 )
    {
    }
    /**
     * @brief Indicated the action of the node if its an operator or a variable
     */
    ExpressionNodeType nodeType;

    /**
     * @brief AST Construction Left Side
     */
    struct ExpressionNode *left;

    /**
     * @brief AST Construction Right Side
     */
    struct ExpressionNode *right;

    /**
     * @brief Node Value is a floating point
     */
    double floatingValue;

    /**
     * @brief Node Value is boolean
     */
    bool booleanValue;

    /**
     * @brief Unique Signal ID provided by Cloud
     */
    SignalID signalID;

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
     * @brief ImagesDataType is vector representing metadata for Image Capture.
     */
    using ImagesDataType = std::vector<ImageCollectionInfo>;
    const ImagesDataType INVALID_IMAGE_DATA = std::vector<ImageCollectionInfo>();

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
    virtual const struct ExpressionNode *getCondition() const = 0;

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
     * @brief Returns all Image Capture settings for this collectionScheme
     *
     * @return if not ready an empty vector
     */
    virtual const ImagesDataType &getImageCaptureData() const = 0;

    /**
     * @brief Returns all of the Expression Nodes
     *
     * @return if not ready an empty vector
     */
    virtual const ExpressionNode_t &getAllExpressionNodes() const = 0;

    virtual ~ICollectionScheme() = 0;
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
