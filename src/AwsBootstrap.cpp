// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/AwsBootstrap.h"
#include "aws/iotfleetwise/LogLevel.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <aws/core/Aws.h>
#include <aws/core/utils/logging/FormattedLogSystem.h>
#include <aws/core/utils/logging/LogSystemInterface.h>
#include <aws/core/utils/memory/stl/AWSString.h>
#include <aws/core/utils/memory/stl/AWSStringStream.h>
#include <aws/crt/Api.h>
#include <aws/crt/io/Bootstrap.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

constexpr size_t NUM_THREADS = 1;
constexpr size_t MAX_HOSTS = 1;
constexpr size_t MAX_TTL = 5;

class CustomLogSystem : public Aws::Utils::Logging::FormattedLogSystem
{
public:
    using Aws::Utils::Logging::FormattedLogSystem::FormattedLogSystem;

    void
    LogStream( Aws::Utils::Logging::LogLevel logLevel,
               const char *tag,
               const Aws::OStringStream &message_stream ) override
    {
        std::stringstream stream;
        stream << tag << ": " << message_stream.str();

        switch ( logLevel )
        {
        case Aws::Utils::Logging::LogLevel::Error:
        case Aws::Utils::Logging::LogLevel::Fatal:
            FWE_LOG_ERROR( stream.str() )
            break;
        case Aws::Utils::Logging::LogLevel::Warn:
            FWE_LOG_WARN( stream.str() )
            break;
        case Aws::Utils::Logging::LogLevel::Info:
            FWE_LOG_INFO( stream.str() )
            break;
        case Aws::Utils::Logging::LogLevel::Debug:
        case Aws::Utils::Logging::LogLevel::Trace:
        default:
            FWE_LOG_TRACE( stream.str() )
            break;
        }
    }

    void
    ProcessFormattedStatement( Aws::String &&statement ) override
    {
        // Because we overrode FormattedLogSystem::LogStream, ProcessFormattedStatement will only be
        // called by FormattedLogSystem::Log, which isn't normally used. FormattedLogSystem::Log is
        // a printf-style function, so overriding it would force us to copy all the logic from the
        // original method, which isn't trivial.
        // In case this is ever used, we just log the full formatted statement, which would already
        // include timestamp, thread ID and log level, making them appear twice in the output.
        constexpr const char *shortFilename = getShortFilename( __FILE__ );
        LoggingModule::log(
            LogLevel::Trace, shortFilename, static_cast<uint16_t>( __LINE__ ), __FUNCTION__, std::move( statement ) );
    }

    void
    Flush() override
    {
        LoggingModule::flush();
    }
};

struct AwsBootstrap::Impl
{
    Impl( Aws::Utils::Logging::LogLevel logLevel )
    {
        mOptions.loggingOptions.logLevel = logLevel;
        mOptions.loggingOptions.logger_create_fn =
            [logLevel]() -> std::shared_ptr<Aws::Utils::Logging::LogSystemInterface> {
            return std::make_shared<CustomLogSystem>( logLevel );
        };

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

AwsBootstrap::AwsBootstrap( Aws::Utils::Logging::LogLevel logLevel )
{
    mImpl = std::make_unique<Impl>( logLevel );
}

AwsBootstrap &
AwsBootstrap::getInstance( Aws::Utils::Logging::LogLevel logLevel )
{
    static AwsBootstrap boot( logLevel );
    return boot;
}

Aws::Utils::Logging::LogLevel
AwsBootstrap::logLevelFromString( const std::string &logLevelStr )
{
    if ( logLevelStr == "Trace" )
    {
        return Aws::Utils::Logging::LogLevel::Trace;
    }
    else if ( logLevelStr == "Debug" )
    {
        return Aws::Utils::Logging::LogLevel::Debug;
    }
    else if ( logLevelStr == "Info" )
    {
        return Aws::Utils::Logging::LogLevel::Info;
    }
    else if ( logLevelStr == "Warn" )
    {
        return Aws::Utils::Logging::LogLevel::Warn;
    }
    else if ( logLevelStr == "Error" )
    {
        return Aws::Utils::Logging::LogLevel::Error;
    }
    else if ( logLevelStr == "Fatal" )
    {
        return Aws::Utils::Logging::LogLevel::Fatal;
    }
    else if ( logLevelStr == "Off" )
    {
        return Aws::Utils::Logging::LogLevel::Off;
    }
    else
    {
        throw std::invalid_argument( "Invalid log level for AWS SDK: " + logLevelStr );
    }
}

Aws::Crt::Io::ClientBootstrap *
AwsBootstrap::getClientBootStrap()
{
    return mImpl->getClientBootStrap();
}

} // namespace IoTFleetWise
} // namespace Aws
