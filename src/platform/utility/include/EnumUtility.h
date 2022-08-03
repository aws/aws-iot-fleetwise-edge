#pragma once

#include <type_traits>
namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
namespace Utility
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
} // namespace Utility
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws