// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Thread.h"
#include <string>
#include <sys/prctl.h>

namespace Aws
{
namespace IoTFleetWise
{

extern "C"
{
    static void *
    workerFunctionWrapperC( void *params )
    {
        return Thread::workerFunctionWrapper( params );
    }
}

bool
Thread::create( WorkerFunction workerFunction, void *execParam )
{
    mExecParams.mSelf = this;
    mExecParams.mWorkerFunction = workerFunction;
    mExecParams.mParams = execParam;

    mDone.store( false );
    mTerminateSignal = std::make_unique<Signal>();

    if ( pthread_create( &mThread, nullptr, workerFunctionWrapperC, &mExecParams ) != 0 )
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

    if ( ( mThread != 0U ) && ( pthread_join( mThread, nullptr ) != 0 ) )
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
Thread::setCurrentThreadName( const std::string &name )
{
    prctl( PR_SET_NAME, name.c_str(), 0, 0, 0 );
}

unsigned long
Thread::getThreadID() const
{
    return static_cast<unsigned long>( mThread );
}

} // namespace IoTFleetWise
} // namespace Aws
