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

#include <chrono>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{

/**
 * Generic Timer utility that helps tracking execution of tasks.
 */
class Timer
{
public:
    using Clock = std::chrono::high_resolution_clock;
    using Duration = Clock::duration;
    using DurationUs = std::chrono::microseconds;
    using DurationMs = std::chrono::milliseconds;
    using TimePoint = Clock::time_point;

    /**
     * @brief Default constructor, all internal counters will be reset.
     */
    Timer();

    /**
     * @brief Resets the timer counter.
     */
    void reset();

    /**
     *  @brief Pauses tick counting.
     */
    void pause();

    /**
     * @brief Resumes tick counting.
     */
    void resume();

    /**
     * @brief Checks if the timer is running.
     * @return True if running, false if paused
     */
    bool isTimerRunning() const;

    /**
     * @brief Get elapsed duration in ms.
     * @return Duration in ms.
     */
    DurationMs getElapsedMs() const;

    /**
     * @brief Get elapsed duration in seconds.
     * @return Duration in seconds.
     */
    double getElapsedSeconds() const;

private:
    /**
     * @brief Get elapsed duration in nanonseconds.( Default Chrono implementation )
     * @return Chrono Duration object.
     */
    Duration getElapsed() const;

    TimePoint mStart;
    TimePoint mResume;
    Duration mElapsed;
    bool mIsTimerRunning;
};

inline Timer::Timer()
{
    reset();
}

inline void
Timer::reset()
{
    mStart = mResume = Clock::now();
    mElapsed = Duration{ 0 };
    mIsTimerRunning = true;
}

inline void
Timer::pause()
{
    if ( !mIsTimerRunning )
    {
        return;
    }

    mIsTimerRunning = false;
    mElapsed += Clock::now() - mResume;
}

inline void
Timer::resume()
{
    if ( mIsTimerRunning )
    {
        return;
    }
    mIsTimerRunning = true;
    mResume = Clock::now();
}

inline bool
Timer::isTimerRunning() const
{
    return mIsTimerRunning;
}

inline Timer::Duration
Timer::getElapsed() const
{
    return mIsTimerRunning ? mElapsed + Clock::now() - mResume : mElapsed;
}

inline Timer::DurationMs
Timer::getElapsedMs() const
{
    return std::chrono::duration_cast<DurationMs>( getElapsed() );
}

inline double
Timer::getElapsedSeconds() const
{
    return std::chrono::duration<double>( getElapsed() ).count();
}

} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
