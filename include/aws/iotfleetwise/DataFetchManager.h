// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/DataFetchManagerAPITypes.h"
#include "aws/iotfleetwise/Signal.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/Thread.h"
#include "aws/iotfleetwise/Timer.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Aws
{
namespace IoTFleetWise
{

class DataFetchManager
{
public:
    DataFetchManager( std::shared_ptr<FetchRequestQueue> fetchQueue );
    ~DataFetchManager();

    DataFetchManager( const DataFetchManager & ) = delete;
    DataFetchManager &operator=( const DataFetchManager & ) = delete;
    DataFetchManager( DataFetchManager && ) = delete;
    DataFetchManager &operator=( DataFetchManager && ) = delete;

    bool start();
    bool stop();
    bool isAlive();

    /**
     * @brief As soon as new fetch request is available in the queue call this to wakeup the thread
     * */
    void onNewFetchRequestAvailable();

    /**
     * @brief callback to be invoked when new fetch matrix is received
     */
    void onChangeFetchMatrix( std::shared_ptr<const FetchMatrix> fetchMatrix );

    /**
     * @brief Function to register known custom fetch function and their name
     */
    void registerCustomFetchFunction( const std::string &functionName, CustomFetchFunction function );

private:
    void doWork();
    bool shouldStop();

    /**
     * @brief calls corresponding custom function for this fetch request
     *
     * @param fetchRequestID fetch request to execute
     * @return FetchErrorCode success if execution was completed
     */
    FetchErrorCode executeFetch( const FetchRequestID &fetchRequestID );

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::mutex mThreadMutex;
    Signal mWait;

    Timer mFetchRequestTimer;
    std::shared_ptr<const FetchMatrix> mFetchMatrix;
    std::shared_ptr<const FetchMatrix> mCurrentFetchMatrix;
    std::mutex mFetchMatrixMutex;
    std::atomic<bool> mUpdatedFetchMatrixAvailable{ false };
    std::unordered_map<FetchRequestID, LastExecutionInfo> mLastExecutionInformation;
    std::shared_ptr<FetchRequestQueue> mFetchQueue;
    std::unordered_map<std::string, CustomFetchFunction> mSupportedCustomFetchFunctionsMap;

    std::shared_ptr<const Clock> mClock{ ClockHandler::getClock() };
};

} // namespace IoTFleetWise
} // namespace Aws
