// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <type_traits>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Get the underlying type of an enum
 *
 * @tparam E The enumerator type
 * @param enumerator The enumerator value
 * @return The underlying type of the enumerator
 */
template <typename E>
constexpr auto
toUType( E enumerator )
{
    return static_cast<std::underlying_type_t<E>>( enumerator );
}

} // namespace IoTFleetWise
} // namespace Aws
