// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "IActiveDecoderDictionaryListener.h"
#include "LoggingModule.h"
#include "Thread.h"

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
class CustomDataSource : public IActiveDecoderDictionaryListener
{
public:
    CustomDataSource() = default;
    // from IActiveDecoderDictionaryListener
    void onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                     VehicleDataSourceProtocol networkProtocol ) override;

    bool start();
    bool stop();

    /**
     * Use a specific CAN channel and message ID combination for this custom data source
     * Only if this message is collected by a collection scheme this custom data gets active
     *
     * @param canChannel the edge internal number correspondng to the 'interfaceId' defined in the DecoderManifest
     * @param canRawFrameId the 'messageId' defined in the DecoderManifest
     */
    void setFilter( CANChannelNumericID canChannel, CANRawFrameID canRawFrameId );

    /**
     * Get the unique SignalID, which is used to assign values to signals
     * Only call from inside doWork/pollData function
     *
     * @param startBit defined in the DecoderManifest in the message given to setFilter
     *
     * @return a signalId which can be use to set a value for this signal as if it was read on CAN. INVALID_SIGNAL_ID if
     * no signal is defined for this startBit in the DecoderManifest
     */
    SignalID getSignalIdFromStartBit( uint16_t startBit );

    ~CustomDataSource() override;
    CustomDataSource( const CustomDataSource & ) = delete;
    CustomDataSource &operator=( const CustomDataSource & ) = delete;
    CustomDataSource( CustomDataSource && ) = delete;
    CustomDataSource &operator=( CustomDataSource && ) = delete;

protected:
    virtual const char *getThreadName();

    /**
     * Will be called from inside doWork function when data should be polled
     * the interval this function should be called is defined in mPollIntervalMs
     */
    virtual void pollData() = 0;
    void
    setPollIntervalMs( uint32_t pollIntervalMs )
    {
        mPollIntervalMs = pollIntervalMs;
    };
    uint32_t mPollIntervalMs = DEFAULT_POLL_INTERVAL_MS;

    bool
    isRunning()
    {
        return mThread.isValid() && mThread.isActive();
    }

private:
    void matchDictionaryToFilter( const CANDecoderDictionary &dictionary,
                                  CANChannelNumericID canChannel,
                                  CANRawFrameID canRawFrameId );
    // Main work function that runs in new thread
    static void doWork( void *data );
    bool shouldStop() const;

    bool shouldSleep() const;

    LoggingModule mLogger;
    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::atomic<bool> mShouldSleep{ false };
    mutable std::mutex mThreadMutex;
    Platform::Linux::Signal mWait;
    CANChannelNumericID mCanChannel = INVALID_CAN_SOURCE_NUMERIC_ID;
    CANRawFrameID mCanRawFrameId = 0;

    CANMessageFormat mExtractedMessageFormat;
    std::atomic<bool> mNewMessageFormatExtracted{ false };
    CANMessageFormat mUsedMessageFormat;
    mutable std::mutex mExtractionOngoing;

    std::shared_ptr<const CANDecoderDictionary> lastReceivedDictionary;

    static const uint32_t DEFAULT_POLL_INTERVAL_MS = 50; // Default poll every 50ms data
};
} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws