// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "CustomDataSource.h"
#include "SignalTypes.h"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * To implement a custom data source create a new class and inherit from CustomDataSource
 * then call setFilter() then start() and provide an implementation for pollData
 */
class AaosVhalSource : public CustomDataSource
{
public:
    /**
     *     @param signalBufferPtr the signal buffer is used pass extracted data
     */
    AaosVhalSource( SignalBufferPtr signalBufferPtr );
    /**
     * Initialize AaosVhalSource and set filter for CustomDataSource
     *
     * @param canChannel the CAN channel used in the decoder manifest
     * @param canRawFrameId the CAN message Id used in the decoder manifest
     *
     * @return on success true otherwise false
     */
    bool init( CANChannelNumericID canChannel, CANRawFrameID canRawFrameId );

    /**
     * Returns a vector of vehicle property info
     *
     * @return Vehicle property info, with each member containing an array with 4 values:
     * - Vehicle property ID
     * - Area index
     * - Result index
     * - Signal ID
     */
    std::vector<std::array<uint32_t, 4>> getVehiclePropertyInfo();

    /**
     * Set Vehicle property value
     *
     * @param signalId Signal ID
     * @param value Property value
     */
    void setVehicleProperty( SignalID signalId, double value );

    static constexpr const char *CAN_CHANNEL_NUMBER = "canChannel";
    static constexpr const char *CAN_RAW_FRAME_ID = "canFrameId";

protected:
    void pollData() override;
    const char *getThreadName() override;

private:
    SignalBufferPtr mSignalBufferPtr;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    CANChannelNumericID mCanChannel{ INVALID_CAN_SOURCE_NUMERIC_ID };
    CANRawFrameID mCanRawFrameId{ 0 };
};

} // namespace IoTFleetWise
} // namespace Aws
