// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ExternalGpsSource.h"
#include "LoggingModule.h"
#include "SignalTypes.h"
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

ExternalGpsSource::ExternalGpsSource( std::shared_ptr<NamedSignalDataSource> namedSignalDataSource,
                                      std::string latitudeSignalName,
                                      std::string longitudeSignalName )
    : mNamedSignalDataSource( std::move( namedSignalDataSource ) )
    , mLatitudeSignalName( std::move( latitudeSignalName ) )
    , mLongitudeSignalName( std::move( longitudeSignalName ) )
{
}

void
ExternalGpsSource::setLocation( double latitude, double longitude )
{
    if ( ( !validLatitude( latitude ) ) || ( !validLongitude( longitude ) ) )
    {
        FWE_LOG_WARN( "Invalid location: Latitude: " + std::to_string( latitude ) +
                      ", Longitude: " + std::to_string( longitude ) );
        return;
    }
    FWE_LOG_TRACE( "Latitude: " + std::to_string( latitude ) + ", Longitude: " + std::to_string( longitude ) );
    std::vector<std::pair<std::string, DecodedSignalValue>> values;
    values.emplace_back( std::make_pair( mLatitudeSignalName, DecodedSignalValue{ latitude, SignalType::DOUBLE } ) );
    values.emplace_back( std::make_pair( mLongitudeSignalName, DecodedSignalValue{ longitude, SignalType::DOUBLE } ) );
    mNamedSignalDataSource->ingestMultipleSignalValues( 0, values );
}

bool
ExternalGpsSource::validLatitude( double latitude )
{
    return ( latitude >= -90.0 ) && ( latitude <= 90.0 );
}
bool
ExternalGpsSource::validLongitude( double longitude )
{
    return ( longitude >= -180.0 ) && ( longitude <= 180.0 );
}

} // namespace IoTFleetWise
} // namespace Aws
