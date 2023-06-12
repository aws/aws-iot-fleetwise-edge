// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "CollectionSchemeIngestion.h"
#include "LoggingModule.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{

using namespace Aws::IoTFleetWise::Schemas;

CollectionSchemeIngestion::~CollectionSchemeIngestion()
{
    /* Delete all global objects allocated by libprotobuf. */
    google::protobuf::ShutdownProtobufLibrary();
}

bool
CollectionSchemeIngestion::isReady() const
{
    return mReady;
}

bool
CollectionSchemeIngestion::copyData(
    std::shared_ptr<CollectionSchemesMsg::CollectionScheme> protoCollectionSchemeMessagePtr )
{
    mProtoCollectionSchemeMessagePtr = protoCollectionSchemeMessagePtr;
    return true;
}

bool
CollectionSchemeIngestion::build()
{
    // Check if Collection collectionScheme has an ID and a Decoder Manifest ID
    if ( mProtoCollectionSchemeMessagePtr->campaign_sync_id().empty() ||
         mProtoCollectionSchemeMessagePtr->decoder_manifest_sync_id().empty() )
    {
        FWE_LOG_ERROR( "CollectionScheme does not have ID or DM ID" );
        return false;
    }

    // Check if the CollectionScheme EndTime is before the StartTime
    if ( mProtoCollectionSchemeMessagePtr->expiry_time_ms_epoch() <
         mProtoCollectionSchemeMessagePtr->start_time_ms_epoch() )
    {
        FWE_LOG_ERROR( "CollectionScheme end time comes before start time" );
        return false;
    }

    FWE_LOG_TRACE( "Building CollectionScheme with ID: " + mProtoCollectionSchemeMessagePtr->campaign_sync_id() );

    // Build Collected Signals
    for ( int signalIndex = 0; signalIndex < mProtoCollectionSchemeMessagePtr->signal_information_size();
          ++signalIndex )
    {
        // Get a reference to the SignalInformation Message in the protobuf
        const CollectionSchemesMsg::SignalInformation &signalInformation =
            mProtoCollectionSchemeMessagePtr->signal_information( signalIndex );

        SignalCollectionInfo signalInfo;
        // Append the signal Information to the results
        signalInfo.signalID = signalInformation.signal_id();
        signalInfo.sampleBufferSize = signalInformation.sample_buffer_size();
        signalInfo.minimumSampleIntervalMs = signalInformation.minimum_sample_period_ms();
        signalInfo.fixedWindowPeriod = signalInformation.fixed_window_period_ms();
        signalInfo.isConditionOnlySignal = signalInformation.condition_only_signal();

        FWE_LOG_TRACE( "Adding signalID: " + std::to_string( signalInfo.signalID ) + " to list of signals to collect" );
        mCollectedSignals.emplace_back( signalInfo );
    }

    // Build Raw CAN Data
    for ( int canFrameIndex = 0; canFrameIndex < mProtoCollectionSchemeMessagePtr->raw_can_frames_to_collect_size();
          ++canFrameIndex )
    {
        // Get a reference to the RAW CAN Frame in the protobuf
        const CollectionSchemesMsg::RawCanFrame &rawCanFrame =
            mProtoCollectionSchemeMessagePtr->raw_can_frames_to_collect( canFrameIndex );

        CanFrameCollectionInfo rawCAN;
        // Append the CAN Frame Information to the results
        rawCAN.frameID = rawCanFrame.can_message_id();
        rawCAN.interfaceID = rawCanFrame.can_interface_id();
        rawCAN.sampleBufferSize = rawCanFrame.sample_buffer_size();
        rawCAN.minimumSampleIntervalMs = rawCanFrame.minimum_sample_period_ms();

        FWE_LOG_TRACE( "Adding rawCAN frame to collect ID: " + std::to_string( rawCAN.frameID ) +
                       " node ID: " + rawCAN.interfaceID );
        mCollectedRawCAN.emplace_back( rawCAN );
    }

    // condition node
    if ( mProtoCollectionSchemeMessagePtr->collection_scheme_type_case() ==
         CollectionSchemesMsg::CollectionScheme::kConditionBasedCollectionScheme )
    {
        auto numNodes =
            getNumberOfNodes( mProtoCollectionSchemeMessagePtr->condition_based_collection_scheme().condition_tree(),
                              Aws::IoTFleetWise::DataInspection::MAX_EQUATION_DEPTH );

        FWE_LOG_INFO( "CollectionScheme is Condition Based. Building AST with " + std::to_string( numNodes ) +
                      " nodes" );

        mExpressionNodes.resize( numNodes );

        // As pointers to elements inside the vector are used after this no realloc for mExpressionNodes is allowed
        std::size_t currentIndex = 0; // start at index 0 of mExpressionNodes for first node
        mExpressionNode =
            serializeNode( mProtoCollectionSchemeMessagePtr->condition_based_collection_scheme().condition_tree(),
                           currentIndex,
                           Aws::IoTFleetWise::DataInspection::MAX_EQUATION_DEPTH );
        FWE_LOG_INFO( "AST complete" );
    }
    // time based node
    // For time based node the condition is always set to true hence: currentNode.booleanValue=true
    else if ( mProtoCollectionSchemeMessagePtr->collection_scheme_type_case() ==
              CollectionSchemesMsg::CollectionScheme::kTimeBasedCollectionScheme )
    {
        FWE_LOG_INFO( "CollectionScheme is Time based with interval of: " +
                      std::to_string( mProtoCollectionSchemeMessagePtr->time_based_collection_scheme()
                                          .time_based_collection_scheme_period_ms() ) +
                      " ms" );

        mExpressionNodes.emplace_back();
        ExpressionNode &currentNode = mExpressionNodes.back();
        currentNode.booleanValue = true;
        currentNode.nodeType = ExpressionNodeType::BOOLEAN;
        mExpressionNode = &currentNode;
    }
    else
    {
        FWE_LOG_ERROR( "COLLECTION_SCHEME_TYPE_NOT_SET" );
    }

    // Build Image capture collection info
    for ( int imageDataIndex = 0; imageDataIndex < mProtoCollectionSchemeMessagePtr->image_data_size();
          ++imageDataIndex )
    {
        // Get a reference to the RAW CAN Frame in the protobuf
        const CollectionSchemesMsg::ImageData &imageData =
            mProtoCollectionSchemeMessagePtr->image_data( imageDataIndex );

        ImageCollectionInfo imageCaptureData;
        // Read the Image capture settings
        imageCaptureData.deviceID = imageData.image_source_node_id();
        // We only support Time based image capture. We skip any other type.
        if ( imageData.image_collection_method_case() ==
             CollectionSchemesMsg::ImageData::ImageCollectionMethodCase::kTimeBasedImageData )
        {
            imageCaptureData.collectionType = ImageCollectionType::TIME_BASED;
            // Timing constraints
            imageCaptureData.beforeDurationMs = imageData.time_based_image_data().before_duration_ms();
            // Image format
            imageCaptureData.imageFormat = static_cast<uint32_t>( imageData.image_type() );
            FWE_LOG_INFO( "Adding Image capture settings for DeviceID: " +
                          std::to_string( imageCaptureData.deviceID ) );

            mImagesCaptureData.emplace_back( imageCaptureData );
        }
        else
        {
            FWE_LOG_WARN( "Unsupported Image capture settings provided, skipping" );
        }
    }

    FWE_LOG_INFO( "Successfully built CollectionScheme ID: " + mProtoCollectionSchemeMessagePtr->campaign_sync_id() );

    // Set ready flag to true
    mReady = true;
    return true;
}

const ICollectionScheme::ExpressionNode_t &
CollectionSchemeIngestion::getAllExpressionNodes() const
{
    if ( !mReady )
    {
        return INVALID_EXPRESSION_NODE;
    }

    return mExpressionNodes;
}

WindowFunction
CollectionSchemeIngestion::convertFunctionType(
    CollectionSchemesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType function )
{
    switch ( function )
    {
    case CollectionSchemesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType_LAST_WINDOW_MIN:
        FWE_LOG_INFO( "Converting node to: LAST_FIXED_WINDOW_MIN" );
        return WindowFunction::LAST_FIXED_WINDOW_MIN;
    case CollectionSchemesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType_LAST_WINDOW_MAX:
        FWE_LOG_INFO( "Converting node to: LAST_FIXED_WINDOW_MAX" );
        return WindowFunction::LAST_FIXED_WINDOW_MAX;
    case CollectionSchemesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType_LAST_WINDOW_AVG:
        FWE_LOG_INFO( "Converting node to: LAST_FIXED_WINDOW_AVG" );
        return WindowFunction::LAST_FIXED_WINDOW_AVG;
    case CollectionSchemesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType_PREV_LAST_WINDOW_MIN:
        FWE_LOG_INFO( "Converting node to: PREV_LAST_FIXED_WINDOW_MIN" );
        return WindowFunction::PREV_LAST_FIXED_WINDOW_MIN;
    case CollectionSchemesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType_PREV_LAST_WINDOW_MAX:
        FWE_LOG_INFO( "Converting node to: PREV_LAST_FIXED_WINDOW_MAX" );
        return WindowFunction::PREV_LAST_FIXED_WINDOW_MAX;
    case CollectionSchemesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType_PREV_LAST_WINDOW_AVG:
        FWE_LOG_INFO( "Converting node to: PREV_LAST_FIXED_WINDOW_AVG" );
        return WindowFunction::PREV_LAST_FIXED_WINDOW_AVG;
    default:
        FWE_LOG_ERROR( "Function node type not supported." );
        return WindowFunction::NONE;
    }
}

ExpressionNodeType
CollectionSchemeIngestion::convertOperatorType( CollectionSchemesMsg::ConditionNode_NodeOperator_Operator op )
{
    switch ( op )
    {
    case CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_COMPARE_SMALLER:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_SMALLER" );
        return ExpressionNodeType::OPERATOR_SMALLER;
    case CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_BIGGER" );
        return ExpressionNodeType::OPERATOR_BIGGER;
    case CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_COMPARE_SMALLER_EQUAL:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_SMALLER_EQUAL" );
        return ExpressionNodeType::OPERATOR_SMALLER_EQUAL;
    case CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER_EQUAL:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_BIGGER_EQUAL" );
        return ExpressionNodeType::OPERATOR_BIGGER_EQUAL;
    case CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_COMPARE_EQUAL:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_EQUAL" );
        return ExpressionNodeType::OPERATOR_EQUAL;
    case CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_COMPARE_NOT_EQUAL:
        FWE_LOG_INFO( "Converting operator NodeOperator_Operator_COMPARE_NOT_EQUAL to OPERATOR_NOT_EQUAL" );
        return ExpressionNodeType::OPERATOR_NOT_EQUAL;
    case CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_LOGICAL_AND:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_LOGICAL_AND" );
        return ExpressionNodeType::OPERATOR_LOGICAL_AND;
    case CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_LOGICAL_OR:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_LOGICAL_OR" );
        return ExpressionNodeType::OPERATOR_LOGICAL_OR;
    case CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_LOGICAL_NOT:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_LOGICAL_NOT" );
        return ExpressionNodeType::OPERATOR_LOGICAL_NOT;
    case CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_ARITHMETIC_PLUS:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_ARITHMETIC_PLUS" );
        return ExpressionNodeType::OPERATOR_ARITHMETIC_PLUS;
    case CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_ARITHMETIC_MINUS:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_ARITHMETIC_MINUS" );
        return ExpressionNodeType::OPERATOR_ARITHMETIC_MINUS;
    case CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_ARITHMETIC_MULTIPLY:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_ARITHMETIC_MULTIPLY" );
        return ExpressionNodeType::OPERATOR_ARITHMETIC_MULTIPLY;
    case CollectionSchemesMsg::ConditionNode_NodeOperator_Operator_ARITHMETIC_DIVIDE:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_ARITHMETIC_DIVIDE" );
        return ExpressionNodeType::OPERATOR_ARITHMETIC_DIVIDE;
    default:
        return ExpressionNodeType::BOOLEAN;
    }
}

uint32_t
CollectionSchemeIngestion::getNumberOfNodes( const CollectionSchemesMsg::ConditionNode &node, const int depth )
{
    if ( depth <= 0 )
    {
        return 0;
    }
    uint32_t sum = 1; // this is one node so start with 1;
    if ( node.node_case() == CollectionSchemesMsg::ConditionNode::kNodeOperator )
    {
        if ( node.node_operator().has_left_child() )
        {
            sum += getNumberOfNodes( node.node_operator().left_child(), depth - 1 );
        }

        if ( node.node_operator().has_right_child() )
        {
            sum += getNumberOfNodes( node.node_operator().right_child(), depth - 1 );
        }
    }
    return sum;
}

ExpressionNode *
CollectionSchemeIngestion::serializeNode( const CollectionSchemesMsg::ConditionNode &node,
                                          std::size_t &nextIndex,
                                          int remainingDepth )
{
    if ( remainingDepth <= 0 )
    {
        return nullptr;
    }
    mExpressionNodes.emplace_back();
    ExpressionNode *currentNode = nullptr;
    if ( mExpressionNodes.empty() || ( mExpressionNodes.size() <= nextIndex ) )
    {
        return nullptr;
    }
    else
    {
        currentNode = &( mExpressionNodes[nextIndex] );
    }
    nextIndex++;

    if ( node.node_case() == CollectionSchemesMsg::ConditionNode::kNodeSignalId )
    {
        currentNode->signalID = node.node_signal_id();
        currentNode->nodeType = ExpressionNodeType::SIGNAL;
        FWE_LOG_TRACE( "Creating SIGNAL node with ID: " + std::to_string( currentNode->signalID ) );
        return currentNode;
    }
    else if ( node.node_case() == CollectionSchemesMsg::ConditionNode::kNodeDoubleValue )
    {
        currentNode->floatingValue = node.node_double_value();
        currentNode->nodeType = ExpressionNodeType::FLOAT;
        FWE_LOG_TRACE( "Creating FLOAT node with value: " + std::to_string( currentNode->floatingValue ) );
        return currentNode;
    }
    else if ( node.node_case() == CollectionSchemesMsg::ConditionNode::kNodeBooleanValue )
    {
        currentNode->booleanValue = node.node_boolean_value();
        currentNode->nodeType = ExpressionNodeType::BOOLEAN;
        FWE_LOG_TRACE( "Creating BOOLEAN node with value: " +
                       std::to_string( static_cast<int>( currentNode->booleanValue ) ) );
        return currentNode;
    }
    else if ( node.node_case() == CollectionSchemesMsg::ConditionNode::kNodeFunction )
    {
        if ( node.node_function().functionType_case() ==
             CollectionSchemesMsg::ConditionNode_NodeFunction::kWindowFunction )
        {
            currentNode->signalID = node.node_function().window_function().signal_id();
            currentNode->function.windowFunction =
                convertFunctionType( node.node_function().window_function().window_type() );
            currentNode->nodeType = ExpressionNodeType::WINDOWFUNCTION;
            FWE_LOG_TRACE( "Creating Window FUNCTION node for Signal ID:" + std::to_string( currentNode->signalID ) );
            return currentNode;
        }
        else if ( node.node_function().functionType_case() ==
                  CollectionSchemesMsg::ConditionNode_NodeFunction::kGeohashFunction )
        {
            if ( ( node.node_function().geohash_function().geohash_precision() > UINT8_MAX ) ||
                 ( node.node_function().geohash_function().gps_unit() >=
                   toUType( GeohashFunction::GPSUnitType::MAX ) ) )
            {
                FWE_LOG_WARN( "Invalid Geohash function arguments" );
            }
            else
            {
                currentNode->nodeType = ExpressionNodeType::GEOHASHFUNCTION; // geohash
                currentNode->function.geohashFunction.latitudeSignalID =
                    node.node_function().geohash_function().latitude_signal_id();
                currentNode->function.geohashFunction.longitudeSignalID =
                    node.node_function().geohash_function().longitude_signal_id();
                currentNode->function.geohashFunction.precision =
                    static_cast<uint8_t>( node.node_function().geohash_function().geohash_precision() );
                // coverity[autosar_cpp14_a7_2_1_violation] Range is checked by the if-statement above
                currentNode->function.geohashFunction.gpsUnitType =
                    static_cast<GeohashFunction::GPSUnitType>( node.node_function().geohash_function().gps_unit() );
                FWE_LOG_TRACE(
                    "Creating Geohash FUNCTION node: Lat SignalID: " +
                    std::to_string( currentNode->function.geohashFunction.latitudeSignalID ) +
                    "; Lon SignalID: " + std::to_string( currentNode->function.geohashFunction.longitudeSignalID ) +
                    "; precision: " + std::to_string( currentNode->function.geohashFunction.precision ) +
                    "; GPS Unit Type: " +
                    std::to_string( static_cast<uint8_t>( currentNode->function.geohashFunction.gpsUnitType ) ) );
                return currentNode;
            }
        }
        else
        {
            FWE_LOG_WARN( "Unsupported Function Node Type" );
        }
    }
    else if ( node.node_case() == CollectionSchemesMsg::ConditionNode::kNodeOperator )
    {
        // If no left node this node_operator is invalid
        if ( node.node_operator().has_left_child() )
        {
            FWE_LOG_TRACE( "Processing left child" );
            currentNode->nodeType = convertOperatorType( node.node_operator().operator_() );
            // If no valid function found return always false
            if ( currentNode->nodeType == ExpressionNodeType::BOOLEAN )
            {
                FWE_LOG_INFO( "Setting BOOLEAN node to false" );
                currentNode->booleanValue = false;
                return currentNode;
            }
            ExpressionNode *left = serializeNode( node.node_operator().left_child(), nextIndex, remainingDepth - 1 );
            currentNode->left = left;
            // Not operator is unary and has only left child
            if ( ( currentNode->nodeType != ExpressionNodeType::OPERATOR_LOGICAL_NOT ) &&
                 node.node_operator().has_right_child() )
            {
                FWE_LOG_TRACE( "Processing right child" );
                ExpressionNode *right =
                    serializeNode( node.node_operator().right_child(), nextIndex, remainingDepth - 1 );
                currentNode->right = right;
            }
            else
            {
                FWE_LOG_TRACE( "Setting right child to nullptr" );
                currentNode->right = nullptr;
            }
            return currentNode;
        }
    }

    // if not returned until here its an invalid node so remove it from buffer
    mExpressionNodes.pop_back();
    return nullptr;
}

const std::string &
CollectionSchemeIngestion::getCollectionSchemeID() const
{
    if ( !mReady )
    {
        return INVALID_COLLECTION_SCHEME_ID;
    }

    return mProtoCollectionSchemeMessagePtr->campaign_sync_id();
}

const std::string &
CollectionSchemeIngestion::getDecoderManifestID() const
{
    if ( !mReady )
    {
        return INVALID_DECODER_MANIFEST_ID;
    }

    return mProtoCollectionSchemeMessagePtr->decoder_manifest_sync_id();
}

uint64_t
CollectionSchemeIngestion::getStartTime() const
{
    if ( !mReady )
    {
        return INVALID_COLLECTION_SCHEME_START_TIME;
    }

    return mProtoCollectionSchemeMessagePtr->start_time_ms_epoch();
}

uint64_t
CollectionSchemeIngestion::getExpiryTime() const
{
    if ( !mReady )
    {
        return INVALID_COLLECTION_SCHEME_EXPIRY_TIME;
    }

    return mProtoCollectionSchemeMessagePtr->expiry_time_ms_epoch();
}

uint32_t
CollectionSchemeIngestion::getAfterDurationMs() const
{
    if ( !mReady )
    {
        return INVALID_AFTER_TRIGGER_DURATION;
    }

    return mProtoCollectionSchemeMessagePtr->after_duration_ms();
}

bool
CollectionSchemeIngestion::isActiveDTCsIncluded() const
{
    if ( !mReady )
    {
        return false;
    }

    return mProtoCollectionSchemeMessagePtr->include_active_dtcs();
}

uint32_t
CollectionSchemeIngestion::getPriority() const
{
    if ( !mReady )
    {
        return INVALID_PRIORITY_LEVEL;
    }

    return mProtoCollectionSchemeMessagePtr->priority();
}

bool
CollectionSchemeIngestion::isPersistNeeded() const
{
    if ( !mReady )
    {
        return false;
    }

    return mProtoCollectionSchemeMessagePtr->persist_all_collected_data();
}

bool
CollectionSchemeIngestion::isCompressionNeeded() const
{
    if ( !mReady )
    {
        return false;
    }

    return mProtoCollectionSchemeMessagePtr->compress_collected_data();
}

uint32_t
CollectionSchemeIngestion::getMinimumPublishIntervalMs() const
{
    if ( !mReady )
    {
        return INVALID_MINIMUM_PUBLISH_TIME;
    }

    // Test if Message is TimeBasedCollectionScheme
    if ( mProtoCollectionSchemeMessagePtr->collection_scheme_type_case() ==
         CollectionSchemesMsg::CollectionScheme::kTimeBasedCollectionScheme )
    {
        return mProtoCollectionSchemeMessagePtr->time_based_collection_scheme()
            .time_based_collection_scheme_period_ms();
    }

    // Test if Message is ConditionBasedCollectionScheme
    if ( mProtoCollectionSchemeMessagePtr->collection_scheme_type_case() ==
         CollectionSchemesMsg::CollectionScheme::kConditionBasedCollectionScheme )
    {
        return mProtoCollectionSchemeMessagePtr->condition_based_collection_scheme().condition_minimum_interval_ms();
    }

    // Default Return Condition
    return INVALID_MINIMUM_PUBLISH_TIME;
}

const ICollectionScheme::Signals_t &
CollectionSchemeIngestion::getCollectSignals() const
{
    if ( !mReady )
    {
        return INVALID_COLLECTED_SIGNALS;
    }

    return mCollectedSignals;
}

const ICollectionScheme::RawCanFrames_t &
CollectionSchemeIngestion::getCollectRawCanFrames() const
{
    if ( !mReady )
    {
        return INVALID_RAW_CAN_COLLECTED_SIGNALS;
    }

    return mCollectedRawCAN;
}

const ICollectionScheme::ImagesDataType &
CollectionSchemeIngestion::getImageCaptureData() const
{
    if ( !mReady )
    {
        return INVALID_IMAGE_DATA;
    }

    return mImagesCaptureData;
}

const struct ExpressionNode *
CollectionSchemeIngestion::getCondition() const
{
    if ( !mReady )
    {
        return nullptr;
    }

    return mExpressionNode;
}

bool
CollectionSchemeIngestion::isTriggerOnlyOnRisingEdge() const
{
    if ( !mReady )
    {
        return false;
    }
    // Test if Message is ConditionBasedCollectionScheme
    if ( mProtoCollectionSchemeMessagePtr->collection_scheme_type_case() ==
         CollectionSchemesMsg::CollectionScheme::kConditionBasedCollectionScheme )
    {
        return mProtoCollectionSchemeMessagePtr->condition_based_collection_scheme().condition_trigger_mode() ==
               CollectionSchemesMsg::ConditionBasedCollectionScheme::ConditionTriggerMode::
                   ConditionBasedCollectionScheme_ConditionTriggerMode_TRIGGER_ONLY_ON_RISING_EDGE;
    }

    return false;
}

double
CollectionSchemeIngestion::getProbabilityToSend() const
{
    if ( !mReady )
    {
        return INVALID_PROBABILITY_TO_SEND;
    }
    if ( !mProtoCollectionSchemeMessagePtr->has_probabilities() )
    {
        return DEFAULT_PROBABILITY_TO_SEND;
    }
    return mProtoCollectionSchemeMessagePtr->probabilities().probability_to_send();
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
