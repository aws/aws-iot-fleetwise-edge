// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Utility class to track and report CPU commutative time usage.
 */
// coverity[cert_dcl60_cpp_violation] false positive - class only defined once
// coverity[autosar_cpp14_m3_2_2_violation] false positive - class only defined once
// coverity[misra_cpp_2008_rule_3_2_2_violation] false positive - class only defined once
class CPUUsageInfo
{
public:
    static constexpr int MAX_PROC_STAT_FILE_SIZE_READ = 300;
    using ThreadId = unsigned long;
    struct ThreadCPUUsageInfo
    {
        ThreadId threadId;
        std::string threadName;
        double mUserSpaceTime;
        double mKernelSpaceTime;

        /**
         * @brief get the percentage of core that this thread is using i the last time
         *
         * @param previousUsage the stat elapsedSeconds seconds ago
         * @param elapsedSeconds for this amount of seconds the avg cpu percentage is returned
         *
         * @return the cpu utilization in percent from 100.0 = 100% to 0 = 0%
         */
        inline double
        getCPUPercentage( const ThreadCPUUsageInfo &previousUsage, const double &elapsedSeconds ) const
        {
            if ( elapsedSeconds == 0.0 )
            {
                return 0.0;
            }
            double userSpaceTime = ( mUserSpaceTime - previousUsage.mUserSpaceTime );
            double kernelSpaceTime = ( mKernelSpaceTime - previousUsage.mKernelSpaceTime );
            return round( ( ( userSpaceTime + kernelSpaceTime ) / elapsedSeconds ) * 10000.0 ) / 100.0;
        }
    };
    using ThreadCPUUsageInfos = std::vector<ThreadCPUUsageInfo>;

    /**
     * @brief Gets the amount of time spent in user space.
     * @return User space time in seconds.
     */
    double getUserSpaceTime() const;

    /**
     * @brief Gets the amount of time spent in the kernel space.
     * @return User time in seconds.
     */
    double getKernelSpaceTime() const;

    /**
     * @brief Gets the total amount of time for User and Kernel space.
     * @return Total time in seconds.
     */
    double getTotalTime() const;

    /**
     * @brief Gets the total CPU percentage used as a delta between the 2 updates.
     * @return CPU Percentage.
     */
    double getTotalCPUPercentage( const CPUUsageInfo &previousUsage, const double &elapsedSeconds ) const;

    /**
     * @brief Returns CPU percentage of one core the process is using
     * This output is comparable to htop output and can be above 100% for a multicore application
     *
     * @param previousUsage The usages that was updated elapsedSeconds ago
     * @param elapsedSeconds The average usage in this time period will be returned
     *
     * @return the cpu utilization in percent from 100.0 = 100% to 0 = 0%
     */
    double getCPUPercentage( const CPUUsageInfo &previousUsage, const double &elapsedSeconds ) const;

    /**
     * @brief Returns the time the system has been idle
     * @return time in seconds.
     */
    double getIdleTime() const;

    /**
     * @brief Returns the number of core visible to this system partition.
     * @return number of cores
     */
    uint32_t getNumCPUCores() const;

    /**
     * @brief Gets the detailed infos per thread
     * @param threadCPUUsageInfos object.
     * @return True if thread info was populated.
     */
    static bool reportPerThreadUsageData( ThreadCPUUsageInfos &threadCPUUsageInfos );

    /**
     * @brief Reports the CPU Usage info of the current Process.
     * @return True if the report succeeded.
     */
    bool reportCPUUsageInfo();

private:
    double mUserSpaceTime{ 0 };
    double mKernelSpaceTime{ 0 };
    double mIdleTime{ 0 };
    uint32_t mNumCPUCores{ std::max( std::thread::hardware_concurrency(), 1U ) };
};

inline double
CPUUsageInfo::getUserSpaceTime() const
{
    return mUserSpaceTime;
}

inline double
CPUUsageInfo::getKernelSpaceTime() const
{
    return mKernelSpaceTime;
}

inline double
CPUUsageInfo::getTotalTime() const
{
    return mUserSpaceTime + mKernelSpaceTime;
}

inline double
CPUUsageInfo::getIdleTime() const
{
    return mIdleTime;
}

inline uint32_t
CPUUsageInfo::getNumCPUCores() const
{
    return mNumCPUCores;
}

inline double
CPUUsageInfo::getCPUPercentage( const CPUUsageInfo &previousUsage, const double &elapsedSeconds ) const
{
    if ( elapsedSeconds == 0.0 )
    {
        return 0.0;
    }
    double userTime = ( mUserSpaceTime - previousUsage.getUserSpaceTime() );
    double systemTime = ( mKernelSpaceTime - previousUsage.getKernelSpaceTime() );
    return round( ( userTime + systemTime ) / elapsedSeconds * 10000.0 ) / 100.0;
}

inline double
CPUUsageInfo::getTotalCPUPercentage( const CPUUsageInfo &previousUsage, const double &elapsedSeconds ) const
{
    if ( ( mNumCPUCores == 0 ) || ( elapsedSeconds == 0.0 ) )
    {
        return 0.0;
    }
    double userTime = ( mUserSpaceTime - previousUsage.getUserSpaceTime() ) / mNumCPUCores;
    double systemTime = ( mKernelSpaceTime - previousUsage.getKernelSpaceTime() ) / mNumCPUCores;
    return round( ( userTime + systemTime ) / elapsedSeconds * 10000.0 ) / 100.0;
}

} // namespace IoTFleetWise
} // namespace Aws
