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

#include "GeohashFunctionNode.h"
#include "ICollectionScheme.h"
#include "IDecoderManifest.h"
// multi producer queue:
#include <boost/lockfree/queue.hpp>
// single producer queue:
#include <boost/lockfree/spsc_queue.hpp>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{
using namespace Aws::IoTFleetWise::DataManagement;

static const uint32_t MAX_NUMBER_OF_ACTIVE_CONDITION = 256; /**< More active conditions will be ignored */
static const uint32_t ALL_CONDITIONS = 0xFFFFFFFF;
static const uint32_t MAX_EQUATION_DEPTH =
    10; /**< If the AST of the expression is deeper than this value the equation is not accepted */
static const uint32_t MAX_SIGNAL_ID = 50000; /**< Signal IDs above this threshold will not be accepted */

static const double MIN_PROBABILITY = 0.0;
static const double MAX_PROBABILITY = 1.0;
// INPUT to collection and inspection engine:

// This values will be provided by CollectionSchemeManagement:

// The following structs describe an inspection view on all active collection conditions
// As a start these structs are mainly a copy of the data defined in ICollectionScheme
struct PassThroughMetaData
{
    bool compress;
    bool persist;
    uint32_t priority;
    std::string decoderID;
    std::string collectionSchemeID;
};

// Camera Data collection related types
enum class InspectionMatrixImageCollectionType
{
    TIME_BASED,
    FRAME_BASED,
    NONE
};

struct InspectionMatrixImageCollectionInfo
{
    ImageDeviceID deviceID;                             // Unique Identifier of the image sensor in the system
    uint32_t imageFormat;                               // Image format expected from the System e.g. PNG.
                                                        // Exact ids of the type will end up in an enum in the
                                                        // CollectionScheme decoder.
    InspectionMatrixImageCollectionType collectionType; // Whether Images are collected from the device based on
                                                        // a timewindow or based on frame number.
    uint32_t beforeDurationMs;                          // Amount of time in ms to be collected from the
                                                        // image sensor buffer. This time is counted before the
                                                        // condition is met. This is not relevant when the
};

// As a start these structs are mainly a copy of the data defined in ICollectionScheme but as plain old data structures
struct InspectionMatrixSignalCollectionInfo
{
    SignalID signalID;
    uint32_t sampleBufferSize;        /**<  at least this amount of last x samples will be kept in buffer*/
    uint32_t minimumSampleIntervalMs; /**<  zero means all signals are recorded as seen on the bus */
    uint32_t fixedWindowPeriod;       /**< zero means no fixed window sampling would happen */
    bool isConditionOnlySignal;       /**< Should the collected signals be sent to cloud or are the number
                                       * of samples in the buffer only necessary for condition evaluation
                                       */
};

struct InspectionMatrixCanFrameCollectionInfo
{
    CANRawFrameID frameID;
    CANChannelNumericID channelID;
    uint32_t sampleBufferSize;        /**<  at least this amount of last x raw can frames will be kept in buffer */
    uint32_t minimumSampleIntervalMs; /**<  0 which all frames are recording as seen on the bus */
};

struct ConditionWithCollectedData
{
    const ExpressionNode *condition; /**< points into InspectionMatrix.expressionNodes;
                                      * Raw pointer is used as needed for efficient AST and ConditionWithCollectedData
                                      * never exists without the relevant InspectionMatrix */
    uint32_t minimumPublishInterval;
    uint32_t afterDuration;
    std::vector<InspectionMatrixSignalCollectionInfo> signals;
    std::vector<InspectionMatrixCanFrameCollectionInfo> canFrames;
    bool includeActiveDtcs;
    bool triggerOnlyOnRisingEdge;
    double probabilityToSend;
    PassThroughMetaData metaData;
    std::vector<InspectionMatrixImageCollectionInfo> imageCollectionInfos;
    bool includeImageCapture;
};

struct InspectionMatrix
{
    std::vector<ConditionWithCollectedData> conditions;
    std::vector<ExpressionNode> expressionNodeStorage; /**< A list of Expression nodes from all conditions;
                                                        * to increase performance the expressionNodes from one
                                                        * collectionScheme should be close to each other (memory
                                                        * locality). The traversal is depth first preorder */
};

// These values are provided by the NetworkChannelConsumers
struct CollectedCanRawFrame
{
    CollectedCanRawFrame()
        : frameID( INVALID_CAN_FRAME_ID )
        , channelId( INVALID_CAN_CHANNEL_NUMERIC_ID )
        , receiveTime( 0.0 )
        , data{}
        , size( 0 )
    {
    }
    CollectedCanRawFrame( CANRawFrameID frameIDIn,
                          CANChannelNumericID channelIdIn,
                          timestampT receiveTimeIn,
                          std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> &dataIn,
                          uint8_t sizeIn )
        : frameID( frameIDIn )
        , channelId( channelIdIn )
        , receiveTime( receiveTimeIn )
        , data( dataIn )
        , size( sizeIn )
    {
    }
    CANRawFrameID frameID;
    CANChannelNumericID channelId;
    timestampT receiveTime;
    std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> data;
    uint8_t size;
};

struct CollectedSignal
{
    CollectedSignal()
        : signalID( INVALID_SIGNAL_ID )
        , receiveTime( 0 )
        , value( 0.0 )
    {
    }
    CollectedSignal( SignalID signalIDIn, timestampT receiveTimeIn, double valueIn )
        : signalID( signalIDIn )
        , receiveTime( receiveTimeIn )
        , value( valueIn )
    {
    }
    SignalID signalID;
    timestampT receiveTime;
    double value;
};

typedef boost::lockfree::queue<CollectedSignal>
    SignalBuffer; /**<  multi NetworkChannel Consumers fill this queue and only one instance of the Inspection and
                     Collection Engine consumes it. It is used for Can and OBD based signals */
typedef boost::lockfree::queue<CollectedCanRawFrame>
    CANBuffer; /**<  contains only raw can messages which at least one collectionScheme needs to publish in a raw
                  format. multi NetworkChannel Consumers fill this queue and only one instance of the Inspection and
                  Collection Engine consumes it */
typedef boost::lockfree::spsc_queue<DTCInfo>
    ActiveDTCBuffer; /**<  Set of currently active DTCs. produced by OBD NetworkChannel Consumer and consumed by
                        Inspection and CollectionEngine */

// Shared Pointer type to the buffer that send data to Collection Engine
typedef std::shared_ptr<SignalBuffer> SignalBufferPtr;
typedef std::shared_ptr<CANBuffer> CANBufferPtr;
typedef std::shared_ptr<ActiveDTCBuffer> ActiveDTCBufferPtr;

// Output of collection Inspection Engine

struct TriggeredCollectionSchemeData
{
    PassThroughMetaData metaData;
    timestampT triggerTime;
    std::vector<CollectedSignal> signals;
    std::vector<CollectedCanRawFrame> canFrames;
    DTCInfo mDTCInfo;
    GeohashInfo mGeohashInfo; // Because Geohash is not a physical signal from VSS, we decided to not using SignalID for
    // geohash. In future we might introduce virtual signal concept which will include geohash.
    EventID eventID;
};

typedef std::shared_ptr<const TriggeredCollectionSchemeData> TriggeredCollectionSchemeDataPtr;
typedef boost::lockfree::spsc_queue<TriggeredCollectionSchemeDataPtr>
    CollectedDataReadyToPublish; /**< produced by Inspection and Collection Engine consumed by Sender to cloud. As the
                                    data can be big only a shared_ptr is handed over*/

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws