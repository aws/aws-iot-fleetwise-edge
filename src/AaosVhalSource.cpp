// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/AaosVhalSource.h"
#include "aws/iotfleetwise/IDecoderManifest.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

AaosVhalSource::AaosVhalSource( InterfaceID interfaceId, SignalBufferDistributor &signalBufferDistributor )
    : mInterfaceId( std::move( interfaceId ) )
    , mSignalBufferDistributor( signalBufferDistributor )
{
}

static bool
popU32FromString( std::string &decoder, uint32_t &val )
{
    try
    {
        size_t pos{};
        val = static_cast<uint32_t>( std::stoul( decoder, &pos, 0 ) );
        pos++; // Skip over delimiter
        decoder = ( pos < decoder.size() ) ? decoder.substr( pos ) : "";
    }
    catch ( ... )
    {
        return false;
    }
    return true;
}

void
AaosVhalSource::onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                            VehicleDataSourceProtocol networkProtocol )
{
    if ( networkProtocol != VehicleDataSourceProtocol::CUSTOM_DECODING )
    {
        return;
    }
    std::lock_guard<std::mutex> lock( mDecoderDictionaryUpdateMutex );
    mVehiclePropertyInfo.clear();
    mSignalIdToSignalType.clear();
    auto decoderDictionary = std::dynamic_pointer_cast<const CustomDecoderDictionary>( dictionary );
    if ( decoderDictionary == nullptr )
    {
        FWE_LOG_TRACE( "Decoder dictionary removed" );
        return;
    }
    auto decodersForInterface = decoderDictionary->customDecoderMethod.find( mInterfaceId );
    if ( decodersForInterface == decoderDictionary->customDecoderMethod.end() )
    {
        FWE_LOG_TRACE( "Decoder dictionary does not contain interface ID " + mInterfaceId );
        return;
    }
    for ( const auto &decoder : decodersForInterface->second )
    {
        auto decoderString = decoder.first;
        auto signalId = decoder.second.mSignalID;
        uint32_t vehiclePropertyId{};
        uint32_t areaIndex{};
        uint32_t resultIndex{};
        if ( ( !popU32FromString( decoderString, vehiclePropertyId ) ) ||
             ( !popU32FromString( decoderString, areaIndex ) ) || ( !popU32FromString( decoderString, resultIndex ) ) )
        {
            FWE_LOG_ERROR( "Invalid decoder for signal ID " + std::to_string( signalId ) + ": " + decoder.first );
            continue;
        }
        mVehiclePropertyInfo.push_back(
            std::array<uint32_t, 4>{ vehiclePropertyId, areaIndex, resultIndex, signalId } );
        mSignalIdToSignalType[signalId] = decoder.second.mSignalType;
    }
    FWE_LOG_TRACE( "Decoder dictionary updated" );
}

std::vector<std::array<uint32_t, 4>>
AaosVhalSource::getVehiclePropertyInfo()
{
    std::lock_guard<std::mutex> lock( mDecoderDictionaryUpdateMutex );
    return mVehiclePropertyInfo;
}

void
AaosVhalSource::setVehicleProperty( SignalID signalId, const DecodedSignalValue &value )
{
    auto signalType = SignalType::DOUBLE;
    {
        std::lock_guard<std::mutex> lock( mDecoderDictionaryUpdateMutex );
        auto it = mSignalIdToSignalType.find( signalId );
        if ( it != mSignalIdToSignalType.end() )
        {
            signalType = it->second;
        }
    }
    auto timestamp = mClock->systemTimeSinceEpochMs();
    CollectedSignalsGroup collectedSignalsGroup;
    collectedSignalsGroup.push_back( CollectedSignal::fromDecodedSignal( signalId, timestamp, value, signalType ) );

    mSignalBufferDistributor.push( CollectedDataFrame( std::move( collectedSignalsGroup ) ) );
}

} // namespace IoTFleetWise
} // namespace Aws
