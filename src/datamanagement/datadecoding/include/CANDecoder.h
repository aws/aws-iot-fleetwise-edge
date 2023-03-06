// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "CANDataTypes.h"
#include "ClockHandler.h"
#include "IDecoderManifest.h"
#include "Timer.h"
#include <memory>
#include <unordered_set>
#include <vector>
namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using namespace Aws::IoTFleetWise::Platform::Linux;
/**
 * @brief CAN Bus decoder. Decodes a given raw message frame according
 * to the DBC schema
 */

class CANDecoder
{

public:
    /**
     * @brief Decode a given frameData of frameSize using the DBC format.
     * @param frameData pointer to the frame data
     * @param frameSize size in bytes of the frame.
     * @param format of the frame according to the DBC file.
     * @param signalIDsToCollect the id for the signals to be collected
     * @param decodedMessage result of the decoding.
     * @return True if the decoding is successful, False means that the decoding was
     * partially not successful
     */
    bool decodeCANMessage( const uint8_t *frameData,
                           size_t frameSize,
                           const CANMessageFormat &format,
                           const std::unordered_set<SignalID> &signalIDsToCollect,
                           CANDecodedMessage &decodedMessage );

    /**
     * @brief extracts a signal raw value from a frame.
     * @param frameData pointer to the frame data
     * @param signalDescription DBC Description of the signal.
     * @return 8 bytes representation of the physical value of the signal.
     */
    static int64_t extractSignalFromFrame( const uint8_t *frameData, const CANSignalFormat &signalDescription );

private:
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
