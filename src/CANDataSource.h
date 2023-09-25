// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CANDataConsumer.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "IActiveDecoderDictionaryListener.h"
#include "IDecoderDictionary.h"
#include "Signal.h"
#include "SignalTypes.h"
#include "Thread.h"
#include "TimeTypes.h"
#include "Timer.h"
#include "VehicleDataSourceTypes.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <sys/socket.h> // IWYU pragma: keep

namespace Aws
{
namespace IoTFleetWise
{

// This timestamp is used when uploading data to the cloud
enum class CanTimestampType
{
    KERNEL_SOFTWARE_TIMESTAMP, // default and the best option in most scenarios
    KERNEL_HARDWARE_TIMESTAMP, // is not necessarily a unix epoch timestamp which will lead to problems and records
                               // potentially rejected by cloud
    POLLING_TIME, // fallback if selected value is 0. can lead to multiple can frames having the same timestamp and so
                  // being dropped by cloud
};

inline bool
stringToCanTimestampType( std::string const &timestampType, CanTimestampType &outTimestampType )
{
    if ( timestampType == "Software" )
    {
        outTimestampType = CanTimestampType::KERNEL_SOFTWARE_TIMESTAMP;
    }
    else if ( timestampType == "Hardware" )
    {
        outTimestampType = CanTimestampType::KERNEL_HARDWARE_TIMESTAMP;
    }
    else if ( timestampType == "Polling" )
    {
        outTimestampType = CanTimestampType::POLLING_TIME;
    }
    else
    {
        return false;
    }
    return true;
}

/**
 * @brief Linux CAN Bus implementation. Uses Raw Sockets to listen to CAN
 * data on 1 single CAN IF.
 */
// coverity[cert_dcl60_cpp_violation] false positive - class only defined once
// coverity[autosar_cpp14_m3_2_2_violation] false positive - class only defined once
class CANDataSource : public IActiveDecoderDictionaryListener
{
public:
    static constexpr int PARALLEL_RECEIVED_FRAMES_FROM_KERNEL = 10;
    static constexpr int DEFAULT_THREAD_IDLE_TIME_MS = 1000;

    /**
     * @brief Construct CAN data source
     * @param channelId CAN channel identifier
     * @param timestampTypeToUse which timestamp type should be used to tag the can frames, this timestamp will be
     * visible in the cloud
     * @param interfaceName SocketCAN interface name, e.g. vcan0
     * @param forceCanFD True to force CAN-FD mode, which will cause #connect to return an error if not available.
     * False to use CAN-FD if available.
     * @param threadIdleTimeMs Poll period of SocketCAN interface.
     * @param consumer CAN data consumer
     */
    CANDataSource( CANChannelNumericID channelId,
                   CanTimestampType timestampTypeToUse,
                   std::string interfaceName,
                   bool forceCanFD,
                   uint32_t threadIdleTimeMs,
                   CANDataConsumer &consumer );
    ~CANDataSource() override;

    CANDataSource( const CANDataSource & ) = delete;
    CANDataSource &operator=( const CANDataSource & ) = delete;
    CANDataSource( CANDataSource && ) = delete;
    CANDataSource &operator=( CANDataSource && ) = delete;

    bool init();
    bool connect();

    bool disconnect();

    bool isAlive();

    void onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                     VehicleDataSourceProtocol networkProtocol ) override;

private:
    // Start the bus thread
    bool start();
    // Stop the bus thread
    bool stop();
    // atomic state of the bus. If true, we should stop
    bool shouldStop() const;
    // Main work function. Listens on the socket for CAN Messages
    // and push data to the circular buffer.
    static void doWork( void *data );

    Timestamp extractTimestamp( struct msghdr *msgHeader );

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    mutable std::mutex mThreadMutex;
    Timer mTimer;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    int mSocket{ -1 };
    Signal mWait;
    uint32_t mIdleTimeMs{ DEFAULT_THREAD_IDLE_TIME_MS };
    uint64_t mReceivedMessages{ 0 };
    CanTimestampType mTimestampTypeToUse{ CanTimestampType::KERNEL_SOFTWARE_TIMESTAMP };
    bool mForceCanFD{ false };
    std::mutex mDecoderDictMutex;
    std::shared_ptr<const CANDecoderDictionary> mDecoderDictionary;
    CANChannelNumericID mChannelId{ INVALID_CAN_SOURCE_NUMERIC_ID };
    std::string mIfName;
    CANDataConsumer &mConsumer;
};

} // namespace IoTFleetWise
} // namespace Aws
