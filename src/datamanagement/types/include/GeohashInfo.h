#pragma once

#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

/**
 * @brief This struct encapsulates the Geohash information.
 * In current implementation, the Geohash is represented in String Format.
 * API hasItems() indicates whether this structure contains valid Geohash
 *
 */
struct GeohashInfo
{
    // Geohash in String Format
    std::string mGeohashString;

    // Geohash in string format which was reported to Cloud in last update
    std::string mPrevReportedGeohashString;

    // Return whether Geohash is valid to use. In string format, we consider non-zero length string
    // as valid geohash
    bool
    hasItems() const
    {
        return mGeohashString.length() > 0;
    }
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
