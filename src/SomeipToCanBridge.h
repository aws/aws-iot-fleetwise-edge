// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CANDataConsumer.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "IDecoderDictionary.h"
#include "SignalTypes.h"
#include "TimeTypes.h"
#include "VehicleDataSourceTypes.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vsomeip/vsomeip.hpp>

namespace Aws
{
namespace IoTFleetWise
{

class SomeipToCanBridge
{
public:
    SomeipToCanBridge( uint16_t someipServiceId,
                       uint16_t someipInstanceId,
                       uint16_t someipEventId,
                       uint16_t someipEventGroupId,
                       std::string someipApplicationName,
                       CANChannelNumericID canChannelId,
                       CANDataConsumer &canDataConsumer,
                       std::function<std::shared_ptr<vsomeip::application>( std::string )> createSomeipApplication,
                       std::function<void( std::string )> removeSomeipApplication );
    ~SomeipToCanBridge() = default;

    SomeipToCanBridge( const SomeipToCanBridge & ) = delete;
    SomeipToCanBridge &operator=( const SomeipToCanBridge & ) = delete;
    SomeipToCanBridge( SomeipToCanBridge && ) = delete;
    SomeipToCanBridge &operator=( SomeipToCanBridge && ) = delete;

    bool init();
    void disconnect();

    void onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                     VehicleDataSourceProtocol networkProtocol );

private:
    static constexpr uint8_t HEADER_SIZE = 12;
    uint16_t mSomeipServiceId;
    uint16_t mSomeipInstanceId;
    uint16_t mSomeipEventId;
    uint16_t mSomeipEventGroupId;
    std::string mSomeipApplicationName;
    CANChannelNumericID mCanChannelId;
    CANDataConsumer &mCanDataConsumer;
    std::function<std::shared_ptr<vsomeip::application>( std::string )> mCreateSomeipApplication;
    std::function<void( std::string )> mRemoveSomeipApplication;
    std::shared_ptr<vsomeip::application> mSomeipApplication;
    std::thread mSomeipThread;
    std::mutex mDecoderDictMutex;
    std::shared_ptr<const CANDecoderDictionary> mDecoderDictionary;
    Timestamp mLastFrameTime{};
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
};

} // namespace IoTFleetWise
} // namespace Aws
