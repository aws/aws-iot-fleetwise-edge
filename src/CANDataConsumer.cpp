// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CANDataConsumer.h"
#include "aws/iotfleetwise/CANDataTypes.h"
#include "aws/iotfleetwise/CANDecoder.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/EnumUtility.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/MessageTypes.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <linux/can.h>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

CANDataConsumer::CANDataConsumer( SignalBufferDistributor &signalBufferDistributor )
    : mSignalBufferDistributor{ signalBufferDistributor }
{
}

bool
CANDataConsumer::findDecoderMethod( CANChannelNumericID channelId,
                                    uint32_t &messageId,
                                    const CANDecoderDictionary::CANMsgDecoderMethodType &decoderMethod,
                                    CANMessageDecoderMethod &currentMessageDecoderMethod )
{
    auto outerMapIt = decoderMethod.find( channelId );
    if ( outerMapIt != decoderMethod.cend() )
    {
        auto it = outerMapIt->second.find( messageId );

        if ( it != outerMapIt->second.cend() )
        {
            currentMessageDecoderMethod = it->second;
            return true;
        }
        it = outerMapIt->second.find( messageId & CAN_EFF_MASK );

        if ( it != outerMapIt->second.cend() )
        {
            messageId = messageId & CAN_EFF_MASK;
            currentMessageDecoderMethod = it->second;
            return true;
        }
    }
    return false;
}

void
CANDataConsumer::processMessage( CANChannelNumericID channelId,
                                 const CANDecoderDictionary *dictionary,
                                 uint32_t messageId,
                                 const uint8_t *data,
                                 size_t dataLength,
                                 Timestamp timestamp )
{
    // Skip if the dictionary was invalidated during message processing:
    if ( dictionary == nullptr )
    {
        return;
    }
    TraceSection traceSection =
        ( ( channelId < static_cast<CANChannelNumericID>( toUType( TraceSection::CAN_DECODER_CYCLE_19 ) -
                                                          toUType( TraceSection::CAN_DECODER_CYCLE_0 ) ) )
              ? static_cast<TraceSection>( channelId + toUType( TraceSection::CAN_DECODER_CYCLE_0 ) )
              : TraceSection::CAN_DECODER_CYCLE_19 );
    TraceModule::get().sectionBegin( traceSection );
    // get decoderMethod from the decoder dictionary
    const auto &decoderMethod = dictionary->canMessageDecoderMethod;
    // a set of signalID specifying which signal to collect
    const auto &signalIDsToCollect = dictionary->signalIDsToCollect;
    // check if this CAN message ID on this CAN Channel has the decoder method
    CANMessageDecoderMethod currentMessageDecoderMethod;
    // The value of messageId may be changed by the findDecoderMethod function. This is a
    // workaround as the cloud as of now does not send extended id messages.
    // If the decoder method for this message is not found in
    // decoderMethod dictionary, we check for the same id without the MSB set.
    // The message id which has a decoderMethod gets passed into messageId
    if ( findDecoderMethod( channelId, messageId, decoderMethod, currentMessageDecoderMethod ) )
    {
        // format to be used for decoding
        const auto &format = currentMessageDecoderMethod.format;

        // Create Collected Data Frame
        CollectedDataFrame collectedDataFrame;
        if ( format.isValid() )
        {
            std::vector<CANDecodedSignal> decodedSignals;
            if ( !CANDecoder::decodeCANMessage( data, dataLength, format, signalIDsToCollect, decodedSignals ) )
            {
                if ( decodedSignals.empty() )
                {
                    FWE_LOG_WARN( "CAN Frame " + std::to_string( messageId ) + " decoding failed! " );
                }
                else
                {
                    FWE_LOG_WARN( "CAN Frame " + std::to_string( messageId ) + " decoding partially failed! " );
                }
            }

            // Create vector of Collected Signal Object
            CollectedSignalsGroup collectedSignalsGroup;
            for ( auto const &signal : decodedSignals )
            {
                // Create Collected Signal Object
                // Only add valid signals to the vector
                if ( signal.mSignalID != INVALID_SIGNAL_ID )
                {
                    collectedSignalsGroup.push_back( CollectedSignal::fromDecodedSignal(
                        signal.mSignalID, timestamp, signal.mPhysicalValue, signal.mSignalType ) );
                }
            }
            collectedDataFrame.mCollectedSignals = std::move( collectedSignalsGroup );
        }
        else
        {
            // The CAN Message format is not valid, report as warning
            FWE_LOG_WARN( "CANMessageFormat Invalid for format message id: " + std::to_string( format.mMessageID ) +
                          " can message id: " + std::to_string( messageId ) +
                          " on CAN Channel Id: " + std::to_string( channelId ) );
        }

        TraceModule::get().sectionEnd( traceSection );

        mSignalBufferDistributor.push( std::move( collectedDataFrame ) );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
