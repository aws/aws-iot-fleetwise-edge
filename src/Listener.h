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
 */
template <typename ThreadListener>
class ThreadListeners
{

private:
    template <typename... Args>
    using CallBackFunction = void ( ThreadListener::* )( Args... );

public:
    ThreadListeners() = default;
    virtual ~ThreadListeners()
    {
        MutexLock lock( mMutex );
        mContainer.clear();
    }
    ThreadListeners( const ThreadListeners & ) = delete;
    ThreadListeners &operator=( const ThreadListeners & ) = delete;
    ThreadListeners( ThreadListeners && ) = delete;
    ThreadListeners &operator=( ThreadListeners && ) = delete;

    /**
     * @brief Subscribe the listener instance to notifications from this thread.
     * @param listener Listener instance that will invoked when notifyListeners is called.
     * @return True if the listener instance has been subscribed, False if
     * it's already subscribed.
     */
    bool
    subscribeListener( ThreadListener *listener )
    {
        MutexLock lock( mMutex );

        ListenerContainer &container = getContainer();
        const auto &containerIterator = std::find( container.begin(), container.end(), listener );
        if ( containerIterator == container.end() )
        {
            container.emplace_back( listener );
            mModified = mCopied;
            return true;
        }
        else
        {
            return false;
        }
    }

    /**
     * @brief unSubscribe a Listener  the listener instance from notifications to this thread.
     * @param listener Listener instance that will stop receiving notifications.
     * @return True if the listener instance has been removed, False if
     * it was not subscribed in the first place.
     */
    bool
    unSubscribeListener( ThreadListener *listener )
    {
        MutexLock lock( mMutex );

        ListenerContainer &container = getContainer();
        const auto &containerIterator = std::find( container.begin(), container.end(), listener );
        if ( containerIterator != container.end() )
        {
            container.erase( containerIterator );
            mModified = mCopied;
            return true;
        }
        else
        {
            return false;
        }
    }

    /**
     * @brief Notify all listeners that are observing this object state.
     */
    template <typename... Args>
    void
    notifyListeners( CallBackFunction<Args...> callBackFunction, Args... args ) const
    {
        MutexLock lock( mMutex );

        ContainerInvocationState state( this );

        for ( const auto &listener : mContainer )
        {
            ( ( listener )->*callBackFunction )( args... );
        }
    }

private:
    // Container to store the list of listeners to this thread
    using ListenerContainer = std::vector<ThreadListener *>;
    // Mutex to protect the storage from concurrent reads and writes
    using MutexLock = std::lock_guard<std::mutex>;
    // Container for all listeners registered.
    mutable ListenerContainer mContainer;
    // Temporary container used during modification via subscribe/unsubscribe
    mutable ListenerContainer mTemporaryContainer;
    // Container state coordinating local swaps of the listener container
    // Active flag is set when Listener callbacks are being executed
    mutable bool mActive{ false };
    // Copied flag is set when the Listener Container is being backed up successfully after an invocation
    mutable bool mCopied{ false };
    // Modified flag is set when the Listener Container is being modified ( when a listener callback
    // is either added or removed.
    mutable bool mModified{ false };
    mutable typename std::mutex mMutex;

    // Manages the container storing the listeners in a way that
    // callback invocations is eventually consistent.
    struct ContainerInvocationState
    {
        ContainerInvocationState( ThreadListeners const *currentState )
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
        ThreadListeners const *mOriginalContainer;
    };

    ListenerContainer &
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
