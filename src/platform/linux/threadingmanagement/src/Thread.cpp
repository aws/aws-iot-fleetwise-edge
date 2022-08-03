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

#if defined( IOTFLEETWISE_LINUX )
// Includes

#include "Thread.h"
#include <string>
#include <sys/prctl.h>
#include <unistd.h>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
namespace Linux
{

bool
Thread::create( WorkerFunction workerFunction, void *execParam )
{
    mExecParams.mSelf = this;
    mExecParams.mWorkerFunction = workerFunction;
    mExecParams.mParams = execParam;

    mDone.store( false );
    mTerminateSignal = std::make_unique<Signal>();

    if ( pthread_create( &mThread, nullptr, Thread::workerFunctionWrapper, &mExecParams ) != 0 )
    {

        mThreadId = 0;
        return false;
    }

    mThreadId = getThreadID();
    return true;
}

bool
Thread::release()
{
    if ( !isValid() )
    {
        mDone.store( true );
        return true;
    }

    mThreadId = 0;

    mDone.store( true );
    // Wait till the Predicate
    mTerminateSignal->wait( Signal::WaitWithPredicate );

    if ( mThread != 0u && pthread_join( mThread, nullptr ) != 0 )
    {
        return false;
    }

    mThread = 0;
    return true;
}

bool
Thread::isActive() const
{
    return !mDone.load();
}

bool
Thread::isValid() const
{
    return static_cast<unsigned long>( mThread ) > 0;
}

void *
Thread::workerFunctionWrapper( void *params )
{
    // POSIX settings
    pthread_setcancelstate( PTHREAD_CANCEL_ENABLE, nullptr );
    pthread_setcanceltype( PTHREAD_CANCEL_ASYNCHRONOUS, nullptr );

    ThreadSettings *threadSettings = static_cast<ThreadSettings *>( params );
    Thread *self = threadSettings->mSelf;

    if ( !self->mDone.load() )
    {
        self->mExecParams.mWorkerFunction( self->mExecParams.mParams );
    }

    self->mDone.store( true );
    // Signal Termination so that the thread can be released.
    self->mTerminateSignal->notify();

    return nullptr;
}

void
Thread::setThreadName( const std::string &name ) // NOLINT(readability-make-member-function-const)
{

    pthread_setname_np( mThread, name.c_str() );
}

void
Thread::SetCurrentThreadName( const std::string &name )
{
    prctl( PR_SET_NAME, name.c_str(), 0, 0, 0 );
}

unsigned long
Thread::getThreadID() const
{
    return static_cast<unsigned long>( mThread );
}

} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
#endif // IOTFLEETWISE_LINUX
