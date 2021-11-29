/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#pragma once

// Includes
#include <algorithm>
#include <mutex>
#include <vector>
namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
/**
 * @brief Template utility implementing a thread safe Subject/Observer design pattern.
 */
template <typename ThreadListener>
class ThreadListeners
{

private:
    template <typename... Args>
    struct CallBackFunction
    {
        typedef void ( ThreadListener::*type )( Args... );
    };

public:
    ThreadListeners()
        : mActive( false )
        , mCopied( false )
        , mModified( false )
    {
    }

    ~ThreadListeners()
    {
        MutexLock lock( mMutex );
        mContainer.clear();
    }

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
    notifyListeners( typename CallBackFunction<Args...>::type callBackFunction, Args... args ) const
    {
        MutexLock lock( mMutex );

        ContainerInvocationState state( this );

        for ( const auto &listener : mContainer )
        {
            ( ( listener )->*callBackFunction )( args... );
        }
    }

    /**
     * @brief Mutable size of the listeners count.
     */
    size_t
    size() const
    {
        MutexLock lock( mMutex );

        return mContainer.size();
    }

private:
    // Container to store the list of listeners to this thread
    typedef std::vector<ThreadListener *> ListenerContainer;
    // Mutex to protect the storage from concurrent reads and writes
    typedef typename std::lock_guard<std::recursive_mutex> MutexLock;
    // Container for all listeners registered.
    mutable ListenerContainer mContainer;
    // Temporary container used during modification via subscribe/unsubscribe
    mutable ListenerContainer mTemporaryContainer;
    // Container state coordinating local swaps of the listener container
    mutable bool mActive;
    mutable bool mCopied;
    mutable bool mModified;
    mutable typename std::recursive_mutex mMutex;

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

} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
