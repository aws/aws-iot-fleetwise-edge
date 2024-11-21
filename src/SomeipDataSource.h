// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "ExampleSomeipInterfaceWrapper.h"
#include "IDecoderDictionary.h"
#include "NamedSignalDataSource.h"
#include "RawDataManager.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace Aws
{
namespace IoTFleetWise
{

class SomeipDataSource
{
public:
    SomeipDataSource( std::shared_ptr<ExampleSomeipInterfaceWrapper> exampleSomeipInterfaceWrapper,
                      std::shared_ptr<NamedSignalDataSource> namedSignalDataSource,
                      std::shared_ptr<RawData::BufferManager> rawDataBufferManager,
                      uint32_t cyclicUpdatePeriodMs );
    ~SomeipDataSource();

    bool init();

    SomeipDataSource( const SomeipDataSource & ) = delete;
    SomeipDataSource &operator=( const SomeipDataSource & ) = delete;
    SomeipDataSource( SomeipDataSource && ) = delete;
    SomeipDataSource &operator=( SomeipDataSource && ) = delete;

private:
    std::shared_ptr<ExampleSomeipInterfaceWrapper> mExampleSomeipInterfaceWrapper;
    std::shared_ptr<NamedSignalDataSource> mNamedSignalDataSource;
    std::shared_ptr<v1::commonapi::ExampleSomeipInterfaceProxy<>> mProxy;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    std::shared_ptr<const CustomDecoderDictionary> mDecoderDictionary;
    std::shared_ptr<RawData::BufferManager> mRawBufferManager;

    uint32_t mCyclicUpdatePeriodMs{};
    std::thread mThread;
    std::atomic<bool> mShouldStop{};
    std::mutex mLastValMutex;

    uint32_t mXSubscription{};
    bool mLastXValAvailable{};
    int32_t mLastXVal{};
    void pushXValue( const int32_t &val );

    uint32_t mA1Subscription{};
    bool mLastA1ValAvailable{};
    v1::commonapi::CommonTypes::a1Struct mLastA1Val;
    void pushA1Value( const v1::commonapi::CommonTypes::a1Struct &val );

    void pushStringSignalToNamedDataSource( const std::string &signalName, const std::string &stringValue );
};

} // namespace IoTFleetWise
} // namespace Aws
