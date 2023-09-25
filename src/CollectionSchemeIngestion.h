// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ICollectionScheme.h"
#include "collection_schemes.pb.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

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

    /**
     * @brief Function used to Flatten the Abstract Syntax Tree (AST)
     */
    ExpressionNode *serializeNode( const Schemas::CollectionSchemesMsg::ConditionNode &node,
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
    uint32_t getNumberOfNodes( const Schemas::CollectionSchemesMsg::ConditionNode &node, const int depth );

    /**
     * @brief  Private Local Function used by the serializeNode Function to return the used Function Type
     */
    static WindowFunction convertFunctionType(
        Schemas::CollectionSchemesMsg::ConditionNode_NodeFunction_WindowFunction_WindowType function );

    /**
     * @brief Private Local Function used by the serializeNode Function to return the used Operator Type
     */
    static ExpressionNodeType convertOperatorType(
        Schemas::CollectionSchemesMsg::ConditionNode_NodeOperator_Operator op );
};

} // namespace IoTFleetWise
} // namespace Aws
