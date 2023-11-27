// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ICollectionScheme.h"
#include "collection_schemes.pb.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "SignalTypes.h"
#include <atomic>
#endif

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief DecoderManifestIngestion (PI = Schema) is the implementation of ICollectionScheme used by
 * Schema.
 */
class CollectionSchemeIngestion : public ICollectionScheme
{
public:
    CollectionSchemeIngestion() = default;
    ~CollectionSchemeIngestion() override;

    CollectionSchemeIngestion( const CollectionSchemeIngestion & ) = delete;
    CollectionSchemeIngestion &operator=( const CollectionSchemeIngestion & ) = delete;
    CollectionSchemeIngestion( CollectionSchemeIngestion && ) = delete;
    CollectionSchemeIngestion &operator=( CollectionSchemeIngestion && ) = delete;

    bool isReady() const override;

    bool build() override;

    bool copyData( std::shared_ptr<Schemas::CollectionSchemesMsg::CollectionScheme> protoCollectionSchemeMessagePtr );

    const std::string &getCollectionSchemeID() const override;

    const std::string &getDecoderManifestID() const override;

    uint64_t getStartTime() const override;

    uint64_t getExpiryTime() const override;

    uint32_t getAfterDurationMs() const override;

    bool isActiveDTCsIncluded() const override;

    bool isTriggerOnlyOnRisingEdge() const override;

    double getProbabilityToSend() const override;

    const Signals_t &getCollectSignals() const override;

    const RawCanFrames_t &getCollectRawCanFrames() const override;

    bool isPersistNeeded() const override;

    bool isCompressionNeeded() const override;

    uint32_t getPriority() const override;

    const ExpressionNode *getCondition() const override;

    uint32_t getMinimumPublishIntervalMs() const override;

    const ExpressionNode_t &getAllExpressionNodes() const override;

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    /**
     * @brief The internal PartialSignalIDs are generated here. This ID must be unique across all campaigns.
     * To avoid duplication this counter is used as the uppermost Bit INTERNAL_SIGNAL_ID_BITMASK must be set
     * the MSB can never be used
     */
    static std::atomic<uint32_t> mPartialSignalCounter;

    const PartialSignalIDLookup &getPartialSignalIdToSignalPathLookupTable() const override;

    S3UploadMetadata getS3UploadMetadata() const override;
#endif

private:
    /**
     * @brief The CollectionScheme message that will hold the deserialized proto.
     */
    std::shared_ptr<Schemas::CollectionSchemesMsg::CollectionScheme> mProtoCollectionSchemeMessagePtr;

    /**
     * @brief Flag which is true if proto binary data is processed into readable data structures.
     */
    bool mReady{ false };

    /**
     * @brief Vector of all the signals that need to be collected/monitored
     */
    Signals_t mCollectedSignals;

    /**
     * @brief Vector of all the CAN Messages that need to be collected/monitored
     */
    RawCanFrames_t mCollectedRawCAN;

    /**
     * @brief Expression Node Pointer to the Tree Root
     */
    ExpressionNode *mExpressionNode{ nullptr };

    /**
     * @brief Vector representing all of the ExpressionNode(s)
     */
    ExpressionNode_t mExpressionNodes;

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    /**
     * @brief unordered_map from partial signal ID to pair of signal path and signal ID
     */
    PartialSignalIDLookup mPartialSignalIDLookup;

    /**
     * @brief Required metadata for S3 upload
     */
    S3UploadMetadata mS3UploadMetadata;
#endif

    /**
     * @brief Function used to Flatten the Abstract Syntax Tree (AST)
     */
    ExpressionNode *serializeNode( const Schemas::CommonTypesMsg::ConditionNode &node,
                                   std::size_t &nextIndex,
                                   int remainingDepth );

    /**
     * @brief Helper function that returns all nodes in the AST by doing a recursive traversal
     *
     * @param node Root node of the AST
     * @param depth Used recursively to find the depth. Start with maximum depth.
     *
     * @return Returns the number of nodes in the AST
     */
    uint32_t getNumberOfNodes( const Schemas::CommonTypesMsg::ConditionNode &node, const int depth );

    /**
     * @brief  Private Local Function used by the serializeNode Function to return the used Function Type
     */
    static WindowFunction convertFunctionType(
        Schemas::CommonTypesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType function );

    /**
     * @brief Private Local Function used by the serializeNode Function to return the used Operator Type
     */
    static ExpressionNodeType convertOperatorType( Schemas::CommonTypesMsg::ConditionNode_NodeOperator_Operator op );

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    /**
     * @brief If for the Signal ID and path combination already an ID was generated return it. Otherwise generate a new.
     */
    PartialSignalID getOrInsertPartialSignalId( SignalID signalId, const Schemas::CommonTypesMsg::SignalPath &path );
#endif
};

} // namespace IoTFleetWise
} // namespace Aws
