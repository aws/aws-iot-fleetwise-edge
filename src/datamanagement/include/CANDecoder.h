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

#pragma once

// Includes
#include "CANDataTypes.h"
#include "ClockHandler.h"
#include "IDecoderManifest.h"
#include "LoggingModule.h"
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
using namespace Aws::IoTFleetWise::Platform;
/**
 * @brief CAN Bus decoder. Decodes a given raw message frame according
 * to the DBC schema
 */

class CANDecoder
{

public:
    CANDecoder();
    ~CANDecoder();
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
                           const std::unordered_set<SignalID> signalIDsToCollect,
                           CANDecodedMessage &decodedMessage );

    /**
     * @brief extracts a signal raw value from a frame.
     * @param frameData pointer to the frame data
     * @param signalDescription DBC Description of the signal.
     * @return 8 bytes representation of the physical value of the signal.
     */
    int64_t extractSignalFromFrame( const uint8_t *frameData, const CANSignalFormat &signalDescription );

private:
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
};

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
