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

// Includes
#include "CollectionSchemeManager.h"
#include "TraceModule.h"
#include <stack>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
void
CollectionSchemeManager::addConditionData( const ICollectionSchemePtr &collectionScheme,
                                           ConditionWithCollectedData &conditionData )
{
    conditionData.minimumPublishInterval = collectionScheme->getMinimumPublishIntervalMs();
    conditionData.afterDuration = collectionScheme->getAfterDurationMs();
    conditionData.includeActiveDtcs = collectionScheme->isActiveDTCsIncluded();
    conditionData.triggerOnlyOnRisingEdge = collectionScheme->isTriggerOnlyOnRisingEdge();
    conditionData.probabilityToSend = collectionScheme->getProbabilityToSend();

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
        conditionData.signals.emplace_back( inspectionSignal );
    }

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
            mLogger.warn( "CollectionSchemeManager::addConditionData",
                          "Invalid Interface ID provided:" + collectionCANFrames[i].interfaceID );
        }
        else
        {
            conditionData.canFrames.emplace_back( CANFrame );
        }
    }
    // Image capture data
    const std::vector<ImageCollectionInfo> &imageCollectionInfos = collectionScheme->getImageCaptureData();
    for ( const auto &imageInfo : imageCollectionInfos )
    {
        InspectionMatrixImageCollectionInfo imageSettings = {};
        imageSettings.deviceID = imageInfo.deviceID;
        switch ( imageInfo.collectionType )
        {
        case ImageCollectionType::TIME_BASED:
            imageSettings.collectionType = InspectionMatrixImageCollectionType::TIME_BASED;
            imageSettings.beforeDurationMs = imageInfo.beforeDurationMs;
            break;
        case ImageCollectionType::FRAME_BASED:
            imageSettings.collectionType = InspectionMatrixImageCollectionType::FRAME_BASED;
            break;

        default:
            break;
        }
        imageSettings.imageFormat = imageInfo.imageFormat;
        conditionData.imageCollectionInfos.emplace_back( imageSettings );
    }
    conditionData.includeImageCapture = !conditionData.imageCollectionInfos.empty();
    // The rest
    conditionData.metaData.compress = collectionScheme->isCompressionNeeded();
    conditionData.metaData.persist = collectionScheme->isPersistNeeded();
    conditionData.metaData.priority = collectionScheme->getPriority();
    conditionData.metaData.decoderID = collectionScheme->getDecoderManifestID();
    conditionData.metaData.collectionSchemeID = collectionScheme->getCollectionSchemeID();
}

void
CollectionSchemeManager::inspectionMatrixExtractor( const std::shared_ptr<InspectionMatrix> &inspectionMatrix )
{
    std::stack<const ExpressionNode *> nodeStack;
    std::map<const ExpressionNode *, uint32_t> nodeToIndexMap;
    std::vector<const ExpressionNode *> nodes;
    uint32_t index = 0;
    const ExpressionNode *currNode = nullptr;

    for ( auto it = mEnabledCollectionSchemeMap.begin(); it != mEnabledCollectionSchemeMap.end(); it++ )
    {
        ICollectionSchemePtr collectionScheme = it->second;
        ConditionWithCollectedData conditionData;
        addConditionData( collectionScheme, conditionData );

        currNode = collectionScheme->getCondition();
        /* save the old root of this tree */
        conditionData.condition = currNode;
        inspectionMatrix->conditions.emplace_back( conditionData );

        /*
         * The following lines traverse each tree and pack the node addresses into a vector
         * and build a map
         * any order to traverse the tree is OK, here we use in-order.
         */
        while ( currNode != nullptr )
        {
            nodeStack.push( currNode );
            currNode = currNode->left;
        }
        while ( !nodeStack.empty() )
        {
            currNode = nodeStack.top();
            nodeStack.pop();
            nodeToIndexMap[currNode] = index;
            nodes.emplace_back( currNode );
            index++;
            if ( currNode->right != nullptr )
            {
                currNode = currNode->right;
                while ( currNode != nullptr )
                {
                    nodeStack.push( currNode );
                    currNode = currNode->left;
                }
            }
        }
    }

    size_t count = nodes.size();
    /* now we have the count of all nodes from all collectionSchemes, allocate a vector for the output */
    inspectionMatrix->expressionNodeStorage.resize( count );
    /* copy from the old tree node and update left and right children pointers */
    for ( uint32_t i = 0; i < count; i++ )
    {
        inspectionMatrix->expressionNodeStorage[i].nodeType = nodes[i]->nodeType;
        inspectionMatrix->expressionNodeStorage[i].floatingValue = nodes[i]->floatingValue;
        inspectionMatrix->expressionNodeStorage[i].booleanValue = nodes[i]->booleanValue;
        inspectionMatrix->expressionNodeStorage[i].signalID = nodes[i]->signalID;
        inspectionMatrix->expressionNodeStorage[i].function = nodes[i]->function;

        if ( nodes[i]->left != nullptr )
        {
            uint32_t leftIndex = nodeToIndexMap[nodes[i]->left];
            inspectionMatrix->expressionNodeStorage[i].left = &inspectionMatrix->expressionNodeStorage[leftIndex];
        }
        else
        {
            inspectionMatrix->expressionNodeStorage[i].left = nullptr;
        }

        if ( nodes[i]->right != nullptr )
        {
            uint32_t rightIndex = nodeToIndexMap[nodes[i]->right];
            inspectionMatrix->expressionNodeStorage[i].right = &inspectionMatrix->expressionNodeStorage[rightIndex];
        }
        else
        {
            inspectionMatrix->expressionNodeStorage[i].right = nullptr;
        }
    }
    /* update the root of tree with new address */
    for ( uint32_t i = 0; i < inspectionMatrix->conditions.size(); i++ )
    {
        uint32_t newIndex = nodeToIndexMap[inspectionMatrix->conditions[i].condition];
        inspectionMatrix->conditions[i].condition = &inspectionMatrix->expressionNodeStorage[newIndex];
    }
}

void
CollectionSchemeManager::inspectionMatrixUpdater( const std::shared_ptr<const InspectionMatrix> &inspectionMatrix )
{
    notifyListeners<const std::shared_ptr<const InspectionMatrix> &>(
        &IActiveConditionProcessor::onChangeInspectionMatrix, inspectionMatrix );
}
} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
