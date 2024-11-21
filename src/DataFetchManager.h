// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "DataFetchManagerAPITypes.h"
#include "Signal.h"
#include "SignalTypes.h"
#include "Thread.h"
#include "Timer.h"
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>

namespace Aws
{
namespace IoTFleetWise
{

class DataFetchManager
{
public:
    DataFetchManager();
    ~DataFetchManager();

    DataFetchManager( const DataFetchManager & ) = delete;
    DataFetchManager &operator=( const DataFetchManager & ) = delete;
    DataFetchManager( DataFetchManager && ) = delete;
    DataFetchManager &operator=( DataFetchManager && ) = delete;

    bool start();
    bool stop();
    bool isAlive();

    /**
     * @brief callback to be invoked to receive fetch request when condition triggered
     */
    void onFetchRequest( const FetchRequestID &fetchRequestID, const bool &evaluationResult );

    /**
     * @brief callback to be invoked when new fetch matrix is received
     */
    void onChangeFetchMatrix( std::shared_ptr<const FetchMatrix> fetchMatrix );

    /**
     * @brief Function to register known custom fetch function and their name
     */
    void registerCustomFetchFunction( const std::string &functionName, CustomFetchFunction function );

private:
    static void doWork( void *data );
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
    std::queue<FetchRequestID> mFetchQueue;
    std::mutex mFetchQueueMutex;
    size_t mFetchQueueMaxSize{ 1000 };

    std::unordered_map<std::string, CustomFetchFunction> mSupportedCustomFetchFunctionsMap;

    std::shared_ptr<const Clock> mClock{ ClockHandler::getClock() };
};

} // namespace IoTFleetWise
} // namespace Aws
