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
#include <linux/can.h>
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
    CANDataConsumer( CANChannelNumericID channelId, SignalBufferPtr signalBufferPtr, CANBufferPtr canBufferPtr );
    ~CANDataConsumer() = default;

    CANDataConsumer( const CANDataConsumer & ) = delete;
    CANDataConsumer &operator=( const CANDataConsumer & ) = delete;
    CANDataConsumer( CANDataConsumer && ) = delete;
    CANDataConsumer &operator=( CANDataConsumer && ) = delete;

    void processMessage( std::shared_ptr<const CANDecoderDictionary> &dictionary,
                         const struct canfd_frame &message,
                         Timestamp timestamp );

private:
    /**
     * @brief Finds whether there exists a decoder method for a message id or not
     * If found, returns the method by reference
     * messageId is an input-output parameter that has the correct value
     * for the current message's id with either the MSB set or unset
     */
    bool findDecoderMethod( uint32_t &messageId,
                            const CANDecoderDictionary::CANMsgDecoderMethodType &decoderMethod,
                            CANMessageDecoderMethod &currentMessageDecoderMethod ) const;

    CANBufferPtr mCANBufferPtr;
    SignalBufferPtr mSignalBufferPtr;
    CANChannelNumericID mChannelId{ INVALID_CAN_SOURCE_NUMERIC_ID };
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
