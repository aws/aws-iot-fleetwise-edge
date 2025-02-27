// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * Generic Timer utility that helps tracking execution of tasks.
 */
class Timer
{
public:
    using Clock = std::chrono::steady_clock;
    using Duration = Clock::duration;
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
    mStart = Clock::now();
    mResume = mStart;
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

} // namespace IoTFleetWise
} // namespace Aws
