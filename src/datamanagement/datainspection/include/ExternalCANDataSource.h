// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "CANDataConsumer.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "IActiveDecoderDictionaryListener.h"
#include "IDecoderDictionary.h"
#include "Signal.h"
#include "SignalTypes.h"
#include "TimeTypes.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

using namespace Aws::IoTFleetWise::Platform::Linux;

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

/**
 * @brief External CAN Bus implementation. Allows data from external in-process CAN bus sources to
 *        be ingested, for example when FWE is compiled as a shared-library.
 */
// coverity[cert_dcl60_cpp_violation] false positive - class only defined once
// coverity[autosar_cpp14_m3_2_2_violation] false positive - class only defined once
class ExternalCANDataSource : public IActiveDecoderDictionaryListener
{
public:
    /**
     * @brief Construct CAN data source
     * @param consumer CAN data consumer
     */
    ExternalCANDataSource( CANDataConsumer &consumer );
    ~ExternalCANDataSource() override = default;

    ExternalCANDataSource( const ExternalCANDataSource & ) = delete;
    ExternalCANDataSource &operator=( const ExternalCANDataSource & ) = delete;
    ExternalCANDataSource( ExternalCANDataSource && ) = delete;
    ExternalCANDataSource &operator=( ExternalCANDataSource && ) = delete;

    /** Ingest CAN message.
     * @param channelId CAN channel ID
     * @param timestamp Timestamp of CAN message in milliseconds since epoch, or zero if unknown.
     * @param messageId CAN message ID in Linux SocketCAN format
     * @param data CAN message data */
    void ingestMessage( CANChannelNumericID channelId,
                        Timestamp timestamp,
                        uint32_t messageId,
                        const std::vector<uint8_t> &data );

    void onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                     VehicleDataSourceProtocol networkProtocol ) override;

private:
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    std::mutex mDecoderDictMutex;
    std::shared_ptr<const CANDecoderDictionary> mDecoderDictionary;
    CANDataConsumer &mConsumer;
    Timestamp mLastFrameTime{};
};

} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
