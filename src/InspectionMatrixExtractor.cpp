// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CollectionSchemeManager.h" // IWYU pragma: associated
#include "aws/iotfleetwise/ICollectionScheme.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <algorithm> // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <unordered_map> // IWYU pragma: keep
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

void
CollectionSchemeManager::updateRawDataBufferConfigStringSignals(
    std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> &updatedSignals )
{
    if ( mDecoderManifest == nullptr )
    {
        return;
    }
    if ( isCollectionSchemesInSyncWithDm() )
    {
        // Iterate through enabled collectionScheme lists to locate the string signals to be collected
        for ( auto it = mEnabledCollectionSchemeMap.begin(); it != mEnabledCollectionSchemeMap.end(); ++it )
        {
            const auto &collectionSchemePtr = it->second;
            for ( const auto &signalInfo : collectionSchemePtr->getCollectSignals() )
            {
                SignalID signalId = signalInfo.signalID;
                RawData::SignalUpdateConfig signalConfig;
                signalConfig.typeId = signalId;

                auto networkType = mDecoderManifest->getNetworkProtocol( signalId );
                if ( networkType != VehicleDataSourceProtocol::CUSTOM_DECODING )
                {
                    continue;
                }

                auto customSignalDecoderFormat = mDecoderManifest->getCustomSignalDecoderFormat( signalId );
                if ( customSignalDecoderFormat == INVALID_CUSTOM_SIGNAL_DECODER_FORMAT )
                {
                    continue;
                }

                if ( customSignalDecoderFormat.mSignalType != SignalType::STRING )
                {
                    continue;
                }

                signalConfig.interfaceId = customSignalDecoderFormat.mInterfaceId;
                signalConfig.messageId = customSignalDecoderFormat.mDecoder;

                updatedSignals[signalId] = signalConfig;
            }
        }
    }
#ifdef FWE_FEATURE_REMOTE_COMMANDS
    /* TODO: Here we should iterate over all signals from all network interface types looking
       for string signals that are WRITE or READ_WRITE and add them.
       But the READ/WRITE/READ_WRITE indication for each signal is not yet available, so until then
       we only support custom decoded signals with the decoder id being the actuator name. */
    auto customSignalDecoderFormatMap = mDecoderManifest->getSignalIDToCustomSignalDecoderFormatMap();
    if ( mGetActuatorNamesCallback && customSignalDecoderFormatMap )
    {
        auto actuatorNames = mGetActuatorNamesCallback();
        for ( const auto &interface : actuatorNames )
        {
            for ( const auto &actuatorName : interface.second )
            {
                for ( const auto &customSignalDecoderFormat : *customSignalDecoderFormatMap )
                {
                    if ( ( interface.first != customSignalDecoderFormat.second.mInterfaceId ) ||
                         ( actuatorName != customSignalDecoderFormat.second.mDecoder ) )
                    {
                        continue;
                    }
                    if ( customSignalDecoderFormat.second.mSignalType == SignalType::STRING )
                    {
                        auto signalId = customSignalDecoderFormat.first;
                        updatedSignals[signalId] =
                            RawData::SignalUpdateConfig{ signalId,
                                                         customSignalDecoderFormat.second.mInterfaceId,
                                                         customSignalDecoderFormat.second.mDecoder };
                    }
                    break;
                }
            }
        }
    }
#endif
}

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
void
CollectionSchemeManager::updateRawDataBufferConfigComplexSignals(
    Aws::IoTFleetWise::ComplexDataDecoderDictionary *complexDataDecoderDictionary,
    std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> &updatedSignals )
{
    if ( ( mDecoderManifest != nullptr ) && isCollectionSchemesInSyncWithDm() )
    {
        // Iterate through enabled collectionScheme lists to locate the signals to be collected
        for ( auto it = mEnabledCollectionSchemeMap.begin(); it != mEnabledCollectionSchemeMap.end(); ++it )
        {
            const auto &collectionSchemePtr = it->second;
            // first iterate through the signalID lists
            for ( const auto &signalInfo : collectionSchemePtr->getCollectSignals() )
            {
                SignalID signalId = signalInfo.signalID;
                if ( ( signalId & INTERNAL_SIGNAL_ID_BITMASK ) != 0 )
                {
                    continue;
                }

                auto networkType = mDecoderManifest->getNetworkProtocol( signalId );
                if ( networkType != VehicleDataSourceProtocol::COMPLEX_DATA )
                {
                    continue;
                }

                RawData::SignalUpdateConfig signalConfig;
                signalConfig.typeId = signalId;

                auto complexSignalDecoderFormat = mDecoderManifest->getComplexSignalDecoderFormat( signalId );
                signalConfig.interfaceId = complexSignalDecoderFormat.mInterfaceId;
                if ( complexDataDecoderDictionary != nullptr )
                {
                    auto interface = complexDataDecoderDictionary->complexMessageDecoderMethod.find(
                        complexSignalDecoderFormat.mInterfaceId );
                    // Now try to find the messageId for this signal
                    if ( interface != complexDataDecoderDictionary->complexMessageDecoderMethod.end() )
                    {
                        auto complexDataMessage = std::find_if(
                            interface->second.begin(), interface->second.end(), [signalId]( const auto &pair ) -> bool {
                                return pair.second.mSignalId == signalId;
                            } );
                        if ( complexDataMessage != interface->second.end() )
                        {
                            signalConfig.messageId = complexDataMessage->first;
                        }
                    }
                }
                updatedSignals[signalId] = signalConfig;
            }
        }
    }
}
#endif

void
CollectionSchemeManager::addConditionData( const ICollectionScheme &collectionScheme,
                                           ConditionWithCollectedData &conditionData )
{
    conditionData.metadata.compress = collectionScheme.isCompressionNeeded();
    conditionData.metadata.persist = collectionScheme.isPersistNeeded();
    conditionData.metadata.priority = collectionScheme.getPriority();
    conditionData.metadata.decoderID = collectionScheme.getDecoderManifestID();
    conditionData.metadata.collectionSchemeID = collectionScheme.getCollectionSchemeID();
#ifdef FWE_FEATURE_STORE_AND_FORWARD
    conditionData.metadata.campaignArn = collectionScheme.getCampaignArn();
#endif

    /*
     * use for loop to copy signalInfo and CANframe over to avoid error or memory issue
     * This is probably not the fastest way to get things done, but the safest way
     * since the object is not big, so not really slow
     */
    const std::vector<SignalCollectionInfo> &collectionSignals = collectionScheme.getCollectSignals();
    for ( uint32_t i = 0; i < collectionSignals.size(); i++ )
    {
        InspectionMatrixSignalCollectionInfo inspectionSignal = {};
        inspectionSignal.signalID = collectionSignals[i].signalID;
        inspectionSignal.sampleBufferSize = collectionSignals[i].sampleBufferSize;
        inspectionSignal.minimumSampleIntervalMs = collectionSignals[i].minimumSampleIntervalMs;
        inspectionSignal.fixedWindowPeriod = collectionSignals[i].fixedWindowPeriod;
        inspectionSignal.isConditionOnlySignal = collectionSignals[i].isConditionOnlySignal;
        inspectionSignal.signalType = getSignalType( collectionSignals[i].signalID );
        conditionData.signals.emplace_back( inspectionSignal );
    }

    conditionData.minimumPublishIntervalMs = collectionScheme.getMinimumPublishIntervalMs();
    conditionData.afterDuration = collectionScheme.getAfterDurationMs();
    conditionData.includeActiveDtcs = collectionScheme.isActiveDTCsIncluded();
    conditionData.triggerOnlyOnRisingEdge = collectionScheme.isTriggerOnlyOnRisingEdge();
}

static void
buildExpressionNodeMapAndVector( const ExpressionNode *expressionNode,
                                 std::map<const ExpressionNode *, uint32_t> &expressionNodeToIndexMap,
                                 std::vector<const ExpressionNode *> &expressionNodes,
                                 uint32_t &index,
                                 bool &isStaticCondition,
                                 bool &alwaysEvaluateCondition )
{
    if ( expressionNode == nullptr )
    {
        return;
    }

    expressionNodeToIndexMap[expressionNode] = index;
    index++;
    expressionNodes.push_back( expressionNode );

    if ( expressionNode->nodeType == ExpressionNodeType::SIGNAL )
    {
        // If at least one of the nodes is a signal value, condition is not static
        isStaticCondition = false;
    }

    if ( expressionNode->nodeType == ExpressionNodeType::CUSTOM_FUNCTION )
    {
        // Custom functions should always be reevaluated
        alwaysEvaluateCondition = true;
        for ( const auto &param : expressionNode->function.customFunctionParams )
        {
            buildExpressionNodeMapAndVector(
                param, expressionNodeToIndexMap, expressionNodes, index, isStaticCondition, alwaysEvaluateCondition );
        }
    }

    if ( expressionNode->nodeType == ExpressionNodeType::IS_NULL_FUNCTION )
    {
        // Null function should always be reevaluated
        alwaysEvaluateCondition = true;
    }

    buildExpressionNodeMapAndVector( expressionNode->left,
                                     expressionNodeToIndexMap,
                                     expressionNodes,
                                     index,
                                     isStaticCondition,
                                     alwaysEvaluateCondition );
    buildExpressionNodeMapAndVector( expressionNode->right,
                                     expressionNodeToIndexMap,
                                     expressionNodes,
                                     index,
                                     isStaticCondition,
                                     alwaysEvaluateCondition );
}

void
CollectionSchemeManager::matrixExtractor( InspectionMatrix &inspectionMatrix, FetchMatrix &fetchMatrix )
{
    std::map<const ExpressionNode *, uint32_t> expressionNodeToIndexMap;
    std::vector<const ExpressionNode *> expressionNodes;
    uint32_t index = 0U;
    FetchRequestID fetchRequestID = 0U;

    if ( !isCollectionSchemesInSyncWithDm() )
    {
        return;
    }

    for ( const auto &enabledCollectionScheme : mEnabledCollectionSchemeMap )
    {
        auto collectionScheme = enabledCollectionScheme.second;

        extractCondition( inspectionMatrix,
                          *collectionScheme,
                          expressionNodes,
                          expressionNodeToIndexMap,
                          index,
                          collectionScheme->getCondition() );

        ConditionWithCollectedData &conditionWithCollectedData = inspectionMatrix.conditions.back();

        // extract FetchInformation
        const std::vector<FetchInformation> &fetchInformations = collectionScheme->getAllFetchInformations();

        for ( const auto &fetchInformation : fetchInformations )
        {
            bool isValid = true;

            auto itFetchRequests =
                fetchMatrix.fetchRequests.emplace( fetchRequestID, std::vector<FetchRequest>() ).first;
            std::vector<FetchRequest> &fetchRequests = itFetchRequests->second;

            SignalID signalID = fetchInformation.signalID;

            for ( const auto &action : fetchInformation.actions )
            {
                if ( action->nodeType != ExpressionNodeType::CUSTOM_FUNCTION )
                {
                    FWE_LOG_WARN( "Ignored fetch information due to unsupported action "
                                  "(only custom functions are allowed)" );
                    isValid = false;
                    break;
                }

                fetchRequests.emplace_back();

                FetchRequest &fetchRequest = fetchRequests.back();

                fetchRequest.signalID = signalID;
                fetchRequest.functionName = action->function.customFunctionName;

                for ( const auto &param : action->function.customFunctionParams )
                {
                    fetchRequest.args.emplace_back();

                    InspectionValue &arg = fetchRequest.args.back();

                    if ( param->nodeType == ExpressionNodeType::BOOLEAN )
                    {
                        arg = param->booleanValue;
                    }
                    else if ( param->nodeType == ExpressionNodeType::FLOAT )
                    {
                        arg = param->floatingValue;
                    }
                    else if ( param->nodeType == ExpressionNodeType::STRING )
                    {
                        arg = param->stringValue;
                    }
                    else
                    {
                        FWE_LOG_WARN( "Ignored fetch information due to unsupported action arguments "
                                      "(only boolean, double and string value are allowed)" );
                        isValid = false;
                        break;
                    }
                }

                if ( !isValid )
                {
                    break;
                }
            }

            if ( ( fetchInformation.condition == nullptr ) && ( fetchInformation.executionPeriodMs == 0 ) )
            {
                FWE_LOG_WARN( "Ignored fetch information due to unsupported time based fetch configuration" );
                isValid = false;
            }

            if ( !isValid )
            {
                // invalid FetchInformation => remove key fetchRequestID from map fetchMatrix->fetchRequests
                fetchMatrix.fetchRequests.erase( itFetchRequests );
                continue;
            }

            if ( fetchInformation.condition == nullptr )
            {
                // time-based fetch configuration
                PeriodicalFetchParameters &periodicalFetchParameters =
                    fetchMatrix.periodicalFetchRequestSetup[fetchRequestID];

                periodicalFetchParameters.fetchFrequencyMs = fetchInformation.executionPeriodMs;
                // TODO: below parameters are not yet supported by the cloud and are ignored on edge
                periodicalFetchParameters.maxExecutionCount = fetchInformation.maxExecutionPerInterval;
                periodicalFetchParameters.maxExecutionCountResetPeriodMs = fetchInformation.executionIntervalMs;
            }
            else
            {
                // condition-based fetch configuration
                ConditionForFetch conditionForFetch;

                conditionForFetch.condition = fetchInformation.condition;
                conditionForFetch.triggerOnlyOnRisingEdge = fetchInformation.triggerOnlyOnRisingEdge;
                conditionForFetch.fetchRequestID = fetchRequestID;

                conditionWithCollectedData.fetchConditions.emplace_back( conditionForFetch );

                bool alwaysEvaluateCondition = false;
                buildExpressionNodeMapAndVector( fetchInformation.condition,
                                                 expressionNodeToIndexMap,
                                                 expressionNodes,
                                                 index,
                                                 conditionWithCollectedData.isStaticCondition,
                                                 alwaysEvaluateCondition );
            }

            for ( auto &signal : conditionWithCollectedData.signals )
            {
                if ( signal.signalID == signalID )
                {
                    signal.fetchRequestIDs.push_back( fetchRequestID );
                }
            }

            fetchRequestID++;
        }

#ifdef FWE_FEATURE_STORE_AND_FORWARD
        for ( const auto &partition : collectionScheme->getStoreAndForwardConfiguration() )
        {

            bool alwaysEvaluateCondition = false;
            ConditionForForward conditionForForward;

            conditionForForward.condition = partition.uploadOptions.conditionTree;
            conditionWithCollectedData.forwardConditions.emplace_back( conditionForForward );

            buildExpressionNodeMapAndVector( conditionForForward.condition,
                                             expressionNodeToIndexMap,
                                             expressionNodes,
                                             index,
                                             conditionWithCollectedData.isStaticCondition,
                                             alwaysEvaluateCondition );
        }
#endif
    }

    // re-build all ExpressionNodes (for storage) and set ExpressionNode pointer addresses appropriately
    std::size_t expressionNodeCount = expressionNodes.size();

    inspectionMatrix.expressionNodeStorage.resize( expressionNodeCount );

    for ( std::size_t i = 0U; i < expressionNodeCount; i++ )
    {
        inspectionMatrix.expressionNodeStorage[i].nodeType = expressionNodes[i]->nodeType;
        inspectionMatrix.expressionNodeStorage[i].floatingValue = expressionNodes[i]->floatingValue;
        inspectionMatrix.expressionNodeStorage[i].booleanValue = expressionNodes[i]->booleanValue;
        inspectionMatrix.expressionNodeStorage[i].stringValue = expressionNodes[i]->stringValue;
        inspectionMatrix.expressionNodeStorage[i].signalID = expressionNodes[i]->signalID;
        inspectionMatrix.expressionNodeStorage[i].function.windowFunction = expressionNodes[i]->function.windowFunction;
        inspectionMatrix.expressionNodeStorage[i].function.customFunctionName =
            expressionNodes[i]->function.customFunctionName;
        inspectionMatrix.expressionNodeStorage[i].function.customFunctionInvocationId =
            expressionNodes[i]->function.customFunctionInvocationId;

        for ( const auto &param : expressionNodes[i]->function.customFunctionParams )
        {
            uint32_t paramIndex = expressionNodeToIndexMap[param];

            inspectionMatrix.expressionNodeStorage[i].function.customFunctionParams.push_back(
                &inspectionMatrix.expressionNodeStorage[paramIndex] );
        }

        if ( expressionNodes[i]->left != nullptr )
        {
            uint32_t leftIndex = expressionNodeToIndexMap[expressionNodes[i]->left];
            inspectionMatrix.expressionNodeStorage[i].left = &inspectionMatrix.expressionNodeStorage[leftIndex];
        }

        if ( expressionNodes[i]->right != nullptr )
        {
            uint32_t rightIndex = expressionNodeToIndexMap[expressionNodes[i]->right];
            inspectionMatrix.expressionNodeStorage[i].right = &inspectionMatrix.expressionNodeStorage[rightIndex];
        }
    }

    for ( auto &conditionWithCollectedData : inspectionMatrix.conditions )
    {
        if ( conditionWithCollectedData.condition != nullptr )
        {
            uint32_t conditionIndex = expressionNodeToIndexMap[conditionWithCollectedData.condition];

            conditionWithCollectedData.condition = &inspectionMatrix.expressionNodeStorage[conditionIndex];
        }

        for ( auto &conditionForFetch : conditionWithCollectedData.fetchConditions )
        {
            uint32_t conditionIndex = expressionNodeToIndexMap[conditionForFetch.condition];

            conditionForFetch.condition = &inspectionMatrix.expressionNodeStorage[conditionIndex];
        }
#ifdef FWE_FEATURE_STORE_AND_FORWARD
        for ( auto &conditionForForward : conditionWithCollectedData.forwardConditions )
        {
            if ( conditionForForward.condition != nullptr )
            {
                uint32_t conditionIndex = expressionNodeToIndexMap.at( conditionForForward.condition );
                conditionForForward.condition = &inspectionMatrix.expressionNodeStorage[conditionIndex];
            }
        }
#endif
    }
}

void
CollectionSchemeManager::extractCondition( InspectionMatrix &inspectionMatrix,
                                           const ICollectionScheme &collectionScheme,
                                           std::vector<const ExpressionNode *> &nodes,
                                           std::map<const ExpressionNode *, uint32_t> &nodeToIndexMap,
                                           uint32_t &index,
                                           const ExpressionNode *initialNode )
{
    ConditionWithCollectedData conditionData{};
    addConditionData( collectionScheme, conditionData );
    const ExpressionNode *currNode = initialNode;
    /* save the old root of this tree */
    conditionData.condition = currNode;

    buildExpressionNodeMapAndVector( currNode,
                                     nodeToIndexMap,
                                     nodes,
                                     index,
                                     conditionData.isStaticCondition,
                                     conditionData.alwaysEvaluateCondition );
    inspectionMatrix.conditions.emplace_back( conditionData );
}

void
CollectionSchemeManager::inspectionMatrixUpdater( std::shared_ptr<const InspectionMatrix> inspectionMatrix )
{
    mInspectionMatrixChangeListeners.notify( inspectionMatrix );
}

void
CollectionSchemeManager::fetchMatrixUpdater( std::shared_ptr<const FetchMatrix> fetchMatrix )
{
    mFetchMatrixChangeListeners.notify( fetchMatrix );
}

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
std::shared_ptr<StateTemplateList>
CollectionSchemeManager::lastKnownStateExtractor()
{
    auto extractedStateTemplates = std::make_shared<StateTemplateList>();

    for ( auto &stateTemplate : mStateTemplates )
    {
        if ( stateTemplate.second->decoderManifestID != mCurrentDecoderManifestID )
        {
            FWE_LOG_INFO( "Decoder manifest out of sync: " + stateTemplate.second->decoderManifestID + " vs. " +
                          mCurrentDecoderManifestID );
            continue;
        }

        // Intentionally copy it because we will need to modify the signals
        auto newStateTemplate = std::make_shared<StateTemplateInformation>( *stateTemplate.second );
        for ( auto &signal : newStateTemplate->signals )
        {
            signal.signalType = getSignalType( signal.signalID );
        }

        extractedStateTemplates->emplace_back( newStateTemplate );
    }

    return extractedStateTemplates;
}

void
CollectionSchemeManager::lastKnownStateUpdater( std::shared_ptr<StateTemplateList> stateTemplates )
{
    mStateTemplatesChangeListeners.notify( stateTemplates );
}
#endif

} // namespace IoTFleetWise
} // namespace Aws
