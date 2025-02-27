// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "aws/iotfleetwise/NamedSignalDataSource.h"
#include <memory>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

class ExternalGpsSource
{
public:
    /**
     * @param namedSignalDataSource Named signal data source
     * @param latitudeSignalName the signal name of the latitude signal
     * @param longitudeSignalName the signal name of the longitude signal
     */
    ExternalGpsSource( std::shared_ptr<NamedSignalDataSource> namedSignalDataSource,
                       std::string latitudeSignalName,
                       std::string longitudeSignalName );

    void setLocation( double latitude, double longitude );

    static constexpr const char *LATITUDE_SIGNAL_NAME = "latitudeSignalName";
    static constexpr const char *LONGITUDE_SIGNAL_NAME = "longitudeSignalName";

private:
    static bool validLatitude( double latitude );
    static bool validLongitude( double longitude );

    std::shared_ptr<NamedSignalDataSource> mNamedSignalDataSource;
    std::string mLatitudeSignalName;
    std::string mLongitudeSignalName;
};

} // namespace IoTFleetWise
} // namespace Aws
