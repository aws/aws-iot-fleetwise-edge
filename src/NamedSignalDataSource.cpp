// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/NamedSignalDataSource.h"
#include "aws/iotfleetwise/IDecoderManifest.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <unordered_map>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

NamedSignalDataSource::NamedSignalDataSource( InterfaceID interfaceId,
                                              SignalBufferDistributor &signalBufferDistributor )
    : mInterfaceId( std::move( interfaceId ) )
    , mSignalBufferDistributor( signalBufferDistributor )
{
}

void
NamedSignalDataSource::ingestSignalValue( Timestamp timestamp,
                                          const std::string &name,
                                          const DecodedSignalValue &value,
                                          FetchRequestID fetchRequestID )
{
    std::vector<std::pair<std::string, DecodedSignalValue>> values = { std::make_pair( name, value ) };
    ingestMultipleSignalValues( timestamp, values, fetchRequestID );
}

void
NamedSignalDataSource::ingestMultipleSignalValues(
    Timestamp timestamp,
    const std::vector<std::pair<std::string, DecodedSignalValue>> &values,
    FetchRequestID fetchRequestID )
{
    if ( timestamp == 0 )
    {
        TraceModule::get().incrementVariable( TraceVariable::POLLING_TIMESTAMP_COUNTER );
        timestamp = mClock->systemTimeSinceEpochMs();
    }
    if ( timestamp < mLastTimestamp )
    {
        TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::NOT_TIME_MONOTONIC_FRAMES );
    }
    mLastTimestamp = timestamp;

    std::lock_guard<std::mutex> lock( mDecoderDictMutex );
    if ( mDecoderDictionary == nullptr )
    {
        return;
    }
    auto decodersForInterface = mDecoderDictionary->customDecoderMethod.find( mInterfaceId );
    if ( decodersForInterface == mDecoderDictionary->customDecoderMethod.end() )
    {
        return;
    }

    CollectedSignalsGroup collectedSignalsGroup;
    for ( const auto &value : values )
    {
        auto decoderFormat = decodersForInterface->second.find( value.first );
        if ( decoderFormat == decodersForInterface->second.end() )
        {
            continue;
        }

        collectedSignalsGroup.push_back( CollectedSignal::fromDecodedSignal( decoderFormat->second.mSignalID,
                                                                             timestamp,
                                                                             value.second,
                                                                             decoderFormat->second.mSignalType,
                                                                             fetchRequestID ) );
    }
    if ( !collectedSignalsGroup.empty() )
    {
        mSignalBufferDistributor.push( CollectedDataFrame( collectedSignalsGroup ) );
    }
}

void
NamedSignalDataSource::onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                                   VehicleDataSourceProtocol networkProtocol )
{
    if ( networkProtocol != VehicleDataSourceProtocol::CUSTOM_DECODING )
    {
        return;
    }
    std::lock_guard<std::mutex> lock( mDecoderDictMutex );
    mDecoderDictionary = std::dynamic_pointer_cast<const CustomDecoderDictionary>( dictionary );
    if ( dictionary == nullptr )
    {
        FWE_LOG_TRACE( "Decoder dictionary removed" );
    }
    else
    {
        FWE_LOG_TRACE( "Decoder dictionary updated" );
    }
}

SignalID
NamedSignalDataSource::getNamedSignalID( const std::string &name )
{
    std::lock_guard<std::mutex> lock( mDecoderDictMutex );
    if ( mDecoderDictionary == nullptr )
    {
        return INVALID_SIGNAL_ID;
    }
    auto decodersForInterface = mDecoderDictionary->customDecoderMethod.find( mInterfaceId );
    if ( decodersForInterface == mDecoderDictionary->customDecoderMethod.end() )
    {
        return INVALID_SIGNAL_ID;
    }

    auto decoderFormat = decodersForInterface->second.find( name );
    if ( decoderFormat == decodersForInterface->second.end() )
    {
        return INVALID_SIGNAL_ID;
    }

    return decoderFormat->second.mSignalID;
}

} // namespace IoTFleetWise
} // namespace Aws
