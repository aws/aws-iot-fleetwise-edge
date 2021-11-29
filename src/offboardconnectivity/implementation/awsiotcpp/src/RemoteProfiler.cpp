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

#include "RemoteProfiler.h"
#include "TraceModule.h"

using namespace Aws::IoTFleetWise::OffboardConnectivity;

Aws::IoTFleetWise::Platform::IMetricsReceiver::~IMetricsReceiver()
{
}

const char *RemoteProfiler::NAME_TOP_LEVEL_LOG_ARRAY = "LogEvents";
const char *RemoteProfiler::NAME_TOP_LEVEL_LOG_PREFIX = "Prefix";

RemoteProfiler::RemoteProfiler( std::shared_ptr<ISender> metricsSender,
                                std::shared_ptr<ISender> logSender,
                                uint32_t initialMetricsUploadInterval,
                                uint32_t initialLogMaxInterval,
                                LogLevel initialLogLevelThresholdToSend,
                                std::string profilerPrefix )
    : fShouldStop( false )
    , fMetricsSender( metricsSender )
    , fLogSender( logSender )
    , fCurrentMetricsPending( 0 )
    , fInitialUploadInterval( initialMetricsUploadInterval )
    , fInitialLogMaxInterval( initialLogMaxInterval )
    , fLastTimeMetricsSentOut( 0 )
    , fLastTimeMLogsSentOut( 0 )
    , fLogLevelThreshold( initialLogLevelThresholdToSend )
    , fProfilerPrefix( profilerPrefix )
    , fCurrentUserPayloadInLogRoot( 0 )
{
    std::unique_ptr<ConsoleLogger> consoleLogger( new ConsoleLogger() );
    initLogStructure();
    fLastCPURUsage.reportCPUUsageInfo();
    fLastCPURUsage.reportPerThreadUsageData( fLastThreadUsage );
    fLastTimeExecutionEnvironmentMetricsCollected = fClock->timeSinceEpochMs();
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
    auto ret = fMetricsSender->send( reinterpret_cast<const uint8_t *>( output.c_str() ), output.length() );
    if ( ConnectivityError::Success != ret )
    {
        fLogger.error( "RemoteProfiler::sendMetricsOut",
                       " Send error" + std::to_string( static_cast<uint32_t>( ret ) ) );
    }
    fMetricsRoot.clear();
    fCurrentMetricsPending = 0;
}

void
RemoteProfiler::sendLogsOut()
{
    std::string output;
    {
        // No logging in this area as this will deadlock
        std::lock_guard<std::mutex> lock( loggingMutex );
        Json::StreamWriterBuilder builder;
        builder["indentation"] = ""; // If you want whitespace-less output
        const std::string output = Json::writeString( builder, fLogRoot );
        initLogStructure();
    }

    if ( fLogSender != nullptr && fCurrentUserPayloadInLogRoot > 0 )
    {
        auto ret = fLogSender->send( reinterpret_cast<const uint8_t *>( output.c_str() ), output.length() );
        if ( ConnectivityError::Success != ret )
        {
            fLogger.error( "RemoteProfiler::sendLogsOut",
                           " Send error" + std::to_string( static_cast<uint32_t>( ret ) ) );
        }
    }
}

void
RemoteProfiler::logMessage( LogLevel level, const std::string &function, const std::string &logEntry )
{
    if ( level < fLogLevelThreshold )
    {
        return;
    }
    Json::Value logNode;

    logNode["logLevel"] = levelToString( level );
    logNode["logFunction"] = function;
    logNode["logEntry"] = logEntry;
    uint32_t size = static_cast<uint32_t>( function.length() + logEntry.length() + JSON_MAX_OVERHEAD_BYTES_PER_LOG );
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
RemoteProfiler::setMetric( const std::string &name, double value, const std::string &unit )
{
    if ( fCurrentMetricsPending > MAX_PARALLEL_METRICS )
    {
        sendMetricsOut();
    }
    fCurrentMetricsPending++;
    Json::Value metric;
    metric["name"] = fProfilerPrefix + name;
    metric["value"] = Json::Value( value );
    metric["unit"] = unit;
    fMetricsRoot["metric" + std::to_string( fCurrentMetricsPending )] = metric;
}

bool
RemoteProfiler::start()
{
    if ( fMetricsSender == nullptr )
    {
        fLogger.error( "RemoteProfiler::start", " Trying to start without sender " );
        return false;
    }
    if ( ( fInitialLogMaxInterval == 0 && fLogLevelThreshold != LogLevel::Off ) ||
         ( fInitialLogMaxInterval != 0 && fLogLevelThreshold == LogLevel::Off ) )
    {
        fLogger.warn( "RemoteProfiler::start",
                      " Logging is turned off by putting LogLevel Threshold to Off but log max interval is not "
                      "0, which is implausible. " );
    }
    // Prevent concurrent stop/init
    std::lock_guard<std::recursive_mutex> lock( fThreadMutex );
    // On multi core systems the shared variable fShouldStop must be updated for
    // all cores before starting the thread otherwise thread will directly end
    fShouldStop.store( false );
    if ( !fThread.create( doWork, this ) )
    {
        fLogger.trace( "RemoteProfiler::start", " Remote Profiler Thread failed to start " );
    }
    else
    {
        fLogger.trace( "RemoteProfiler::start", " Remote Profiler Thread started " );
        fThread.setThreadName( "fwCNProfiler" );
    }

    return fThread.isActive() && fThread.isValid();
}

bool
RemoteProfiler::stop()
{
    if ( !fThread.isValid() || !fThread.isActive() )
    {
        return true;
    }

    std::lock_guard<std::recursive_mutex> lock( fThreadMutex );
    fShouldStop.store( true, std::memory_order_relaxed );
    fLogger.trace( "RemoteProfiler::stop", " Request stop " );
    fWait.notify();
    fThread.release();
    initLogStructure();
    fMetricsRoot.clear();
    fLogger.trace( "RemoteProfiler::stop", " Stop finished " );
    fShouldStop.store( false, std::memory_order_relaxed );
    return !fThread.isActive();
}

void
RemoteProfiler::collectExecutionEnvironmentMetrics()
{
    CPUUsageInfo lastUsage = fLastCPURUsage;
    fLastCPURUsage.reportCPUUsageInfo();
    timestampT currentTime = fClock->timeSinceEpochMs();
    double secondsBetweenCollection =
        static_cast<double>( currentTime - fLastTimeExecutionEnvironmentMetricsCollected ) / 1000.0;
    fLastTimeExecutionEnvironmentMetricsCollected = currentTime;
    double totalCPUPercentage = fLastCPURUsage.getCPUPercentage( lastUsage, secondsBetweenCollection );
    fMemoryUsage.reportMemoryUsageInfo();

    setMetric( "MemoryMaxResidentRam", static_cast<double>( fMemoryUsage.getMaxResidentMemorySize() ), "Bytes" );
    setMetric( "MemoryCurrentResidentRam", static_cast<double>( fMemoryUsage.getResidentMemorySize() ), "Bytes" );
    setMetric( "CpuPercentageSum", totalCPUPercentage, "Percent" );

    CPUUsageInfo::ThreadCPUUsageInfos threadStatsPrevious = fLastThreadUsage;
    fLastCPURUsage.reportPerThreadUsageData( fLastThreadUsage );
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
RemoteProfiler::doWork( void *data )
{
    RemoteProfiler *profiler = static_cast<RemoteProfiler *>( data );
    while ( !profiler->fShouldStop )
    {
        if ( profiler->fInitialUploadInterval == 0 && profiler->fInitialLogMaxInterval == 0 )
        {
            profiler->fWait.wait( Signal::WaitWithPredicate );
        }
        else
        {
            profiler->fWait.wait( static_cast<uint32_t>(
                std::min( profiler->fInitialUploadInterval == 0 ? std::numeric_limits<uint32_t>::max()
                                                                : profiler->fInitialUploadInterval,
                          profiler->fInitialLogMaxInterval == 0 ? std::numeric_limits<uint32_t>::max()
                                                                : profiler->fInitialLogMaxInterval ) ) );
        }
        timestampT currentTime = profiler->fClock->timeSinceEpochMs();
        if ( profiler->fShouldStop ||
             ( ( profiler->fLastTimeMetricsSentOut + profiler->fInitialUploadInterval ) < currentTime ) )
        {
            profiler->fLastTimeMetricsSentOut = currentTime;
            TraceModule::get().forwardAllMetricsToMetricsReceiver( profiler );
            profiler->collectExecutionEnvironmentMetrics();
            profiler->sendMetricsOut();
        }
        if ( profiler->fShouldStop ||
             ( ( profiler->fLastTimeMLogsSentOut + profiler->fInitialLogMaxInterval ) < currentTime ) )
        {
            profiler->fLastTimeMLogsSentOut = currentTime;
            profiler->sendLogsOut();
        }
    }
}
