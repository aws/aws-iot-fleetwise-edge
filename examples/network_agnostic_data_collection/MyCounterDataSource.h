// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <aws/iotfleetwise/Clock.h>
#include <aws/iotfleetwise/ClockHandler.h>
#include <aws/iotfleetwise/NamedSignalDataSource.h>
#include <aws/iotfleetwise/RawDataManager.h>
#include <cstdint>
#include <memory>
#include <thread>

class MyCounterDataSource
{
public:
    static constexpr const char *CONFIG_OPTION_1 = "myOption1";
    static constexpr const char *SIGNAL_NAME = "Vehicle.MyCounter";

    MyCounterDataSource( std::shared_ptr<Aws::IoTFleetWise::NamedSignalDataSource> namedSignalDataSource,
                         uint32_t configOption1,
                         Aws::IoTFleetWise::RawData::BufferManager *rawDataBufferManager );

    ~MyCounterDataSource();

private:
    std::shared_ptr<Aws::IoTFleetWise::NamedSignalDataSource> mNamedSignalDataSource;
    uint32_t mConfigOption1;
    std::shared_ptr<const Aws::IoTFleetWise::Clock> mClock = Aws::IoTFleetWise::ClockHandler::getClock();
    Aws::IoTFleetWise::RawData::BufferManager *mRawDataBufferManager;
    uint32_t mCounter{};
    std::thread mThread;
    std::atomic_bool mThreadShouldStop{ false };
};
