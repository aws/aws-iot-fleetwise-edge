// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "MyCustomDataSource.h"
#include <aws/iotfleetwise/IDecoderManifest.h>
#include <aws/iotfleetwise/LoggingModule.h>
#include <aws/iotfleetwise/Thread.h>
#include <chrono>
#include <cstddef>
#include <utility>

MyCustomDataSource::MyCustomDataSource( Aws::IoTFleetWise::InterfaceID interfaceId,
                                        Aws::IoTFleetWise::SignalBufferDistributor &signalBufferDistributor )
    : mInterfaceId( std::move( interfaceId ) )
    , mSignalBufferDistributor( signalBufferDistributor )
{
    // Example of using the decoders in a request/response manner:
    mThread = std::thread( [&]() {
        Aws::IoTFleetWise::Thread::setCurrentThreadName( "MyCustomIfc" );

        while ( !mThreadShouldStop )
        {
            {
                std::lock_guard<std::mutex> lock( mDecoderDictionaryUpdateMutex );

                for ( const auto &decoder : mDecodingInfoTable )
                {
                    double value;
                    if ( !requestSignalValue( decoder.first, decoder.second, value ) )
                    {
                        FWE_LOG_ERROR( "Error requesting signal ID " + std::to_string( decoder.first ) );
                        continue;
                    }

                    auto signalType = Aws::IoTFleetWise::SignalType::DOUBLE;
                    auto it = mSignalIdToSignalType.find( decoder.second.signalId );
                    if ( it != mSignalIdToSignalType.end() )
                    {
                        signalType = it->second;
                    }
                    auto timestamp = mClock->systemTimeSinceEpochMs();
                    Aws::IoTFleetWise::CollectedSignalsGroup collectedSignalsGroup;
                    collectedSignalsGroup.push_back(
                        Aws::IoTFleetWise::CollectedSignal{ decoder.second.signalId, timestamp, value, signalType } );
                    mSignalBufferDistributor.push( Aws::IoTFleetWise::CollectedDataFrame( collectedSignalsGroup ) );
                }
            }
            std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
        }
    } );

    // Example of using the decoders in a message reception function:
    registerReceptionCallback( [&]( const std::vector<uint8_t> data ) {
        if ( data.size() < 8 )
        {
            // Ignore messages smaller than expected
            return;
        }
        auto key = data[0];
        std::lock_guard<std::mutex> lock( mDecoderDictionaryUpdateMutex );
        auto decoderIt = mDecodingInfoTable.find( key );
        if ( decoderIt == mDecodingInfoTable.end() )
        {
            // Ignore messages we do not have a decoder for
            return;
        }
        if ( decoderIt->second.exampleParam1ByteOffset >= data.size() )
        {
            // Ignore byte offsets larger than the data
            return;
        }
        auto rawValue = data[decoderIt->second.exampleParam1ByteOffset];
        auto value = rawValue * decoderIt->second.exampleParam2ScalingFactor;

        auto signalType = Aws::IoTFleetWise::SignalType::DOUBLE;
        auto it = mSignalIdToSignalType.find( decoderIt->second.signalId );
        if ( it != mSignalIdToSignalType.end() )
        {
            signalType = it->second;
        }
        auto timestamp = mClock->systemTimeSinceEpochMs();
        Aws::IoTFleetWise::CollectedSignalsGroup collectedSignalsGroup;
        collectedSignalsGroup.push_back(
            Aws::IoTFleetWise::CollectedSignal{ decoderIt->second.signalId, timestamp, value, signalType } );
        mSignalBufferDistributor.push( Aws::IoTFleetWise::CollectedDataFrame( collectedSignalsGroup ) );
    } );
}

MyCustomDataSource::~MyCustomDataSource()
{
    mThreadShouldStop = true;
    mThread.join();
}

void
MyCustomDataSource::onChangeOfActiveDictionary( Aws::IoTFleetWise::ConstDecoderDictionaryConstPtr &dictionary,
                                                Aws::IoTFleetWise::VehicleDataSourceProtocol networkProtocol )
{
    if ( networkProtocol != Aws::IoTFleetWise::VehicleDataSourceProtocol::CUSTOM_DECODING )
    {
        return;
    }
    std::lock_guard<std::mutex> lock( mDecoderDictionaryUpdateMutex );
    mDecodingInfoTable.clear();
    mSignalIdToSignalType.clear();
    auto decoderDictionary = std::dynamic_pointer_cast<const Aws::IoTFleetWise::CustomDecoderDictionary>( dictionary );
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
        // Here you would parse the decoderString according to your format
        uint32_t key{};
        uint32_t exampleParam1ByteOffset{};
        uint32_t exampleParam2ScalingFactor{};
        if ( ( !popU32FromString( decoderString, key ) ) ||
             ( !popU32FromString( decoderString, exampleParam1ByteOffset ) ) ||
             ( !popU32FromString( decoderString, exampleParam2ScalingFactor ) ) )
        {
            FWE_LOG_ERROR( "Invalid decoder for signal ID " + std::to_string( signalId ) + ": " + decoder.first );
            continue;
        }
        mDecodingInfoTable.emplace(
            key, MyCustomDecodingInfo{ exampleParam1ByteOffset, exampleParam2ScalingFactor, signalId } );
        mSignalIdToSignalType[signalId] = decoder.second.mSignalType;
    }
    FWE_LOG_TRACE( "Decoder dictionary updated" );
}

bool
MyCustomDataSource::requestSignalValue( uint32_t key, const MyCustomDecodingInfo &decoder, double &value )
{
    // Here you would connect with an external middleware to request and return the signal value
    value = 42;
    return true; // No error
}

void
MyCustomDataSource::registerReceptionCallback( std::function<void( const std::vector<uint8_t> data )> callback )
{
    // Here you would register a message reception callback with an external middleware
}

bool
MyCustomDataSource::popU32FromString( std::string &decoder, uint32_t &val )
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
