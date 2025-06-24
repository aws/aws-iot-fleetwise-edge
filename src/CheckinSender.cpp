// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CheckinSender.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include <algorithm>
#include <boost/none.hpp>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

// Checkin retry interval. Used issue checkins to the cloud as soon as possible, set to 5 seconds
static constexpr uint32_t RETRY_CHECKIN_INTERVAL_IN_MILLISECOND = 5000;

CheckinSender::CheckinSender( std::shared_ptr<SchemaListener> schemaListener, uint32_t checkinIntervalMs )
    : mSchemaListener( std::move( schemaListener ) )
{
    if ( checkinIntervalMs > 0 )
    {
        mCheckinIntervalMs = checkinIntervalMs;
    }
}

CheckinSender::~CheckinSender()
{
    if ( isAlive() )
    {
        stop();
    }
}

void
CheckinSender::onCheckinDocumentsChanged( const std::vector<SyncID> &documents )
{
    std::lock_guard<std::mutex> lock( mCheckinDocumentsMutex );
    mCheckinDocuments = documents;
    mWait.notify();
}

bool
CheckinSender::start()
{
    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( mThreadMutex );
    // On multi core systems the shared variable mShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    mShouldStop.store( false );
    if ( !mThread.create( [this]() {
             this->doWork();
         } ) )
    {
        FWE_LOG_TRACE( "Checkin Thread failed to start" );
    }
    else
    {
        FWE_LOG_TRACE( "Checkin Thread started" );
        mThread.setThreadName( "fwCheckin" );
    }

    return mThread.isActive() && mThread.isValid();
}

void
CheckinSender::doWork()
{
    setTimeToSendNextCheckin( mClock->monotonicTimeSinceEpochMs() );

    while ( true )
    {
        if ( shouldStop() )
        {
            break;
        }

        auto timeToSendNextCheckinMs = getTimeToSendNextCheckin();

        if ( !timeToSendNextCheckinMs.has_value() )
        {
            auto timeToWaitMs = mCheckinIntervalMs;
            FWE_LOG_TRACE( "Spurious wake up. Still waiting for the previous checkin response.Sleeping again for: " +
                           std::to_string( timeToWaitMs ) + " ms" );
            mWait.wait( timeToWaitMs );
            continue;
        }

        auto currentTimeMs = mClock->monotonicTimeSinceEpochMs();

        if ( timeToSendNextCheckinMs.get() > currentTimeMs )
        {
            auto timeToWaitMs = static_cast<uint32_t>( timeToSendNextCheckinMs.get() - currentTimeMs );
            FWE_LOG_TRACE( "Spurious wake up. Time for next checkin not reached yet. Sleeping again for: " +
                           std::to_string( timeToWaitMs ) + " ms" );
            mWait.wait( timeToWaitMs );
            continue;
        }

        {
            std::unique_lock<std::mutex> lock( mCheckinDocumentsMutex );

            if ( !mCheckinDocuments.has_value() )
            {
                // coverity[LOCK:FALSE] unique_lock's destructor won't try to unlock again
                lock.unlock();
                FWE_LOG_TRACE( "List of checkin documents not available yet. Sleeping until it is available" );
                mWait.wait( Signal::WaitWithPredicate );
                continue;
            }

            auto &checkinDocuments = mCheckinDocuments.get();

            std::string checkinLogStr;
            for ( size_t i = 0; i < checkinDocuments.size(); i++ )
            {
                if ( i > 0 )
                {
                    checkinLogStr += ", ";
                }
                checkinLogStr += checkinDocuments[i];
            }
            FWE_LOG_TRACE( "CHECKIN: " + checkinLogStr );

            setTimeToSendNextCheckin( boost::none );
            mSchemaListener->sendCheckin( checkinDocuments, [this, currentTimeMs]( bool success ) {
                if ( success )
                {
                    setTimeToSendNextCheckin( currentTimeMs + mCheckinIntervalMs );
                }
                else
                {
                    uint32_t minimumCheckinInterval =
                        std::min( RETRY_CHECKIN_INTERVAL_IN_MILLISECOND, mCheckinIntervalMs );
                    setTimeToSendNextCheckin( mClock->monotonicTimeSinceEpochMs() + minimumCheckinInterval );
                    mWait.notify();
                }
            } );
        }

        mWait.wait( mCheckinIntervalMs );
    }
}

boost::optional<Timestamp>
CheckinSender::getTimeToSendNextCheckin()
{
    std::lock_guard<std::mutex> lock( mTimeToSendNextCheckinMutex );
    return mTimeToSendNextCheckin;
}

void
CheckinSender::setTimeToSendNextCheckin( boost::optional<Timestamp> timeToSendNextCheckin )
{
    std::lock_guard<std::mutex> lock( mTimeToSendNextCheckinMutex );
    mTimeToSendNextCheckin = timeToSendNextCheckin;
}

bool
CheckinSender::stop()
{
    if ( ( !mThread.isValid() ) || ( !mThread.isActive() ) )
    {
        return true;
    }
    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    FWE_LOG_TRACE( "Request stop" );
    mWait.notify();
    mThread.release();
    FWE_LOG_TRACE( "Stop finished" );
    mShouldStop.store( false, std::memory_order_relaxed );
    return !mThread.isActive();
}

bool
CheckinSender::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

bool
CheckinSender::isAlive()
{
    return mThread.isValid() && mThread.isActive();
}

} // namespace IoTFleetWise
} // namespace Aws
