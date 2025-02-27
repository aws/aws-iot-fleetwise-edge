// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Listener.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <algorithm>
#include <boost/optional/optional.hpp>
#include <memory>
#include <mutex>
#include <queue>

namespace Aws
{
namespace IoTFleetWise
{

// Thread-safe queue with mutex
template <typename T>
struct LockedQueue
{
public:
    /**
     * @param maxSize the maximum number of elements that can be stored in the queue. If reached, any new data will be
     * discarded.
     * @param queueName a name to identify the queue, mostly for logging and metrics purposes.
     * @param traceMetric the metric that should be emitted when the number of elements change.
     * @param notifyOnEveryNumOfElements notify the listeners only when the queue grows by this number of elements.
     * This is useful to avoid very busy queues notify the listeners too often. This way, the queue can notify
     * listeners only when data is being produced much faster than consumed.
     */
    LockedQueue( size_t maxSize,
                 std::string queueName,
                 const boost::optional<TraceAtomicVariable> traceMetric = boost::none,
                 size_t notifyOnEveryNumOfElements = 1 )
        : mMaxSize( maxSize )
        , mQueueName( std::move( queueName ) )
        , mTraceMetric( traceMetric )
        , mNotifyOnEveryNumOfElements( notifyOnEveryNumOfElements )
    {
    }

    bool
    push( const T &element )
    {
        std::lock_guard<std::mutex> lock( mMutex );
        if ( ( mQueue.size() + 1 ) > mMaxSize )
        {
            FWE_LOG_WARN( "Queue " + mQueueName + " is full" )
            return false;
        }
        mQueue.push( element );
        if ( mTraceMetric.has_value() )
        {
            TraceModule::get().incrementAtomicVariable( mTraceMetric.get() );
        }

        if ( ( mQueue.size() % mNotifyOnEveryNumOfElements ) == 0 )
        {
            mListeners.notify();
        }

        return true;
    }

    bool
    pop( T &element )
    {
        std::lock_guard<std::mutex> lock( mMutex );
        if ( mQueue.empty() )
        {
            return false;
        }
        element = mQueue.front();
        mQueue.pop();
        if ( mTraceMetric.has_value() )
        {
            TraceModule::get().decrementAtomicVariable( mTraceMetric.get() );
        }
        return true;
    }

    template <typename Functor>
    size_t
    consumeAll( const Functor &functor )
    {
        size_t consumed = 0;
        T element;
        while ( pop( element ) )
        {
            functor( element );
            consumed++;
        }
        return consumed;
    }

    /**
     * @brief Register a callback to be called when new data is pushed to the queue
     *
     * This can be used, for example, to wake up a thread that is interested in processing this data.
     */
    void
    subscribeToNewDataAvailable( std::function<void()> callback )
    {
        mListeners.subscribe( callback );
    }

    // coverity[misra_cpp_2008_rule_14_7_1_violation] Required in unit tests
    bool
    isEmpty()
    {
        std::lock_guard<std::mutex> lock( mMutex );
        return mQueue.empty();
    }

private:
    std::mutex mMutex;
    size_t mMaxSize;
    std::queue<T> mQueue;
    ThreadSafeListeners<std::function<void()>> mListeners;
    std::string mQueueName;
    boost::optional<TraceAtomicVariable> mTraceMetric;
    size_t mNotifyOnEveryNumOfElements;
};

// This queue wrapper is used to support one/multiple producers to multiple queues logic
// It will push an element to all registered thread-safe queues
template <typename T>
struct LockedQueueDistributor
{
public:
    void
    push( const T &&element )
    {
        for ( auto queue : mLockedQueues )
        {
            queue->push( element );
        }
    }
    void
    registerQueue( const std::shared_ptr<LockedQueue<T>> queuePtr )
    {
        mLockedQueues.push_back( queuePtr );
    }

private:
    std::vector<std::shared_ptr<LockedQueue<T>>> mLockedQueues;
};

} // namespace IoTFleetWise
} // namespace Aws
