// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/RemoteProfiler.h"
#include "aws/iotfleetwise/IConnectionTypes.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/TopicConfig.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

RemoteProfiler::RemoteProfiler( ISender &sender,
                                uint32_t initialMetricsUploadInterval,
                                uint32_t initialLogMaxInterval,
                                LogLevel initialLogLevelThresholdToSend,
                                std::string profilerPrefix )
    : fShouldStop( false )
    , mMqttSender( sender )
    , fCurrentMetricsPending( 0 )
    , fInitialUploadInterval( initialMetricsUploadInterval )
    , fInitialLogMaxInterval( initialLogMaxInterval )
    , fLastTimeMetricsSentOut( 0 )
    , fLastTimeMLogsSentOut( 0 )
    , fLastTimeExecutionEnvironmentMetricsCollected( fClock->monotonicTimeSinceEpochMs() )
    , fLogLevelThreshold( initialLogLevelThresholdToSend )
    , fProfilerPrefix( std::move( profilerPrefix ) )
    , fCurrentUserPayloadInLogRoot( 0 )
{
    initLogStructure();
    fLastCPURUsage.reportCPUUsageInfo();
    CPUUsageInfo::reportPerThreadUsageData( fLastThreadUsage );
}

void
RemoteProfiler::initLogStructure()
{
    fLogRoot.clear();
    fLogRoot[NAME_TOP_LEVEL_LOG_PREFIX] = fProfilerPrefix;
    fLogRoot[NAME_TOP_LEVEL_LOG_ARRAY] = Json::arrayValue;
    fCurrentUserPayloadInLogRoot = 0;
}

void
RemoteProfiler::sendMetricsOut()
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = ""; // If you want whitespace-less output
    const std::string output = Json::writeString( builder, fMetricsRoot );
    mMqttSender.sendBuffer( mMqttSender.getTopicConfig().metricsTopic,
                            reinterpret_cast<const uint8_t *>( output.c_str() ),
                            output.length(),
                            []( ConnectivityError result ) {
                                if ( result == ConnectivityError::Success )
                                {
                                    FWE_LOG_ERROR( "Send error " + std::to_string( static_cast<uint32_t>( result ) ) );
                                }
                            } );
    fMetricsRoot.clear();
    fCurrentMetricsPending = 0;
}

void
RemoteProfiler::sendLogsOut()
{
    if ( fCurrentUserPayloadInLogRoot > 0 )
    {
        std::string output;
        {
            // No logging in this area as this will deadlock
            std::lock_guard<std::mutex> lock( loggingMutex );
            Json::StreamWriterBuilder builder;
            builder["indentation"] = ""; // If you want whitespace-less output
            output = Json::writeString( builder, fLogRoot );
            initLogStructure();
        }

        mMqttSender.sendBuffer( mMqttSender.getTopicConfig().logsTopic,
                                reinterpret_cast<const uint8_t *>( output.c_str() ),
                                output.length(),
                                []( ConnectivityError result ) {
                                    if ( result == ConnectivityError::Success )
                                    {
                                        FWE_LOG_ERROR( "Send error " +
                                                       std::to_string( static_cast<uint32_t>( result ) ) );
                                    }
                                } );
    }
}

void
RemoteProfiler::logMessage( LogLevel level,
                            const std::string &filename,
                            const uint32_t lineNumber,
                            const std::string &function,
                            const std::string &logEntry )
{
    if ( level < fLogLevelThreshold )
    {
        return;
    }
    Json::Value logNode;
    const std::string timestamp = fClock->currentTimeToIsoString();
    logNode["logLevel"] = levelToString( level );
    logNode["logFile"] = filename;
    logNode["logLineNumber"] = lineNumber;
    logNode["logFunction"] = function;
    logNode["logEntry"] = logEntry;
    logNode["logTimestamp"] = timestamp;
    uint32_t size = static_cast<uint32_t>( filename.length() + function.length() + logEntry.length() +
                                           timestamp.length() + JSON_MAX_OVERHEAD_BYTES_PER_LOG );
    bool sendOutBeforeAdding = false;
    {
        std::lock_guard<std::mutex> lock( loggingMutex );
        if ( size + fCurrentUserPayloadInLogRoot > MAX_BYTES_FOR_SINGLE_LOG_UPLOAD )
        {
            sendOutBeforeAdding = true;
        }
        else
        {
            fLogRoot[NAME_TOP_LEVEL_LOG_ARRAY].append( logNode );
            fCurrentUserPayloadInLogRoot += size;
        }
    }
    if ( sendOutBeforeAdding )
    {
        sendLogsOut();
        {
            std::lock_guard<std::mutex> lock( loggingMutex );
            fLogRoot[NAME_TOP_LEVEL_LOG_ARRAY].append( logNode );
            fCurrentUserPayloadInLogRoot += size;
        }
    }
}

void
RemoteProfiler::flush()
{
    sendLogsOut();
}

void
RemoteProfiler::setMetric( const std::string &name, double value, const std::string &unit )
{
    if ( fCurrentMetricsPending > MAX_PARALLEL_METRICS )
    {
        sendMetricsOut();
    }
    fCurrentMetricsPending++;
    Json::Value metric;
    metric["name"] = fProfilerPrefix + "_" + name;
    metric["value"] = Json::Value( value );
    metric["unit"] = unit;
    fMetricsRoot["metric" + std::to_string( fCurrentMetricsPending )] = metric;
}

bool
RemoteProfiler::start()
{
    if ( ( ( fInitialLogMaxInterval == 0 ) && ( fLogLevelThreshold != LogLevel::Off ) ) ||
         ( ( fInitialLogMaxInterval != 0 ) && ( fLogLevelThreshold == LogLevel::Off ) ) )
    {
        FWE_LOG_WARN( "Logging is turned off by putting LogLevel Threshold to Off but log max interval is not "
                      "0, which is implausible" );
    }
    // Prevent concurrent stop/init
    std::lock_guard<std::mutex> lock( fThreadMutex );
    // On multi core systems the shared variable fShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    fShouldStop.store( false );
    if ( !fThread.create( [this]() {
             this->doWork();
         } ) )
    {
        FWE_LOG_TRACE( "Remote Profiler Thread failed to start" );
    }
    else
    {
        FWE_LOG_TRACE( "Remote Profiler Thread started" );
        fThread.setThreadName( "fwCNProfiler" );
    }

    return fThread.isActive() && fThread.isValid();
}

bool
RemoteProfiler::stop()
{
    if ( ( !fThread.isValid() ) || ( !fThread.isActive() ) )
    {
        return true;
    }

    std::lock_guard<std::mutex> lock( fThreadMutex );
    fShouldStop.store( true, std::memory_order_relaxed );
    FWE_LOG_TRACE( "Request stop" );
    fWait.notify();
    fThread.release();
    initLogStructure();
    fMetricsRoot.clear();
    FWE_LOG_TRACE( "Stop finished" );
    fShouldStop.store( false, std::memory_order_relaxed );
    return !fThread.isActive();
}

void
RemoteProfiler::collectExecutionEnvironmentMetrics()
{
    CPUUsageInfo lastUsage = fLastCPURUsage;
    fLastCPURUsage.reportCPUUsageInfo();
    Timestamp currentTime = fClock->monotonicTimeSinceEpochMs();
    double secondsBetweenCollection =
        static_cast<double>( currentTime - fLastTimeExecutionEnvironmentMetricsCollected ) / 1000.0;
    fLastTimeExecutionEnvironmentMetricsCollected = currentTime;
    double totalCPUPercentage = fLastCPURUsage.getCPUPercentage( lastUsage, secondsBetweenCollection );
    fMemoryUsage.reportMemoryUsageInfo();

    setMetric( "MemoryMaxResidentRam", static_cast<double>( fMemoryUsage.getMaxResidentMemorySize() ), "Bytes" );
    setMetric( "MemoryCurrentResidentRam", static_cast<double>( fMemoryUsage.getResidentMemorySize() ), "Bytes" );
    setMetric( "CpuPercentageSum", totalCPUPercentage, "Percent" );

    CPUUsageInfo::ThreadCPUUsageInfos threadStatsPrevious = fLastThreadUsage;
    CPUUsageInfo::reportPerThreadUsageData( fLastThreadUsage );
    for ( auto currentThreadCPUUsageInfo : fLastThreadUsage )
    {
        for ( auto previousThreadCPUUsageInfo : threadStatsPrevious )
        {
            if ( currentThreadCPUUsageInfo.threadId == previousThreadCPUUsageInfo.threadId )
            {
                setMetric(
                    std::string( "CpuThread_" ) + currentThreadCPUUsageInfo.threadName + "_" +
                        std::to_string( currentThreadCPUUsageInfo.threadId ),
                    currentThreadCPUUsageInfo.getCPUPercentage( previousThreadCPUUsageInfo, secondsBetweenCollection ),
                    "Percent" );
            }
        }
    }
}

void
RemoteProfiler::doWork()
{
    while ( !fShouldStop )
    {
        if ( ( fInitialUploadInterval == 0 ) && ( fInitialLogMaxInterval == 0 ) )
        {
            fWait.wait( Signal::WaitWithPredicate );
        }
        else
        {
            fWait.wait( static_cast<uint32_t>( std::min(
                fInitialUploadInterval == 0 ? std::numeric_limits<uint32_t>::max() : fInitialUploadInterval,
                fInitialLogMaxInterval == 0 ? std::numeric_limits<uint32_t>::max() : fInitialLogMaxInterval ) ) );
        }
        Timestamp currentTime = fClock->monotonicTimeSinceEpochMs();
        if ( fShouldStop || ( ( fLastTimeMetricsSentOut + fInitialUploadInterval ) < currentTime ) )
        {
            fLastTimeMetricsSentOut = currentTime;
            TraceModule::get().forwardAllMetricsToMetricsReceiver( this );
            TraceModule::get().startNewObservationWindow( fInitialUploadInterval );
            collectExecutionEnvironmentMetrics();
            sendMetricsOut();
        }
        if ( fShouldStop || ( ( fLastTimeMLogsSentOut + fInitialLogMaxInterval ) < currentTime ) )
        {
            fLastTimeMLogsSentOut = currentTime;
            sendLogsOut();
        }
    }
}
} // namespace IoTFleetWise
} // namespace Aws
