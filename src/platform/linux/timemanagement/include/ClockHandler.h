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

#include "Clock.h"
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
namespace Linux
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

} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws