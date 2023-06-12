// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "CustomDataSource.h"
#include "Timer.h"
#include <mutex>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using namespace Aws::IoTFleetWise::Platform::Linux;
using namespace Aws::IoTFleetWise::VehicleNetwork;
using namespace Aws::IoTFleetWise::DataInspection;

/**
 * To implement a custom data source create a new class and inherit from CustomDataSource
 * then call setFilter() then start() and provide an implementation for pollData
 */
class ExternalGpsSource : public CustomDataSource
{
public:
    /**
     *     @param signalBufferPtr the signal buffer is used pass extracted data
     */
    ExternalGpsSource( SignalBufferPtr signalBufferPtr );
    /**
     * Initialize ExternalGpsSource and set filter for CustomDataSource
     *
     * @param canChannel the CAN channel used in the decoder manifest
     * @param canRawFrameId the CAN message Id used in the decoder manifest
     * @param latitudeStartBit the startBit used in the decoder manifest for the latitude signal
     * @param longitudeStartBit the startBit used in the decoder manifest for the longitude signal
     *
     * @return on success true otherwise false
     */
    bool init( CANChannelNumericID canChannel,
               CANRawFrameID canRawFrameId,
               uint16_t latitudeStartBit,
               uint16_t longitudeStartBit );

    void setLocation( double latitude, double longitude );

    static constexpr const char *CAN_CHANNEL_NUMBER = "canChannel";
    static constexpr const char *CAN_RAW_FRAME_ID = "canFrameId";
    static constexpr const char *LATITUDE_START_BIT = "latitudeStartBit";
    static constexpr const char *LONGITUDE_START_BIT = "longitudeStartBit";

protected:
    void pollData() override;
    const char *getThreadName() override;

private:
    static bool validLatitude( double latitude );
    static bool validLongitude( double longitude );

    static const uint32_t CYCLIC_LOG_PERIOD_MS = 1000;

    uint16_t mLatitudeStartBit = 0;
    uint16_t mLongitudeStartBit = 0;

    SignalBufferPtr mSignalBufferPtr;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    CANChannelNumericID mCanChannel{ INVALID_CAN_SOURCE_NUMERIC_ID };
    CANRawFrameID mCanRawFrameId{ 0 };
};
} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
