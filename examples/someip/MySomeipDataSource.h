// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "MySomeipInterfaceWrapper.h"
#include "v1/commonapi/MySomeipInterfaceProxy.hpp"
#include <atomic>
#include <aws/iotfleetwise/Clock.h>
#include <aws/iotfleetwise/ClockHandler.h>
#include <aws/iotfleetwise/IDecoderDictionary.h>
#include <aws/iotfleetwise/NamedSignalDataSource.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

class MySomeipDataSource
{
public:
    MySomeipDataSource( std::shared_ptr<MySomeipInterfaceWrapper> mySomeipInterfaceWrapper,
                        std::shared_ptr<Aws::IoTFleetWise::NamedSignalDataSource> namedSignalDataSource,
                        uint32_t cyclicUpdatePeriodMs );

    ~MySomeipDataSource();

    bool connect();

    MySomeipDataSource( const MySomeipDataSource & ) = delete;
    MySomeipDataSource &operator=( const MySomeipDataSource & ) = delete;
    MySomeipDataSource( MySomeipDataSource && ) = delete;
    MySomeipDataSource &operator=( MySomeipDataSource && ) = delete;

private:
    std::shared_ptr<MySomeipInterfaceWrapper> mMySomeipInterfaceWrapper;
    std::shared_ptr<Aws::IoTFleetWise::NamedSignalDataSource> mNamedSignalDataSource;
    std::shared_ptr<v1::commonapi::MySomeipInterfaceProxy<>> mProxy;
    std::shared_ptr<const Aws::IoTFleetWise::Clock> mClock = Aws::IoTFleetWise::ClockHandler::getClock();
    std::shared_ptr<const Aws::IoTFleetWise::CustomDecoderDictionary> mDecoderDictionary;

    uint32_t mCyclicUpdatePeriodMs{};
    std::thread mThread;
    std::atomic<bool> mShouldStop{};
    std::mutex mLastValMutex;

    uint32_t mXSubscription{};
    bool mLastXValAvailable{};
    int32_t mLastXVal{};

    void pushXValue( const int32_t &val );
};
