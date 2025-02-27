// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "aws/iotfleetwise/NamedSignalDataSource.h"
#include "aws/iotfleetwise/Timer.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace Aws
{
namespace IoTFleetWise
{

class IWaveGpsSource
{
public:
    /**
     * @param namedSignalDataSource Named signal data source
     * @param pathToNmeaSource Path to the file/tty with the NMEA output with the GPS data
     * @param latitudeSignalName the signal name of the latitude signal
     * @param longitudeSignalName the signal name of the longitude signal
     * @param pollIntervalMs the poll interval to read the GPS position
     */
    IWaveGpsSource( std::shared_ptr<NamedSignalDataSource> namedSignalDataSource,
                    std::string pathToNmeaSource,
                    std::string latitudeSignalName,
                    std::string longitudeSignalName,
                    uint32_t pollIntervalMs );
    ~IWaveGpsSource();

    IWaveGpsSource( const IWaveGpsSource & ) = delete;
    IWaveGpsSource &operator=( const IWaveGpsSource & ) = delete;
    IWaveGpsSource( IWaveGpsSource && ) = delete;
    IWaveGpsSource &operator=( IWaveGpsSource && ) = delete;

    bool connect();

    static constexpr const char *PATH_TO_NMEA = "nmeaFilePath";
    static constexpr const char *LATITUDE_SIGNAL_NAME = "latitudeSignalName";
    static constexpr const char *LONGITUDE_SIGNAL_NAME = "longitudeSignalName";
    static constexpr const char *POLL_INTERVAL_MS = "pollIntervalMs";

private:
    void pollData();
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

    int mFileHandle = -1;
    Timer mCyclicLoggingTimer;
    uint32_t mGpggaLineCounter = 0;
    uint32_t mValidCoordinateCounter = 0;
    std::shared_ptr<NamedSignalDataSource> mNamedSignalDataSource;
    std::string mPathToNmeaSource;
    std::string mLatitudeSignalName;
    std::string mLongitudeSignalName;
    uint32_t mPollIntervalMs;
    char mBuffer[MAX_BYTES_READ_PER_POLL]{};
    std::thread mThread;
    std::atomic<bool> mShouldStop{};
};

} // namespace IoTFleetWise
} // namespace Aws
