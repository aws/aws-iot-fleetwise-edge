// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CollectionSchemeIngestion.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <boost/uuid/detail/sha1.hpp>
#include <exception>
#include <google/protobuf/message.h>
#include <string>
#include <vector>

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "aws/iotfleetwise/MessageTypes.h"
#include <unordered_map>
#include <utility>
#endif

namespace Aws
{
namespace IoTFleetWise
{

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
std::atomic<uint32_t> CollectionSchemeIngestion::mPartialSignalCounter( 0 ); // NOLINT Global atomic signal counter
#endif

CollectionSchemeIngestion::~CollectionSchemeIngestion()
{
    /* Delete all global objects allocated by libprotobuf. */
    google::protobuf::ShutdownProtobufLibrary();
}

bool
CollectionSchemeIngestion::operator==( const ICollectionScheme &other ) const
{
    bool isEqual = isReady() && other.isReady() && ( getCollectionSchemeID() == other.getCollectionSchemeID() );
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    // For Vision System Data, comparing just the ID isn't enough, because even though schemes with the same ID
    // should be identical from the perspective of the Cloud, FWE generates some additional data internally which can
    // change when it ingests a new message.
    isEqual =
        isEqual && ( getPartialSignalIdToSignalPathLookupTable() == other.getPartialSignalIdToSignalPathLookupTable() );
#endif
    return isEqual;
}

bool
CollectionSchemeIngestion::operator!=( const ICollectionScheme &other ) const
{
    return !( *this == other );
}

bool
CollectionSchemeIngestion::isReady() const
{
    return mReady;
}

bool
CollectionSchemeIngestion::copyData(
    std::shared_ptr<Schemas::CollectionSchemesMsg::CollectionScheme> protoCollectionSchemeMessagePtr )
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

    // build fetch informations
    if ( mProtoCollectionSchemeMessagePtr->signal_fetch_information_size() > 0 )
    {
        uint32_t numNodesCondition = 0U;
        uint32_t numNodesAction = 0U;

        for ( int i = 0; i < mProtoCollectionSchemeMessagePtr->signal_fetch_information_size(); i++ )
        {
            const Schemas::CollectionSchemesMsg::FetchInformation &signal_fetch_information =
                mProtoCollectionSchemeMessagePtr->signal_fetch_information( i );

            if ( signal_fetch_information.fetchConfig_case() ==
                 Schemas::CollectionSchemesMsg::FetchInformation::kTimeBased )
            {
                // empty (nullptr) condition node for time-based fetching
            }
            else if ( ( signal_fetch_information.fetchConfig_case() ==
                        Schemas::CollectionSchemesMsg::FetchInformation::kConditionBased ) &&
                      signal_fetch_information.condition_based().has_condition_tree() )
            {
                numNodesCondition +=
                    getNumberOfNodes( signal_fetch_information.condition_based().condition_tree(), MAX_EQUATION_DEPTH );
            }
            else
            {
                continue;
            }

            for ( int j = 0; j < signal_fetch_information.actions_size(); j++ )
            {
                numNodesAction += getNumberOfNodes( signal_fetch_information.actions( j ), MAX_EQUATION_DEPTH );
            }
        }

        if ( numNodesCondition > 0 )
        {
            FWE_LOG_TRACE( "The CollectionScheme has some fetch conditions => will build " +
                           std::to_string( numNodesCondition ) + " condition nodes for this purpose" );

            // realloc for mExpressionNodesForFetchCondition is not allowed after reservation
            // as pointers to its elements will be set during AST building process and be used later
            mExpressionNodesForFetchCondition.reserve( numNodesCondition );
        }

        if ( numNodesAction > 0 )
        {
            FWE_LOG_TRACE( "The CollectionScheme has some fetch actions => will build " +
                           std::to_string( numNodesAction ) + " fetch nodes for this purpose" );

            // realloc for mExpressionNodesForFetchAction is not allowed after reservation
            // as pointers to its elements will be set during AST building process and be used later
            mExpressionNodesForFetchAction.reserve( numNodesAction );
        }

        std::size_t currentIndexCondition = 0U;
        std::size_t currentIndexAction = 0U;

        for ( int i = 0; i < mProtoCollectionSchemeMessagePtr->signal_fetch_information_size(); i++ )
        {
            const Schemas::CollectionSchemesMsg::FetchInformation &signal_fetch_information =
                mProtoCollectionSchemeMessagePtr->signal_fetch_information( i );

            mFetchInformations.emplace_back();

            FetchInformation &fetchInformation = mFetchInformations.back();

            fetchInformation.signalID = signal_fetch_information.signal_id();

            if ( signal_fetch_information.fetchConfig_case() ==
                 Schemas::CollectionSchemesMsg::FetchInformation::kTimeBased )
            {
                // no need to set fetchInformation.condition to nullptr here as it is default constructed value
                fetchInformation.maxExecutionPerInterval = signal_fetch_information.time_based().max_execution_count();
                fetchInformation.executionPeriodMs = signal_fetch_information.time_based().execution_frequency_ms();
                fetchInformation.executionIntervalMs =
                    signal_fetch_information.time_based().reset_max_execution_count_interval_ms();
            }
            else if ( ( signal_fetch_information.fetchConfig_case() ==
                        Schemas::CollectionSchemesMsg::FetchInformation::kConditionBased ) &&
                      signal_fetch_information.condition_based().has_condition_tree() )
            {
                fetchInformation.condition = serializeNode( signal_fetch_information.condition_based().condition_tree(),
                                                            mExpressionNodesForFetchCondition,
                                                            currentIndexCondition,
                                                            MAX_EQUATION_DEPTH );

                if ( fetchInformation.condition == nullptr )
                {
                    FWE_LOG_WARN( "Fetch information #" + std::to_string( i ) +
                                  " contains invalid condition => will ignore the fetch information" );
                    mFetchInformations.pop_back();
                    continue;
                }

                fetchInformation.triggerOnlyOnRisingEdge =
                    ( signal_fetch_information.condition_based().condition_trigger_mode() ==
                      Schemas::CollectionSchemesMsg::
                          ConditionBasedFetchConfig_ConditionTriggerMode_TRIGGER_ONLY_ON_RISING_EDGE );
            }
            else
            {
                FWE_LOG_WARN( "Fetch information #" + std::to_string( i ) +
                              " contains unsupported configuration => will ignore the fetch information" );
                mFetchInformations.pop_back();
                continue;
            }

            bool isActionsValid = true;

            for ( int j = 0; j < signal_fetch_information.actions_size(); j++ )
            {
                ExpressionNode *action = serializeNode( signal_fetch_information.actions( j ),
                                                        mExpressionNodesForFetchAction,
                                                        currentIndexAction,
                                                        MAX_EQUATION_DEPTH );

                if ( action == nullptr )
                {
                    FWE_LOG_WARN( "Action #" + std::to_string( j ) + " of fetch information #" + std::to_string( i ) +
                                  " is invalid => will ignore the fetch information" );
                    isActionsValid = false;
                    break;
                }

                fetchInformation.actions.push_back( action );
            }

            if ( !isActionsValid )
            {
                mFetchInformations.pop_back();
            }
        }
    }

    if ( mFetchInformations.empty() )
    {
        FWE_LOG_TRACE( "The CollectionScheme does not have any valid fetch information" );
    }
    else
    {
        FWE_LOG_TRACE( "Adding " + std::to_string( mFetchInformations.size() ) +
                       " fetch informations into the CollectionScheme" );
    }

    // Build Collected Signals
    for ( int signalIndex = 0; signalIndex < mProtoCollectionSchemeMessagePtr->signal_information_size();
          ++signalIndex )
    {
        // Get a reference to the SignalInformation Message in the protobuf
        const Schemas::CollectionSchemesMsg::SignalInformation &signalInformation =
            mProtoCollectionSchemeMessagePtr->signal_information( signalIndex );

        std::string additionalTraceInfo;

        SignalCollectionInfo signalInfo;
        // Append the signal Information to the results
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        if ( signalInformation.has_signal_path() && ( signalInformation.signal_path().signal_path_size() > 0 ) )
        {
            signalInfo.signalID =
                getOrInsertPartialSignalId( signalInformation.signal_id(), signalInformation.signal_path() );
            additionalTraceInfo +=
                " with path length " + std::to_string( signalInformation.signal_path().signal_path_size() );
        }
        else
#endif
        {
            signalInfo.signalID = signalInformation.signal_id();
        }
        signalInfo.sampleBufferSize = signalInformation.sample_buffer_size();
        signalInfo.minimumSampleIntervalMs = signalInformation.minimum_sample_period_ms();
        signalInfo.fixedWindowPeriod = signalInformation.fixed_window_period_ms();
        signalInfo.isConditionOnlySignal = signalInformation.condition_only_signal();
#ifdef FWE_FEATURE_STORE_AND_FORWARD
        signalInfo.dataPartitionId = signalInformation.data_partition_id();
#endif

        FWE_LOG_TRACE( "Adding signalID: " + std::to_string( signalInfo.signalID ) + " to list of signals to collect" +
                       additionalTraceInfo );
        mCollectedSignals.emplace_back( signalInfo );
    }

#ifdef FWE_FEATURE_STORE_AND_FORWARD
    // calculate total number of nodes ahead of time,
    // as we can only allocate mExpressionNodes once
    uint32_t numForwardConditionNodes = 0;
    if ( mProtoCollectionSchemeMessagePtr->has_store_and_forward_configuration() )
    {
        for ( auto i = 0;
              i < mProtoCollectionSchemeMessagePtr->store_and_forward_configuration().partition_configuration_size();
              ++i )
        {
            auto protoPartitionConfiguration =
                mProtoCollectionSchemeMessagePtr->store_and_forward_configuration().partition_configuration( i );
            if ( protoPartitionConfiguration.has_upload_options() )
            {
                numForwardConditionNodes += getNumberOfNodes(
                    protoPartitionConfiguration.upload_options().condition_tree(), MAX_EQUATION_DEPTH );
            }
        }
    }
#endif

    // condition node
    if ( mProtoCollectionSchemeMessagePtr->collection_scheme_type_case() ==
         Schemas::CollectionSchemesMsg::CollectionScheme::kConditionBasedCollectionScheme )
    {
        auto numNodes =
            getNumberOfNodes( mProtoCollectionSchemeMessagePtr->condition_based_collection_scheme().condition_tree(),
                              MAX_EQUATION_DEPTH );
#ifdef FWE_FEATURE_STORE_AND_FORWARD
        numNodes += numForwardConditionNodes;
#endif

        FWE_LOG_INFO( "CollectionScheme is Condition Based. Building AST with " + std::to_string( numNodes ) +
                      " nodes" );

        mExpressionNodes.reserve( numNodes );

        // As pointers to elements inside the vector are used after this no realloc for mExpressionNodes is allowed
        std::size_t currentIndex = 0; // start at index 0 of mExpressionNodes for first node
        mExpressionNode =
            serializeNode( mProtoCollectionSchemeMessagePtr->condition_based_collection_scheme().condition_tree(),
                           mExpressionNodes,
                           currentIndex,
                           MAX_EQUATION_DEPTH );
        FWE_LOG_INFO( "AST complete" );
    }
    // time based node
    // For time based node the condition is always set to true hence: currentNode.booleanValue=true
    else if ( mProtoCollectionSchemeMessagePtr->collection_scheme_type_case() ==
              Schemas::CollectionSchemesMsg::CollectionScheme::kTimeBasedCollectionScheme )
    {
        FWE_LOG_INFO( "CollectionScheme is Time based with interval of: " +
                      std::to_string( mProtoCollectionSchemeMessagePtr->time_based_collection_scheme()
                                          .time_based_collection_scheme_period_ms() ) +
                      " ms" );

#ifdef FWE_FEATURE_STORE_AND_FORWARD
        mExpressionNodes.reserve( numForwardConditionNodes + 1 );
#endif

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

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    if ( mProtoCollectionSchemeMessagePtr->has_s3_upload_metadata() )
    {
        FWE_LOG_INFO( "S3 Upload Metadata was set CollectionScheme ID: " +
                      mProtoCollectionSchemeMessagePtr->campaign_sync_id() );
        mS3UploadMetadata.bucketName = mProtoCollectionSchemeMessagePtr->s3_upload_metadata().bucket_name();
        mS3UploadMetadata.prefix = mProtoCollectionSchemeMessagePtr->s3_upload_metadata().prefix();
        if ( ( mS3UploadMetadata.prefix.length() > 1 ) && ( mS3UploadMetadata.prefix[0] == '/' ) )
        {
            // Remove single leading slash
            mS3UploadMetadata.prefix.erase( 0, 1 );
        }
        mS3UploadMetadata.region = mProtoCollectionSchemeMessagePtr->s3_upload_metadata().region();
        mS3UploadMetadata.bucketOwner =
            mProtoCollectionSchemeMessagePtr->s3_upload_metadata().bucket_owner_account_id();
    }
#endif

#ifdef FWE_FEATURE_STORE_AND_FORWARD
    if ( mProtoCollectionSchemeMessagePtr->has_store_and_forward_configuration() )
    {
        FWE_LOG_INFO( "Store and Forward configuration was set CollectionScheme ID: " +
                      mProtoCollectionSchemeMessagePtr->campaign_sync_id() );
        for ( auto i = 0;
              i < mProtoCollectionSchemeMessagePtr->store_and_forward_configuration().partition_configuration_size();
              ++i )
        {
            auto protoPartitionConfiguration =
                mProtoCollectionSchemeMessagePtr->store_and_forward_configuration().partition_configuration( i );
            PartitionConfiguration partitionConfiguration;
            if ( protoPartitionConfiguration.has_storage_options() )
            {
                partitionConfiguration.storageOptions.storageLocation =
                    protoPartitionConfiguration.storage_options().storage_location();
                partitionConfiguration.storageOptions.maximumSizeInBytes =
                    protoPartitionConfiguration.storage_options().maximum_size_in_bytes();
                partitionConfiguration.storageOptions.minimumTimeToLiveInSeconds =
                    protoPartitionConfiguration.storage_options().minimum_time_to_live_in_seconds();
            }
            if ( protoPartitionConfiguration.has_upload_options() )
            {
                auto currentIndex = mExpressionNodes.size();
                partitionConfiguration.uploadOptions.conditionTree =
                    serializeNode( protoPartitionConfiguration.upload_options().condition_tree(),
                                   mExpressionNodes,
                                   currentIndex,
                                   MAX_EQUATION_DEPTH );
            }
            mStoreAndForwardConfig.emplace_back( partitionConfiguration );
        }
    }
#endif

    FWE_LOG_INFO( "Successfully built CollectionScheme ID: " + mProtoCollectionSchemeMessagePtr->campaign_sync_id() );

    // Set ready flag to true
    mReady = true;
    return true;
}

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
PartialSignalID
CollectionSchemeIngestion::getOrInsertPartialSignalId( SignalID signalId,
                                                       const Schemas::CommonTypesMsg::SignalPath &path )
{

    // Search if path already exists
    for ( auto &partialSignal : *mPartialSignalIDLookup )
    {
        if ( partialSignal.second.first == signalId )
        {
            if ( path.signal_path_size() == static_cast<int>( partialSignal.second.second.size() ) )
            {
                bool found = true;
                for ( int i = 0; i < path.signal_path_size(); i++ )
                {
                    if ( path.signal_path( i ) != partialSignal.second.second[static_cast<uint32_t>( i )] )
                    {
                        found = false;
                        break;
                    }
                }
                if ( found )
                {
                    return partialSignal.first;
                }
            }
        }
    }

    // Copy signal path
    SignalPath signal_path = SignalPath();
    for ( int i = 0; i < path.signal_path_size(); i++ )
    {
        signal_path.emplace_back( path.signal_path( i ) );
    }

    // coverity[misra_cpp_2008_rule_5_2_10_violation] For std::atomic this must be performed in a single statement
    // coverity[autosar_cpp14_m5_2_10_violation] For std::atomic this must be performed in a single statement
    PartialSignalID newPartialSignalId = mPartialSignalCounter++ | INTERNAL_SIGNAL_ID_BITMASK;

    ( *mPartialSignalIDLookup )[newPartialSignalId] = std::pair<SignalID, SignalPath>( signalId, signal_path );
    return newPartialSignalId;
}

const ICollectionScheme::PartialSignalIDLookup &
CollectionSchemeIngestion::getPartialSignalIdToSignalPathLookupTable() const
{

    if ( !mReady )
    {
        return INVALID_PARTIAL_SIGNAL_ID_LOOKUP;
    }
    return *mPartialSignalIDLookup;
}
#endif

#ifdef FWE_FEATURE_STORE_AND_FORWARD
const ICollectionScheme::StoreAndForwardConfig &
CollectionSchemeIngestion::getStoreAndForwardConfiguration() const
{
    if ( !mReady )
    {
        return INVALID_STORE_AND_FORWARD_CONFIG;
    }
    return mStoreAndForwardConfig;
}
#endif

const ICollectionScheme::ExpressionNode_t &
CollectionSchemeIngestion::getAllExpressionNodes() const
{
    if ( !mReady )
    {
        return INVALID_EXPRESSION_NODES;
    }

    return mExpressionNodes;
}

WindowFunction
CollectionSchemeIngestion::convertFunctionType(
    Schemas::CommonTypesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType function )
{
    switch ( function )
    {
    case Schemas::CommonTypesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType_LAST_WINDOW_MIN:
        FWE_LOG_INFO( "Converting node to: LAST_FIXED_WINDOW_MIN" );
        return WindowFunction::LAST_FIXED_WINDOW_MIN;
    case Schemas::CommonTypesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType_LAST_WINDOW_MAX:
        FWE_LOG_INFO( "Converting node to: LAST_FIXED_WINDOW_MAX" );
        return WindowFunction::LAST_FIXED_WINDOW_MAX;
    case Schemas::CommonTypesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType_LAST_WINDOW_AVG:
        FWE_LOG_INFO( "Converting node to: LAST_FIXED_WINDOW_AVG" );
        return WindowFunction::LAST_FIXED_WINDOW_AVG;
    case Schemas::CommonTypesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType_PREV_LAST_WINDOW_MIN:
        FWE_LOG_INFO( "Converting node to: PREV_LAST_FIXED_WINDOW_MIN" );
        return WindowFunction::PREV_LAST_FIXED_WINDOW_MIN;
    case Schemas::CommonTypesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType_PREV_LAST_WINDOW_MAX:
        FWE_LOG_INFO( "Converting node to: PREV_LAST_FIXED_WINDOW_MAX" );
        return WindowFunction::PREV_LAST_FIXED_WINDOW_MAX;
    case Schemas::CommonTypesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType_PREV_LAST_WINDOW_AVG:
        FWE_LOG_INFO( "Converting node to: PREV_LAST_FIXED_WINDOW_AVG" );
        return WindowFunction::PREV_LAST_FIXED_WINDOW_AVG;
    default:
        FWE_LOG_ERROR( "WindowFunction node type not supported." );
        return WindowFunction::NONE;
    }
}

ExpressionNodeType
CollectionSchemeIngestion::convertOperatorType( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator op )
{
    switch ( op )
    {
    case Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_SMALLER:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_SMALLER" );
        return ExpressionNodeType::OPERATOR_SMALLER;
    case Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_BIGGER" );
        return ExpressionNodeType::OPERATOR_BIGGER;
    case Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_SMALLER_EQUAL:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_SMALLER_EQUAL" );
        return ExpressionNodeType::OPERATOR_SMALLER_EQUAL;
    case Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_BIGGER_EQUAL:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_BIGGER_EQUAL" );
        return ExpressionNodeType::OPERATOR_BIGGER_EQUAL;
    case Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_EQUAL:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_EQUAL" );
        return ExpressionNodeType::OPERATOR_EQUAL;
    case Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_COMPARE_NOT_EQUAL:
        FWE_LOG_INFO( "Converting operator NodeOperator_Operator_COMPARE_NOT_EQUAL to OPERATOR_NOT_EQUAL" );
        return ExpressionNodeType::OPERATOR_NOT_EQUAL;
    case Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_LOGICAL_AND:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_LOGICAL_AND" );
        return ExpressionNodeType::OPERATOR_LOGICAL_AND;
    case Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_LOGICAL_OR:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_LOGICAL_OR" );
        return ExpressionNodeType::OPERATOR_LOGICAL_OR;
    case Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_LOGICAL_NOT:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_LOGICAL_NOT" );
        return ExpressionNodeType::OPERATOR_LOGICAL_NOT;
    case Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_ARITHMETIC_PLUS:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_ARITHMETIC_PLUS" );
        return ExpressionNodeType::OPERATOR_ARITHMETIC_PLUS;
    case Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_ARITHMETIC_MINUS:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_ARITHMETIC_MINUS" );
        return ExpressionNodeType::OPERATOR_ARITHMETIC_MINUS;
    case Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_ARITHMETIC_MULTIPLY:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_ARITHMETIC_MULTIPLY" );
        return ExpressionNodeType::OPERATOR_ARITHMETIC_MULTIPLY;
    case Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator_ARITHMETIC_DIVIDE:
        FWE_LOG_INFO( "Converting operator to: OPERATOR_ARITHMETIC_DIVIDE" );
        return ExpressionNodeType::OPERATOR_ARITHMETIC_DIVIDE;
    default:
        return ExpressionNodeType::BOOLEAN;
    }
}

uint32_t
CollectionSchemeIngestion::getNumberOfNodes( const Schemas::CommonTypesMsg::ConditionNode &node, const int depth )
{
    if ( depth <= 0 )
    {
        return 0;
    }
    uint32_t sum = 1; // this is one node so start with 1;
    if ( node.node_case() == Schemas::CommonTypesMsg::ConditionNode::kNodeOperator )
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
    else if ( node.node_case() == Schemas::CommonTypesMsg::ConditionNode::kNodeFunction )
    {
        if ( node.node_function().functionType_case() ==
             Schemas::CommonTypesMsg::ConditionNode_NodeFunction::kCustomFunction )
        {
            for ( int i = 0; i < node.node_function().custom_function().params_size(); i++ )
            {
                sum += getNumberOfNodes( node.node_function().custom_function().params( i ), depth - 1 );
            }
        }
        else if ( node.node_function().functionType_case() ==
                  Schemas::CommonTypesMsg::ConditionNode_NodeFunction::kIsNullFunction )
        {
            if ( node.node_function().is_null_function().has_expression() )
            {
                sum += getNumberOfNodes( node.node_function().is_null_function().expression(), depth - 1 );
            }
        }
    }
    return sum;
}

ExpressionNode *
CollectionSchemeIngestion::serializeNode( const Schemas::CommonTypesMsg::ConditionNode &node,
                                          ExpressionNode_t &expressionNodes,
                                          std::size_t &nextIndex,
                                          int remainingDepth )
{
    if ( remainingDepth <= 0 )
    {
        return nullptr;
    }
    expressionNodes.emplace_back();
    ExpressionNode *currentNode = nullptr;
    if ( expressionNodes.empty() || ( expressionNodes.size() <= nextIndex ) )
    {
        return nullptr;
    }
    else
    {
        currentNode = &( expressionNodes[nextIndex] );
    }
    nextIndex++;

    if ( node.node_case() == Schemas::CommonTypesMsg::ConditionNode::kNodeSignalId )
    {
        currentNode->signalID = node.node_signal_id();
        currentNode->nodeType = ExpressionNodeType::SIGNAL;
        FWE_LOG_TRACE( "Creating SIGNAL node with ID: " + std::to_string( currentNode->signalID ) );
        return currentNode;
    }
    else if ( node.node_case() == Schemas::CommonTypesMsg::ConditionNode::kNodeDoubleValue )
    {
        currentNode->floatingValue = node.node_double_value();
        currentNode->nodeType = ExpressionNodeType::FLOAT;
        FWE_LOG_TRACE( "Creating FLOAT node with value: " + std::to_string( currentNode->floatingValue ) );
        return currentNode;
    }
    else if ( node.node_case() == Schemas::CommonTypesMsg::ConditionNode::kNodeBooleanValue )
    {
        currentNode->booleanValue = node.node_boolean_value();
        currentNode->nodeType = ExpressionNodeType::BOOLEAN;
        FWE_LOG_TRACE( "Creating BOOLEAN node with value: " +
                       std::to_string( static_cast<int>( currentNode->booleanValue ) ) );
        return currentNode;
    }
    else if ( node.node_case() == Schemas::CommonTypesMsg::ConditionNode::kNodeStringValue )
    {
        currentNode->stringValue = node.node_string_value();
        currentNode->nodeType = ExpressionNodeType::STRING;
        FWE_LOG_TRACE( "Creating STRING node with value: " + currentNode->stringValue );
        return currentNode;
    }
    else if ( node.node_case() == Schemas::CommonTypesMsg::ConditionNode::kNodeFunction )
    {
        if ( node.node_function().functionType_case() ==
             Schemas::CommonTypesMsg::ConditionNode_NodeFunction::kWindowFunction )
        {
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
            if ( node.node_function().window_function().has_primitive_type_in_signal() )
            {
                if ( node.node_function().window_function().primitive_type_in_signal().has_signal_path() &&
                     ( node.node_function()
                           .window_function()
                           .primitive_type_in_signal()
                           .signal_path()
                           .signal_path_size() > 0 ) )
                {
                    currentNode->signalID = getOrInsertPartialSignalId(
                        node.node_function().window_function().primitive_type_in_signal().signal_id(),
                        node.node_function().window_function().primitive_type_in_signal().signal_path() );
                }
                else
                {
                    currentNode->signalID =
                        node.node_function().window_function().primitive_type_in_signal().signal_id();
                }
            }
            else
#endif
            {
                currentNode->signalID = node.node_function().window_function().signal_id();
            }
            currentNode->function.windowFunction =
                convertFunctionType( node.node_function().window_function().window_type() );
            currentNode->nodeType = ExpressionNodeType::WINDOW_FUNCTION;
            FWE_LOG_TRACE( "Creating Window FUNCTION node for Signal ID:" + std::to_string( currentNode->signalID ) );
            return currentNode;
        }
        else if ( node.node_function().functionType_case() ==
                  Schemas::CommonTypesMsg::ConditionNode_NodeFunction::kCustomFunction )
        {
            currentNode->function.customFunctionName = node.node_function().custom_function().function_name();
            // Generate a (probably) unique ID for the custom function invocation based on the collection scheme sync ID
            // and the invocation index:
            auto collectionSchemeId = mProtoCollectionSchemeMessagePtr->campaign_sync_id();
            auto invocationInfo = collectionSchemeId + ":" + std::to_string( mCustomFunctionInvocationCounter );
            boost::uuids::detail::sha1 invocationHash;
            try
            {
                invocationHash.process_bytes( invocationInfo.c_str(), invocationInfo.size() );
            }
            catch ( const std::exception &e )
            {
                FWE_LOG_ERROR( "Error calculating SHA1: " + std::string( e.what() ) );
                return nullptr;
            }
            uint32_t digest[5]{}; // SHA1 is 160-bit
            invocationHash.get_digest( digest );
            // Use the first 64-bits:
            currentNode->function.customFunctionInvocationId =
                static_cast<uint64_t>( digest[0] ) | static_cast<uint64_t>( digest[1] ) << 32;
            FWE_LOG_TRACE( "Invocation ID " +
                           customFunctionInvocationIdToHexString( currentNode->function.customFunctionInvocationId ) +
                           ": " + collectionSchemeId + ", invocation " +
                           std::to_string( mCustomFunctionInvocationCounter ) );
            mCustomFunctionInvocationCounter++;
            bool isParamsValid = true;

            for ( int i = 0; i < node.node_function().custom_function().params_size(); i++ )
            {
                FWE_LOG_TRACE( "Parsing param #" + std::to_string( i ) + " of CustomFunction node " +
                               currentNode->function.customFunctionName );

                ExpressionNode *param = serializeNode( node.node_function().custom_function().params( i ),
                                                       expressionNodes,
                                                       nextIndex,
                                                       remainingDepth - 1 );

                if ( param == nullptr )
                {
                    FWE_LOG_WARN( "Param #" + std::to_string( i ) + " of CustomFunction node " +
                                  currentNode->function.customFunctionName +
                                  " is invalid => will ignore the CustomFunction node" );
                    isParamsValid = false;
                    break;
                }

                currentNode->function.customFunctionParams.push_back( param );
            }

            if ( isParamsValid )
            {
                currentNode->nodeType = ExpressionNodeType::CUSTOM_FUNCTION;

                FWE_LOG_TRACE( "Creating CustomFunction node with name " + currentNode->function.customFunctionName +
                               " and " + std::to_string( currentNode->function.customFunctionParams.size() ) +
                               " params" );

                return currentNode;
            }
        }
        else if ( node.node_function().functionType_case() ==
                  Schemas::CommonTypesMsg::ConditionNode_NodeFunction::kIsNullFunction )
        {
            // If no expression node as parameter this isNull is invalid
            if ( node.node_function().is_null_function().has_expression() )
            {
                currentNode->nodeType = ExpressionNodeType::IS_NULL_FUNCTION;
                FWE_LOG_TRACE( "Processing isNull expression" );
                ExpressionNode *left = serializeNode( node.node_function().is_null_function().expression(),
                                                      expressionNodes,
                                                      nextIndex,
                                                      remainingDepth - 1 );

                currentNode->left = left;
                FWE_LOG_TRACE( "Setting right child to nullptr" );
                currentNode->right = nullptr;

                FWE_LOG_TRACE( "Creating IsNullFunction node" );
                return currentNode;
            }
            FWE_LOG_WARN( "Invalid isNull function" );
        }
        else
        {
            FWE_LOG_WARN( "Unsupported Function Node Type" );
        }
    }
    else if ( node.node_case() == Schemas::CommonTypesMsg::ConditionNode::kNodeOperator )
    {
        // If no left node this node_operator is invalid
        if ( node.node_operator().has_left_child() )
        {
            currentNode->nodeType = convertOperatorType( node.node_operator().operator_() );
            FWE_LOG_TRACE( "Processing left child" );
            ExpressionNode *left =
                serializeNode( node.node_operator().left_child(), expressionNodes, nextIndex, remainingDepth - 1 );
            currentNode->left = left;
            // Not operator is unary and has only left child
            if ( ( currentNode->nodeType != ExpressionNodeType::OPERATOR_LOGICAL_NOT ) &&
                 node.node_operator().has_right_child() )
            {
                FWE_LOG_TRACE( "Processing right child" );
                ExpressionNode *right =
                    serializeNode( node.node_operator().right_child(), expressionNodes, nextIndex, remainingDepth - 1 );
                currentNode->right = right;
            }
            else
            {
                FWE_LOG_TRACE( "Setting right child to nullptr" );
                currentNode->right = nullptr;
            }

            if ( ( currentNode->nodeType != ExpressionNodeType::BOOLEAN ) && ( currentNode->left != nullptr ) &&
                 ( ( currentNode->nodeType == ExpressionNodeType::OPERATOR_LOGICAL_NOT ) ||
                   ( currentNode->right != nullptr ) ) )
            {
                FWE_LOG_TRACE( "Creating Operator node" );

                return currentNode;
            }
        }

        FWE_LOG_WARN( "Invalid Operator node" );
    }
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    else if ( node.node_case() == Schemas::CommonTypesMsg::ConditionNode::kNodePrimitiveTypeInSignal )
    {
        if ( node.node_primitive_type_in_signal().has_signal_path() &&
             ( node.node_primitive_type_in_signal().signal_path().signal_path_size() > 0 ) )
        {
            currentNode->signalID = getOrInsertPartialSignalId( node.node_primitive_type_in_signal().signal_id(),
                                                                node.node_primitive_type_in_signal().signal_path() );
            FWE_LOG_TRACE( "Creating SIGNAL node with internal ID: " + std::to_string( currentNode->signalID ) +
                           " used for external ID " +
                           std::to_string( node.node_primitive_type_in_signal().signal_id() ) + " with path length:" +
                           std::to_string( node.node_primitive_type_in_signal().signal_path().signal_path_size() ) );
        }
        else
        {
            currentNode->signalID = node.node_primitive_type_in_signal().signal_id();
            FWE_LOG_TRACE( "Creating SIGNAL node with internal and external ID: " +
                           std::to_string( currentNode->signalID ) + " because path size is 0" );
        }
        currentNode->nodeType = ExpressionNodeType::SIGNAL;
        return currentNode;
    }
#endif

    // if not returned until here its an invalid node so remove it from buffer
    expressionNodes.pop_back();
    return nullptr;
}

const SyncID &
CollectionSchemeIngestion::getCollectionSchemeID() const
{
    if ( !mReady )
    {
        return INVALID_COLLECTION_SCHEME_ID;
    }

    return mProtoCollectionSchemeMessagePtr->campaign_sync_id();
}

#ifdef FWE_FEATURE_STORE_AND_FORWARD
const std::string &
CollectionSchemeIngestion::getCampaignArn() const
{
    if ( !mReady )
    {
        return INVALID_CAMPAIGN_ARN;
    }

    return mProtoCollectionSchemeMessagePtr->campaign_arn();
}
#endif

const SyncID &
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
         Schemas::CollectionSchemesMsg::CollectionScheme::kTimeBasedCollectionScheme )
    {
        return mProtoCollectionSchemeMessagePtr->time_based_collection_scheme()
            .time_based_collection_scheme_period_ms();
    }

    // Test if Message is ConditionBasedCollectionScheme
    if ( mProtoCollectionSchemeMessagePtr->collection_scheme_type_case() ==
         Schemas::CollectionSchemesMsg::CollectionScheme::kConditionBasedCollectionScheme )
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

const ExpressionNode *
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
         Schemas::CollectionSchemesMsg::CollectionScheme::kConditionBasedCollectionScheme )
    {
        return mProtoCollectionSchemeMessagePtr->condition_based_collection_scheme().condition_trigger_mode() ==
               Schemas::CollectionSchemesMsg::ConditionBasedCollectionScheme::ConditionTriggerMode::
                   ConditionBasedCollectionScheme_ConditionTriggerMode_TRIGGER_ONLY_ON_RISING_EDGE;
    }

    return false;
}

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
S3UploadMetadata
CollectionSchemeIngestion::getS3UploadMetadata() const
{
    if ( ( !mReady ) || ( !mProtoCollectionSchemeMessagePtr->has_s3_upload_metadata() ) )
    {
        return INVALID_S3_UPLOAD_METADATA;
    }
    return mS3UploadMetadata;
}
#endif

const ICollectionScheme::ExpressionNode_t &
CollectionSchemeIngestion::getAllExpressionNodesForFetchCondition() const
{
    if ( !mReady )
    {
        return INVALID_EXPRESSION_NODES;
    }

    return mExpressionNodesForFetchCondition;
}

const ICollectionScheme::ExpressionNode_t &
CollectionSchemeIngestion::getAllExpressionNodesForFetchAction() const
{
    if ( !mReady )
    {
        return INVALID_EXPRESSION_NODES;
    }

    return mExpressionNodesForFetchAction;
}

const ICollectionScheme::FetchInformation_t &
CollectionSchemeIngestion::getAllFetchInformations() const
{
    if ( !mReady )
    {
        return INVALID_FETCH_INFORMATIONS;
    }

    return mFetchInformations;
}

} // namespace IoTFleetWise
} // namespace Aws
