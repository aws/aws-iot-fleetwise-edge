// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "CustomDataSource.h"
#include "SignalTypes.h"
#include "Timer.h"
#include <cstdint>
#include <memory>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * To implement a custom data source create a new class and inherit from CustomDataSource
 * then call setFilter() then start() and provide an implementation for pollData
 */
class IWaveGpsSource : public CustomDataSource
{
public:
    /**
     *     @param signalBufferPtr the signal buffer is used pass extracted data
     */
    IWaveGpsSource( SignalBufferPtr signalBufferPtr );
    /**
     * Initialize IWaveGpsSource and set filter for CustomDataSource
     *
     * @param pathToNmeaSource Path to the file/tty with the NMEA output with the GPS data
     * @param canChannel the CAN channel used in the decoder manifest
     * @param canRawFrameId the CAN message Id used in the decoder manifest
     * @param latitudeStartBit the startBit used in the decoder manifest for the latitude signal
     * @param longitudeStartBit the startBit used in the decoder manifest for the longitude signal
     *
     * @return on success true otherwise false
     */
    bool init( const std::string &pathToNmeaSource,
               CANChannelNumericID canChannel,
               CANRawFrameID canRawFrameId,
               uint16_t latitudeStartBit,
               uint16_t longitudeStartBit );

    bool connect();
    bool disconnect();

    static constexpr const char *PATH_TO_NMEA = "nmeaFilePath";
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

    /**
     * The NMEA protocol provides the position in $GPGGA in the following format
     * dddmm.mmmmmm where dd is the degree and the rest is the minute
     * @param dmm the value from NMEA raw parsed as double
     * @param positive true if north or east otherwise false
     *
     * @return the position in the DD (= Decimal degrees) format
     */
    static double convertDmmToDdCoordinates( double dmm, bool positive );

    static int extractLongAndLatitudeFromLine(
        const char *start, int limit, double &longitude, double &latitude, bool &north, bool &east );

    static const uint32_t CYCLIC_LOG_PERIOD_MS = 10000;
    static const uint32_t MAX_BYTES_READ_PER_POLL = 2048;

    uint16_t mLatitudeStartBit = 0;
    uint16_t mLongitudeStartBit = 0;

    int mFileHandle = -1;
    SignalBufferPtr mSignalBufferPtr;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    Timer mCyclicLoggingTimer;
    uint32_t mGpggaLineCounter = 0;
    uint32_t mValidCoordinateCounter = 0;
    std::string mPathToNmeaSource;
    CANChannelNumericID mCanChannel{ INVALID_CAN_SOURCE_NUMERIC_ID };
    CANRawFrameID mCanRawFrameId{};
    char mBuffer[MAX_BYTES_READ_PER_POLL]{};
};

} // namespace IoTFleetWise
} // namespace Aws
