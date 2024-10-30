// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionSchemeManager.h" // IWYU pragma: associated
#include "ICollectionScheme.h"
#include "LoggingModule.h"
#include <algorithm> // IWYU pragma: keep
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map> // IWYU pragma: keep
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
void
CollectionSchemeManager::updateRawDataBufferConfigComplexSignals(
    std::shared_ptr<Aws::IoTFleetWise::ComplexDataDecoderDictionary> complexDataDecoderDictionary,
    std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> &updatedSignals )
{
    if ( ( mDecoderManifest != nullptr ) && isCollectionSchemesInSyncWithDm() )
    {
        // Iterate through enabled collectionScheme lists to locate the signals and CAN frames to be collected
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
CollectionSchemeManager::addConditionData( const ICollectionSchemePtr &collectionScheme,
                                           ConditionWithCollectedData &conditionData )
{
    conditionData.metadata.compress = collectionScheme->isCompressionNeeded();
    conditionData.metadata.persist = collectionScheme->isPersistNeeded();
    conditionData.metadata.priority = collectionScheme->getPriority();
    conditionData.metadata.decoderID = collectionScheme->getDecoderManifestID();
    conditionData.metadata.collectionSchemeID = collectionScheme->getCollectionSchemeID();

    /*
     * use for loop to copy signalInfo and CANframe over to avoid error or memory issue
     * This is probably not the fastest way to get things done, but the safest way
     * since the object is not big, so not really slow
     */
    const std::vector<SignalCollectionInfo> &collectionSignals = collectionScheme->getCollectSignals();
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

    {
        const std::vector<CanFrameCollectionInfo> &collectionCANFrames = collectionScheme->getCollectRawCanFrames();
        for ( uint32_t i = 0; i < collectionCANFrames.size(); i++ )
        {
            InspectionMatrixCanFrameCollectionInfo CANFrame = {};
            CANFrame.frameID = collectionCANFrames[i].frameID;
            CANFrame.channelID = mCANIDTranslator.getChannelNumericID( collectionCANFrames[i].interfaceID );
            CANFrame.sampleBufferSize = collectionCANFrames[i].sampleBufferSize;
            CANFrame.minimumSampleIntervalMs = collectionCANFrames[i].minimumSampleIntervalMs;
            if ( CANFrame.channelID == INVALID_CAN_SOURCE_NUMERIC_ID )
            {
                FWE_LOG_WARN( "Invalid Interface ID provided: " + collectionCANFrames[i].interfaceID );
            }
            else
            {
                conditionData.canFrames.emplace_back( CANFrame );
            }
        }

        conditionData.minimumPublishIntervalMs = collectionScheme->getMinimumPublishIntervalMs();
        conditionData.afterDuration = collectionScheme->getAfterDurationMs();
        conditionData.includeActiveDtcs = collectionScheme->isActiveDTCsIncluded();
        conditionData.triggerOnlyOnRisingEdge = collectionScheme->isTriggerOnlyOnRisingEdge();
    }
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
CollectionSchemeManager::matrixExtractor( const std::shared_ptr<InspectionMatrix> &inspectionMatrix )
{
    std::map<const ExpressionNode *, uint32_t> expressionNodeToIndexMap;
    std::vector<const ExpressionNode *> expressionNodes;
    uint32_t index = 0U;

    if ( !isCollectionSchemesInSyncWithDm() )
    {
        return;
    }

    for ( const auto &enabledCollectionScheme : mEnabledCollectionSchemeMap )
    {
        ICollectionSchemePtr collectionScheme = enabledCollectionScheme.second;

        extractCondition( inspectionMatrix,
                          collectionScheme,
                          expressionNodes,
                          expressionNodeToIndexMap,
                          index,
                          collectionScheme->getCondition() );
    }

    // re-build all ExpressionNodes (for storage) and set ExpressionNode pointer addresses appropriately
    std::size_t expressionNodeCount = expressionNodes.size();

    inspectionMatrix->expressionNodeStorage.resize( expressionNodeCount );

    for ( std::size_t i = 0U; i < expressionNodeCount; i++ )
    {
        inspectionMatrix->expressionNodeStorage[i].nodeType = expressionNodes[i]->nodeType;
        inspectionMatrix->expressionNodeStorage[i].floatingValue = expressionNodes[i]->floatingValue;
        inspectionMatrix->expressionNodeStorage[i].booleanValue = expressionNodes[i]->booleanValue;
        inspectionMatrix->expressionNodeStorage[i].signalID = expressionNodes[i]->signalID;
        inspectionMatrix->expressionNodeStorage[i].function.windowFunction =
            expressionNodes[i]->function.windowFunction;
        if ( expressionNodes[i]->left != nullptr )
        {
            uint32_t leftIndex = expressionNodeToIndexMap[expressionNodes[i]->left];
            inspectionMatrix->expressionNodeStorage[i].left = &inspectionMatrix->expressionNodeStorage[leftIndex];
        }

        if ( expressionNodes[i]->right != nullptr )
        {
            uint32_t rightIndex = expressionNodeToIndexMap[expressionNodes[i]->right];
            inspectionMatrix->expressionNodeStorage[i].right = &inspectionMatrix->expressionNodeStorage[rightIndex];
        }
    }

    for ( auto &conditionWithCollectedData : inspectionMatrix->conditions )
    {
        if ( conditionWithCollectedData.condition != nullptr )
        {
            uint32_t conditionIndex = expressionNodeToIndexMap[conditionWithCollectedData.condition];

            conditionWithCollectedData.condition = &inspectionMatrix->expressionNodeStorage[conditionIndex];
        }
    }
}

void
CollectionSchemeManager::extractCondition( const std::shared_ptr<InspectionMatrix> &inspectionMatrix,
                                           const ICollectionSchemePtr &collectionScheme,
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
    inspectionMatrix->conditions.emplace_back( conditionData );
}

void
CollectionSchemeManager::inspectionMatrixUpdater( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix )
{
    mInspectionMatrixChangeListeners.notify( inspectionMatrix );
}

} // namespace IoTFleetWise
} // namespace Aws
