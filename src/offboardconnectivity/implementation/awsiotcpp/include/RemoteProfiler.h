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

#include "CPUUsageInfo.h"
#include "ClockHandler.h"
#include "IConnectionTypes.h"
#include "ILogger.h"
#include "ISender.h"
#include "LogLevel.h"
#include "LoggingModule.h"
#include "MemoryUsageInfo.h"
#include "Thread.h"
#include "TraceModule.h"
#include <json/json.h>
#include <memory>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace OffboardConnectivity
{
using namespace Aws::IoTFleetWise::Platform;
class RemoteProfiler : public IMetricsReceiver, public ILogger
{
public:
    static const uint16_t MAX_PARALLEL_METRICS = 10;
    static const char *NAME_TOP_LEVEL_LOG_ARRAY;
    static const char *NAME_TOP_LEVEL_LOG_PREFIX;

    static const uint32_t MAX_BYTES_FOR_SINGLE_LOG_UPLOAD =
        16384; // 16KiB. Must be << 128KiB because its sent in one MQTT message
    static const uint32_t JSON_MAX_OVERHEAD_BYTES_PER_LOG = 60; // Including LogLevel

    /**
     * @brief Construct the RemoteProfiler which can upload metrics and logs over MQTT
     *
     * @param metricsSender the channel that should be used to upload metrics
     * @param logSender the channel that should be used to upload logs
     * @param initialMetricsUploadInterval the interval used between two metrics uploads
     * @param initialLogMaxInterval the max interval that logs can be cached before uploading
     * @param initialLogLevelThresholdToSend all logs below this threshold will be ignored
     * @param profilerPrefix this prefix will be added to the metrics name and can for example be used to distinguish
     * specific vehicles
     *
     */
    RemoteProfiler( std::shared_ptr<ISender> metricsSender,
                    std::shared_ptr<ISender> logSender,
                    uint32_t initialMetricsUploadInterval,
                    uint32_t initialLogMaxInterval,
                    LogLevel initialLogLevelThresholdToSend,
                    std::string profilerPrefix );

    /**
     * @brief Implement the ILogger interface
     *
     * Packs the log string into a json and uploads to the cloud if necessary
     * Can be called from multiple threads.
     *
     * @param level the log level used to decide if log entry should be uploaded
     * @param function the name of the function which emitted the log message
     * @param logEntry the actual log message
     */
    void logMessage( LogLevel level, const std::string &function, const std::string &logEntry ) override;

    /**
     * @brief implements IMetricsReceiver and uploads metrics over MQTT
     *
     * @param name the name that is displayed
     * @param value the value as double
     * @param unit the unit can be for example: Seconds | Microseconds | Milliseconds | Bytes | Kilobytes | Megabytes |
     * Gigabytes | Terabytes | Bits | Kilobits | Megabits | Gigabits | Terabits | Percent | Count | Bytes/Second |
     * Kilobytes/Second | Megabytes/Second | Gigabytes/Second | Terabytes/Second | Bits/Second | Kilobits/Second |
     * Megabits/Second | Gigabits/Second | Terabits/Second | Count/Second | None
     */
    void setMetric( const std::string &name, double value, const std::string &unit ) override;

    /**
     * @brief start the thread
     * @return true if thread starting was successful
     */
    bool start();

    /**
     * @brief stops the thread
     * @return true if thread stopping was successful
     */
    bool stop();

    /**
     * @brief check if thread is currently running
     * @return true if thread is currently running
     */
    bool
    isAlive()
    {
        return fThread.isValid() && fThread.isActive();
    }

    ~RemoteProfiler()
    {
        // To make sure the thread stops during teardown of tests.
        if ( isAlive() )
        {
            stop();
        }
    }

private:
    static void doWork( void *data );

    void sendMetricsOut();

    void sendLogsOut();

    void collectExecutionEnvironmentMetrics();

    void initLogStructure();

    Thread fThread;
    std::atomic<bool> fShouldStop;
    std::shared_ptr<ISender> fMetricsSender;
    std::shared_ptr<ISender> fLogSender;
    std::recursive_mutex fThreadMutex;
    std::mutex loggingMutex;
    LoggingModule fLogger;
    Signal fWait;
    Json::Value fMetricsRoot;
    Json::Value fLogRoot;
    uint16_t fCurrentMetricsPending;
    uint32_t fInitialUploadInterval;
    uint32_t fInitialLogMaxInterval;
    std::shared_ptr<const Clock> fClock = ClockHandler::getClock();
    timestampT fLastTimeMetricsSentOut;
    timestampT fLastTimeMLogsSentOut;
    timestampT fLastTimeExecutionEnvironmentMetricsCollected;
    LogLevel fLogLevelThreshold;
    CPUUsageInfo fLastCPURUsage;
    CPUUsageInfo::ThreadCPUUsageInfos fLastThreadUsage;
    MemoryUsageInfo fMemoryUsage;
    std::string fProfilerPrefix;
    uint32_t fCurrentUserPayloadInLogRoot;
};
} // namespace OffboardConnectivity
} // namespace IoTFleetWise
} // namespace Aws