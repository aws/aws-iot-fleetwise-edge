// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsBootstrap.h"
#include "AwsSDKMemoryManager.h"
#include "LoggingModule.h"
#include <aws/core/Aws.h>
#include <aws/crt/Api.h>
#include <aws/crt/io/Bootstrap.h>
#include <cstddef>
#include <functional>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

constexpr size_t NUM_THREADS = 1;
constexpr size_t MAX_HOSTS = 1;
constexpr size_t MAX_TTL = 5;

struct AwsBootstrap::Impl
{
    Impl()
    {
        // Enable for logging
        // mOptions.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Info;

        // We are using a memory manager that is not suitable for over-aligned types.
        // Since we do not use alignas in our codebase, we are OK.
        auto &memMgr = AwsSDKMemoryManager::getInstance();
        mOptions.memoryManagementOptions.memoryManager = &memMgr;

        auto clientBootstrapFn = [this]() -> std::shared_ptr<Aws::Crt::Io::ClientBootstrap> {
            // You need an event loop group to process IO events.
            // If you only have a few connections, 1 thread is ideal
            Aws::Crt::Io::EventLoopGroup eventLoopGroup( NUM_THREADS );
            if ( !eventLoopGroup )
            {
                auto errString = Crt::ErrorDebugString( eventLoopGroup.LastError() );
                auto errLog = errString != nullptr ? std::string( errString ) : std::string( "Unknown error" );
                FWE_LOG_ERROR( "Event Loop Group Creation failed with error " + errLog );
            }
            else
            {
                Aws::Crt::Io::DefaultHostResolver defaultHostResolver( eventLoopGroup, MAX_HOSTS, MAX_TTL );
                constexpr char ALLOCATION_TAG[] = "AWS-SDK";
                auto bootstrap = Aws::MakeShared<Aws::Crt::Io::ClientBootstrap>(
                    &ALLOCATION_TAG[0], eventLoopGroup, defaultHostResolver );
                mBootstrap = bootstrap.get();
                return bootstrap;
            }
            return std::shared_ptr<Aws::Crt::Io::ClientBootstrap>{ nullptr };
        };
        mOptions.ioOptions.clientBootstrap_create_fn = clientBootstrapFn;
        Aws::InitAPI( mOptions );
    }

    ~Impl()
    {
        Aws::ShutdownAPI( mOptions );
        FWE_LOG_TRACE( "AWS API ShutDown Completed" );
    }

    Impl( const Impl & ) = delete;
    Impl &operator=( const Impl & ) = delete;
    Impl( Impl && ) = delete;
    Impl &operator=( Impl && ) = delete;

    Aws::Crt::Io::ClientBootstrap *
    getClientBootStrap() const
    {
        return mBootstrap;
    }

    Aws::SDKOptions mOptions;

    /**
     * @brief Pointer to client bootstrap.
     * @note The lifecycle is managed by the SDK itself
     *
     */
    Aws::Crt::Io::ClientBootstrap *mBootstrap{ nullptr };
};

AwsBootstrap::AwsBootstrap()
{
    mImpl = std::make_unique<Impl>();
}

AwsBootstrap &
AwsBootstrap::getInstance()
{
    static AwsBootstrap boot;
    return boot;
}

Aws::Crt::Io::ClientBootstrap *
AwsBootstrap::getClientBootStrap()
{
    return mImpl->getClientBootStrap();
}

} // namespace IoTFleetWise
} // namespace Aws
