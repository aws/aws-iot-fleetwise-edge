// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RetryThread.h"
#include "LoggingModule.h"
#include <algorithm>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

std::atomic<int> RetryThread::fInstanceCounter( 0 ); // NOLINT Global atomic instance counter

// coverity[misra_cpp_2008_rule_5_2_10_violation] For std::atomic this must be performed in a single statement
// coverity[autosar_cpp14_m5_2_10_violation] For std::atomic this must be performed in a single statement
RetryThread::RetryThread( Retryable retryable, uint32_t startBackoffMs, uint32_t maxBackoffMs )
    : fRetryable( std::move( retryable ) )
    , fInstance( fInstanceCounter++ )
    , fStartBackoffMs( startBackoffMs )
    , fMaxBackoffMs( maxBackoffMs )
    , fCurrentWaitTime( 0 )
    , fShouldStop( false )
    , fRestart( false )
{
}

bool
RetryThread::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( fThreadMutex );
    // On multi core systems the shared variable fShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    fShouldStop.store( false );
    if ( !fThread.create( doWork, this ) )
    {
        FWE_LOG_TRACE( "Retry Thread failed to start" );
    }
    else
    {
        FWE_LOG_TRACE( "Retry Thread started" );
        fThread.setThreadName( "fwCNRetry" + std::to_string( fInstance ) );
    }

    return fThread.isValid();
}

void
RetryThread::restart()
{
    fRestart.store( true );
    fWait.notify();
}

bool
RetryThread::stop()
{
    if ( !fThread.isValid() )
    {
        return true;
    }

    std::lock_guard<std::mutex> lock( fThreadMutex );
    fShouldStop.store( true );
    FWE_LOG_TRACE( "Request stop" );
    fWait.notify();
    fThread.release();
    fShouldStop.store( false, std::memory_order_relaxed );
    return !fThread.isActive();
}

void
RetryThread::doWork( void *data )
{
    RetryThread *retryThread = static_cast<RetryThread *>( data );
    retryThread->fCurrentWaitTime = retryThread->fStartBackoffMs;
    RetryStatus result = RetryStatus::RETRY;
    while ( !retryThread->fShouldStop )
    {
        if ( ( result == RetryStatus::RETRY ) || retryThread->fRestart )
        {
            if ( retryThread->fRestart )
            {
                retryThread->fCurrentWaitTime = retryThread->fStartBackoffMs;
                retryThread->fRestart.store( false, std::memory_order_relaxed );
            }

            result = retryThread->fRetryable();
            if ( result != RetryStatus::RETRY )
            {
                FWE_LOG_TRACE( "Finished with code " + std::to_string( static_cast<int>( result ) ) );
                retryThread->fWait.wait( Signal::WaitWithPredicate );
                continue;
            }

            FWE_LOG_TRACE( "Current retry time is: " + std::to_string( retryThread->fCurrentWaitTime ) );
            retryThread->fWait.wait( retryThread->fCurrentWaitTime );
            // exponential backoff
            retryThread->fCurrentWaitTime = std::min( retryThread->fCurrentWaitTime * 2, retryThread->fMaxBackoffMs );
        }
        else
        {
            retryThread->fWait.wait( Signal::WaitWithPredicate );
        }
    }
    // If thread is shutdown without succeeding signal abort
    FWE_LOG_TRACE( "Stop thread with ABORT" );
}

} // namespace IoTFleetWise
} // namespace Aws
