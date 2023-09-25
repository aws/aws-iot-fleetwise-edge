// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * Wrapper around the faketime library to make it easier to use from tests and provide automatic clean up
 *
 * Since faketime is preloaded, there is no C/C++ API to configure it. This provides an abstraction
 * to avoid having to manually set env vars in the tests and to ensure faketime is configured correctly.
 *
 * Since most settings need to be configured before running the executable, when an invalid config is
 * detected, the implementation just throws an exception.
 *
 * For more details on how it works, see https://github.com/wolfcw/libfaketime/tree/v0.9.7
 */

class Faketime
{
public:
    enum fake_mode
    {
        SYSTEM_ONLY = 0,
        SYSTEM_AND_MONOTONIC = 1
    };

    /**
     * @param mode indicates which clocks should be faked. Note that in most cases the faketime library should
     *  be configured before running the executable. This parameter is mostly to detect a mismatched config.
     */
    Faketime( fake_mode mode )
    {
        std::string ldPreload = getEnvVarOrDefault( "LD_PRELOAD", "" );
        if ( ldPreload.find( "libfaketime" ) == std::string::npos )
        {
            throw std::runtime_error( "libfaketime needs to be preloaded to enable faketime" );
        }

        std::string noCache = getEnvVarOrDefault( "FAKETIME_NO_CACHE", "0" );
        if ( noCache != "1" )
        {
            // If cache is not disabled, it is not possible to reliably change the fake time while the tests are running
            throw std::runtime_error( "Faketime cache should be disabled by setting the env var FAKETIME_NO_CACHE=1 "
                                      "before running this executable" );
        }

        // Some older libfaketime releases (0.9.7) uses the DONT_FAKE_MONOTONIC variable and more recent ones
        // use FAKETIME_DONT_FAKE_MONOTONIC. So if those variables don't match, we can't reliably determine whether
        // the monotonic clock is being faked.
        std::string faketimeDontFakeMonotonic = getEnvVarOrDefault( "FAKETIME_DONT_FAKE_MONOTONIC", "0" );
        std::string dontFakeMonotonic = getEnvVarOrDefault( "DONT_FAKE_MONOTONIC", "0" );
        if ( faketimeDontFakeMonotonic != dontFakeMonotonic )
        {
            throw std::runtime_error(
                "The values for FAKETIME_DONT_FAKE_MONOTONIC and DONT_FAKE_MONOTONIC should be the same" );
        }
        bool isFakingMonotonic = !( faketimeDontFakeMonotonic == "1" && dontFakeMonotonic == "1" );

        if ( mode == SYSTEM_ONLY && isFakingMonotonic )
        {
            throw std::runtime_error(
                "It was requested to fake the system clock only but the monotonic clock is being faked too. "
                "Please set both env vars: FAKETIME_DONT_FAKE_MONOTONIC=1 DONT_FAKE_MONOTONIC=1" );
        }
        else if ( mode == SYSTEM_AND_MONOTONIC && !isFakingMonotonic )
        {
            throw std::runtime_error(
                "It was requested to fake the system and monotonic clock but the fake monotonic clock is disabled. "
                "Please set both env vars: FAKETIME_DONT_FAKE_MONOTONIC=0 DONT_FAKE_MONOTONIC=0" );
        }
    }

    ~Faketime()
    {
        disable();
    }

    /**
     * Set the current time. After that, the clock will tick normally, unless other advanced faketime options are used.
     *
     * @param time this is the time to be set as current. It should be in the format expected by libfaketime e.g. "-2d",
     * "+1h".
     */
    void
    setTime( const std::string &time ) // NOLINT(readability-convert-member-functions-to-static)
    {
        setenv( "FAKETIME", time.c_str(), 1 );
    }

    /**
     * After calling this method, all clocks should start returning the real time again.
     */
    void
    disable() // NOLINT(readability-convert-member-functions-to-static)
    {
        unsetenv( "FAKETIME" );
    }

    Faketime( const Faketime & ) = delete;
    Faketime &operator=( const Faketime & ) = delete;
    Faketime( Faketime && ) = delete;
    Faketime &operator=( Faketime && ) = delete;

private:
    static std::string
    getEnvVarOrDefault( const std::string &name, const std::string &defaultValue )
    {
        const char *envVarValue = getenv( name.c_str() );
        return envVarValue == nullptr ? defaultValue : envVarValue;
    }
};

} // namespace IoTFleetWise
} // namespace Aws
