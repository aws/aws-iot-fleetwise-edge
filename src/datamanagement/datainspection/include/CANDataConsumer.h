// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes

#include "CANDataTypes.h"
#include "CollectionInspectionAPITypes.h"
#include "IDecoderDictionary.h"
#include "SignalTypes.h"
#include "TimeTypes.h"
#include <cstdint>
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{
using namespace Aws::IoTFleetWise::Platform::Linux;
/**
 * @brief CAN Network Data Consumer impl, outputs data to an in-memory buffer.
 *        Operates in Polling mode.
 */
class CANDataConsumer
{
public:
    CANDataConsumer( SignalBufferPtr signalBufferPtr, CANBufferPtr canBufferPtr );
    ~CANDataConsumer() = default;

    CANDataConsumer( const CANDataConsumer & ) = delete;
    CANDataConsumer &operator=( const CANDataConsumer & ) = delete;
    CANDataConsumer( CANDataConsumer && ) = delete;
    CANDataConsumer &operator=( CANDataConsumer && ) = delete;

    void processMessage( CANChannelNumericID channelId,
                         std::shared_ptr<const CANDecoderDictionary> &dictionary,
                         uint32_t messageId,
                         const uint8_t *data,
                         size_t dataLength,
                         Timestamp timestamp );

private:
    /**
     * @brief Finds whether there exists a decoder method for a message id or not
     * If found, returns the method by reference
     * messageId is an input-output parameter that has the correct value
     * for the current message's id with either the MSB set or unset
     */
    static bool findDecoderMethod( CANChannelNumericID channelId,
                                   uint32_t &messageId,
                                   const CANDecoderDictionary::CANMsgDecoderMethodType &decoderMethod,
                                   CANMessageDecoderMethod &currentMessageDecoderMethod );

    CANBufferPtr mCANBufferPtr;
    SignalBufferPtr mSignalBufferPtr;
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
