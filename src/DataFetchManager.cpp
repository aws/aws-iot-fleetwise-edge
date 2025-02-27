// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/DataFetchManager.h"
#include "aws/iotfleetwise/DataFetchManagerAPITypes.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/QueueTypes.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

DataFetchManager::DataFetchManager( const std::shared_ptr<FetchRequestQueue> fetchQueue )
    : mFetchRequestTimer()
    , mFetchQueue( fetchQueue )
{
}

bool
DataFetchManager::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( false );
    if ( !mThread.create( [this]() {
             this->doWork();
         } ) )
    {
        FWE_LOG_TRACE( "Data Fetch Manager Thread failed to start" );
    }
    else
    {
        FWE_LOG_TRACE( "Data Fetch Manager Thread started" );
        mThread.setThreadName( "fwDFMng" );
    }

    return mThread.isActive() && mThread.isValid();
}

bool
DataFetchManager::stop()
{
    if ( ( !mThread.isValid() ) || ( !mThread.isActive() ) )
    {
        return true;
    }
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    FWE_LOG_TRACE( "DataFetchManager request stop" );
    mWait.notify();
    mThread.release();
    FWE_LOG_TRACE( "Stop finished" );
    mShouldStop.store( false, std::memory_order_relaxed );
    return !mThread.isActive();
}

bool
DataFetchManager::shouldStop()
{
    return mShouldStop.load( std::memory_order_relaxed );
}

void
DataFetchManager::doWork()
{
    while ( !shouldStop() )
    {
        uint64_t minTimeToWaitMs = UINT64_MAX;
        if ( mUpdatedFetchMatrixAvailable )
        {
            std::lock_guard<std::mutex> lock( mFetchMatrixMutex );
            mUpdatedFetchMatrixAvailable = false;
            mCurrentFetchMatrix = mFetchMatrix;
        }

        if ( mCurrentFetchMatrix )
        {
            mFetchQueue->consumeAll( [this]( FetchRequestID &reqID ) -> FetchErrorCode {
                return executeFetch( reqID );
            } );

            // Iterate over the fetch matrix and schedule periodic requests
            auto currentTime = mClock->monotonicTimeSinceEpochMs();
            for ( const auto &periodicalRequest : mCurrentFetchMatrix->periodicalFetchRequestSetup )
            {
                auto requestID = periodicalRequest.first;
                auto &executionInfo = mLastExecutionInformation[requestID];

                // Check if it's time to execute this request
                if ( ( executionInfo.lastExecutionMonotonicTimeMs == 0 ) ||
                     ( ( currentTime - executionInfo.lastExecutionMonotonicTimeMs ) >=
                       periodicalRequest.second.fetchFrequencyMs ) )
                {
                    // TODO: max executions and reset interval parameters are not yet supported by the cloud and are
                    // ignored on edge Push the request to the queue
                    executeFetch( requestID );
                    // Update execution info
                    executionInfo.lastExecutionMonotonicTimeMs = currentTime;
                }
                // Calculate time to next execution for this request
                uint64_t timeToNextExecution =
                    executionInfo.lastExecutionMonotonicTimeMs + periodicalRequest.second.fetchFrequencyMs;
                minTimeToWaitMs = std::min( minTimeToWaitMs, timeToNextExecution - currentTime );
            }
        }

        if ( minTimeToWaitMs < UINT64_MAX )
        {
            FWE_LOG_TRACE( "Waiting for: " + std::to_string( minTimeToWaitMs ) + " ms." );
            mWait.wait( static_cast<uint32_t>( minTimeToWaitMs ) );
        }
        else
        {
            mWait.wait( Signal::WaitWithPredicate );
        }
    }
}

void
DataFetchManager::onNewFetchRequestAvailable()
{
    mWait.notify();
}

void
DataFetchManager::onChangeFetchMatrix( std::shared_ptr<const FetchMatrix> fetchMatrix )
{
    std::lock_guard<std::mutex> lock( mFetchMatrixMutex );
    mFetchMatrix = fetchMatrix;
    mUpdatedFetchMatrixAvailable = true;
    FWE_LOG_INFO( "Fetch Matrix updated" );
    mWait.notify();
}

FetchErrorCode
DataFetchManager::executeFetch( const FetchRequestID &fetchRequestID )
{
    if ( shouldStop() )
    {
        // Abort fetch executions if thread needs to stop
        return FetchErrorCode::NOT_IMPLEMENTED;
    }
    if ( mFetchMatrix == nullptr )
    {
        return FetchErrorCode::NOT_IMPLEMENTED;
    }
    auto fetchRequestIterator = mFetchMatrix->fetchRequests.find( fetchRequestID );
    if ( fetchRequestIterator == mFetchMatrix->fetchRequests.end() )
    {
        FWE_LOG_ERROR( "Unknown FetchRequestID : " + std::to_string( fetchRequestID ) );
        return FetchErrorCode::SIGNAL_NOT_FOUND;
    }

    if ( fetchRequestIterator->second.empty() )
    {
        FWE_LOG_ERROR( "No actions specified for FetchRequestID : " + std::to_string( fetchRequestID ) );
        return FetchErrorCode::SIGNAL_NOT_FOUND;
    }

    for ( const auto &request : fetchRequestIterator->second )
    {
        auto functionIt = mSupportedCustomFetchFunctionsMap.find( request.functionName );
        if ( functionIt == mSupportedCustomFetchFunctionsMap.end() )
        {
            FWE_LOG_ERROR( "Unknown Custom function : " + request.functionName );
            continue;
        }

        FWE_LOG_TRACE( "Dispatched fetch request ID " + std::to_string( fetchRequestID ) + " for SignalID " +
                       std::to_string( request.signalID ) );
        auto result = functionIt->second( request.signalID, fetchRequestID, request.args );

        if ( ( result != FetchErrorCode::SUCCESSFUL ) && ( result != FetchErrorCode::REQUESTED_TO_STOP ) )
        {
            FWE_LOG_ERROR( "Failed to execute Custom function: " + request.functionName + " for SignalID " +
                           std::to_string( request.signalID ) );
            return result;
        }
    }

    return FetchErrorCode::SUCCESSFUL;
}

void
DataFetchManager::registerCustomFetchFunction( const std::string &functionName, CustomFetchFunction function )
{
    mSupportedCustomFetchFunctionsMap[functionName] = std::move( function );
    FWE_LOG_TRACE( "Registered custom function for fetch " + functionName );
}

bool
DataFetchManager::isAlive()
{
    return mThread.isValid() && mThread.isActive();
}

DataFetchManager::~DataFetchManager()
{
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
}

} // namespace IoTFleetWise
} // namespace Aws
