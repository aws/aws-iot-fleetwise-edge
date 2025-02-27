// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CANDataConsumer.h"
#include "aws/iotfleetwise/CANInterfaceIDTranslator.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/IDecoderDictionary.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "aws/iotfleetwise/VehicleDataSourceTypes.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief External CAN Bus implementation. Allows data from external in-process CAN bus sources to
 *        be ingested, for example when FWE is compiled as a shared-library.
 */
// coverity[cert_dcl60_cpp_violation] false positive - class only defined once
// coverity[autosar_cpp14_m3_2_2_violation] false positive - class only defined once
class ExternalCANDataSource
{
public:
    /**
     * @brief Construct CAN data source
     * @param canIdTranslator CAN interface to ID translator
     * @param consumer CAN data consumer
     */
    ExternalCANDataSource( const CANInterfaceIDTranslator &canIdTranslator, CANDataConsumer &consumer );
    ~ExternalCANDataSource() = default;

    ExternalCANDataSource( const ExternalCANDataSource & ) = delete;
    ExternalCANDataSource &operator=( const ExternalCANDataSource & ) = delete;
    ExternalCANDataSource( ExternalCANDataSource && ) = delete;
    ExternalCANDataSource &operator=( ExternalCANDataSource && ) = delete;

    /** Ingest CAN message.
     * @param interfaceId Network interface ID
     * @param timestamp Timestamp of CAN message in milliseconds since epoch, or zero if unknown.
     * @param messageId CAN message ID in Linux SocketCAN format
     * @param data CAN message data */
    void ingestMessage( const InterfaceID &interfaceId,
                        Timestamp timestamp,
                        uint32_t messageId,
                        const std::vector<uint8_t> &data );

    void onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                     VehicleDataSourceProtocol networkProtocol );

private:
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    std::mutex mDecoderDictMutex;
    std::shared_ptr<const CANDecoderDictionary> mDecoderDictionary;
    const CANInterfaceIDTranslator &mCanIdTranslator;
    CANDataConsumer &mConsumer;
    Timestamp mLastFrameTime{};
};

} // namespace IoTFleetWise
} // namespace Aws
