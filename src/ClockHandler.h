// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief This module allows users to inject an external clock into the system.
 *        If no clock is provided, the module will return a default clock based on Chrono.
 *        The clock can be shared safely between modules that need time.
 */
class ClockHandler
{
public:
    /**
     * @brief Return the current clock that's available. If no clock has been set,
     *        a default clock based on Chrono will be returned.
     * @return Shared clock object.
     */
    static std::shared_ptr<const Clock> getClock();

    /**
     * @brief Set an external clock to the system. Thread safe API
     * @param clock the new clock instance
     */
    static void setClock( std::shared_ptr<const Clock> clock );
};

} // namespace IoTFleetWise
} // namespace Aws
