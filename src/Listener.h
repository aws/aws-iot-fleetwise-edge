// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <mutex>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Template utility implementing a thread safe Subject/Observer design pattern.
 *
 * It is intended to be used with callbacks defined as std::function.
 */
template <typename T>
class ThreadSafeListeners
{

public:
    ThreadSafeListeners() = default;
    virtual ~ThreadSafeListeners()
    {
        MutexLock lock( mMutex );
        mContainer.clear();
    }
    ThreadSafeListeners( const ThreadSafeListeners & ) = delete;
    ThreadSafeListeners &operator=( const ThreadSafeListeners & ) = delete;
    ThreadSafeListeners( ThreadSafeListeners && ) = delete;
    ThreadSafeListeners &operator=( ThreadSafeListeners && ) = delete;

    /**
     * @brief Subscribe the listener instance to notifications from this thread.
     * @param callback Callback that will invoked when notify is called.
     */
    void
    subscribe( T callback )
    {
        MutexLock lock( mMutex );

        CallbackContainer &container = getContainer();
        container.emplace_back( callback );
        mModified = mCopied;
    }

    /**
     * @brief Notify all listeners that are observing this object state.
     */
    template <typename... Args>
    void
    notify( Args... args ) const
    {
        MutexLock lock( mMutex );

        ContainerInvocationState state( this );

        for ( const auto &callback : mContainer )
        {
            callback( args... );
        }
    }

private:
    // Container to store the list of listeners to this thread
    using CallbackContainer = std::vector<T>;
    // Mutex to protect the storage from concurrent reads and writes
    using MutexLock = std::lock_guard<std::mutex>;
    // Container for all listeners registered.
    mutable CallbackContainer mContainer;
    // Temporary container used during modification via subscribe/unsubscribe
    mutable CallbackContainer mTemporaryContainer;
    // Container state coordinating local swaps of the listener container
    // Active flag is set when Listener callbacks are being executed
    mutable bool mActive{ false };
    // Copied flag is set when the Listener Container is being backed up successfully after an invocation
    mutable bool mCopied{ false };
    // Modified flag is set when the Listener Container is being modified ( when a listener callback
    // is either added or removed.
    mutable bool mModified{ false };
    mutable typename std::mutex mMutex;

    // Manages the container storing the callbacks in a way that
    // callback invocations is eventually consistent.
    struct ContainerInvocationState
    {
        ContainerInvocationState( ThreadSafeListeners const *currentState )
            : mPreviousState( currentState->mActive )
            , mOriginalContainer( currentState )
        {
            currentState->mActive = true;
        }

        ~ContainerInvocationState()
        {
            mOriginalContainer->mActive = mPreviousState;

            if ( !mOriginalContainer->mActive )
            {
                mOriginalContainer->backupContainer();
            }
        }

        ContainerInvocationState( const ContainerInvocationState & ) = delete;
        ContainerInvocationState &operator=( const ContainerInvocationState & ) = delete;
        ContainerInvocationState( ContainerInvocationState && ) = delete;
        ContainerInvocationState &operator=( ContainerInvocationState && ) = delete;

    private:
        bool mPreviousState;
        ThreadSafeListeners const *mOriginalContainer;
    };

    CallbackContainer &
    getContainer()
    {
        if ( mModified )
        {
            return mTemporaryContainer;
        }
        if ( mActive )
        {
            mModified = true;
            mCopied = true;
            mTemporaryContainer = mContainer;

            return mTemporaryContainer;
        }
        return mContainer;
    }

    void
    backupContainer() const
    {

        if ( mCopied )
        {
            if ( mModified )
            {
                mContainer = mTemporaryContainer;
                mModified = false;
            }

            mTemporaryContainer.clear();
            mCopied = false;
        }
    }
};

} // namespace IoTFleetWise
} // namespace Aws
