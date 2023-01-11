// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "DataCollectionSender.h"
#include <boost/filesystem.hpp>
#include <snappy.h>
#include <sstream>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{

DataCollectionSender::DataCollectionSender( std::shared_ptr<ISender> sender,
                                            bool jsonOutputEnabled,
                                            unsigned maxMessageCount,
                                            CANInterfaceIDTranslator &canIDTranslator,
                                            std::string persistencyPath )
    : mSender( std::move( sender ) )
    , mJsonOutputEnabled( jsonOutputEnabled )
    , mProtoWriter( canIDTranslator )
    , mJsonWriter( std::move( persistencyPath ) )
{
    mTransmitThreshold = ( maxMessageCount > 0U ) ? maxMessageCount : UINT_MAX;
    mCollectionEventID = 0U;
}

void
DataCollectionSender::send( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeDataPtr )
{
    if ( triggeredCollectionSchemeDataPtr == nullptr )
    {
        mLogger.warn( "DataCollectionSender::send", "Nothing to send as the input is empty" );
        return;
    }

    // Assign a unique event id to the edge to cloud payload
    mCollectionEventID = triggeredCollectionSchemeDataPtr->eventID;

    setCollectionSchemeParameters( triggeredCollectionSchemeDataPtr );

    if ( mJsonOutputEnabled )
    {
        mJsonWriter.setupEvent( triggeredCollectionSchemeDataPtr, mCollectionEventID );
    }

    if ( mSendDestination == SendDestination::MQTT )
    {
        mProtoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, mCollectionEventID );
    }

    // Iterate through all the signals and add to the protobuf
    for ( const auto &signal : triggeredCollectionSchemeDataPtr->signals )
    {
        if ( mJsonOutputEnabled )
        {
            mJsonWriter.append( signal );
        }
        if ( mSendDestination == SendDestination::MQTT )
        {
            mProtoWriter.append( signal );
            if ( mProtoWriter.getVehicleDataMsgCount() >= mTransmitThreshold )
            {
                serializeAndTransmit();
                // Setup the next payload chunk
                mProtoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, mCollectionEventID );
            }
        }
    }

    // Iterate through all the raw CAN frames and add to the protobuf
    for ( const auto &canFrame : triggeredCollectionSchemeDataPtr->canFrames )
    {
        if ( mJsonOutputEnabled )
        {
            mJsonWriter.append( canFrame );
        }
        if ( mSendDestination == SendDestination::MQTT )
        {
            mProtoWriter.append( canFrame );
            if ( mProtoWriter.getVehicleDataMsgCount() >= mTransmitThreshold )
            {
                serializeAndTransmit();
                // Setup the next payload chunk
                mProtoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, mCollectionEventID );
            }
        }
    }

    if ( mSendDestination == SendDestination::MQTT )
    {
        // Add DTC info to the payload
        if ( triggeredCollectionSchemeDataPtr->mDTCInfo.hasItems() )
        {
            mProtoWriter.setupDTCInfo( triggeredCollectionSchemeDataPtr->mDTCInfo );
            const auto &dtcCodes = triggeredCollectionSchemeDataPtr->mDTCInfo.mDTCCodes;

            // Iterate through all the DTC codes and add to the protobuf
            for ( const auto &dtc : dtcCodes )
            {
                mProtoWriter.append( dtc );

                if ( mProtoWriter.getVehicleDataMsgCount() >= mTransmitThreshold )
                {
                    serializeAndTransmit();
                    // Setup the next payload chunk
                    mProtoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, mCollectionEventID );
                    // Setup DTC metadata for the next payload
                    mProtoWriter.setupDTCInfo( triggeredCollectionSchemeDataPtr->mDTCInfo );
                }
            }
        }
    }

    // Add Geohash to the payload
    if ( triggeredCollectionSchemeDataPtr->mGeohashInfo.hasItems() )
    {
        if ( mJsonOutputEnabled )
        {
            mJsonWriter.append( triggeredCollectionSchemeDataPtr->mGeohashInfo );
        }
        if ( mSendDestination == SendDestination::MQTT )
        {
            mProtoWriter.append( triggeredCollectionSchemeDataPtr->mGeohashInfo );
            if ( mProtoWriter.getVehicleDataMsgCount() >= mTransmitThreshold )
            {
                serializeAndTransmit();
                // Setup the next payload chunk
                mProtoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, mCollectionEventID );
            }
        }
    }

    if ( mJsonOutputEnabled && ( mJsonWriter.getJSONMessageCount() > 0U ) )
    {
        const auto result = mJsonWriter.flushToFile();
    }
    if ( mSendDestination == SendDestination::MQTT )
    {
        // Serialize and transmit any remaining messages
        if ( mProtoWriter.getVehicleDataMsgCount() >= 1U )
        {
            mLogger.trace( "DataCollectionSender::send",
                           "The data collection snapshot has been written on disk and is now scheduled for upload to "
                           "AWS IoT Core" );
            serializeAndTransmit();
        }
    }
}

ConnectivityError
DataCollectionSender::transmit()
{
    if ( mSendDestination != SendDestination::MQTT )
    {
        mLogger.trace( "DataCollectionSender::transmit",
                       "Upload destination is not  set to AWS IoT Core. Skipping this request" );
        return ConnectivityError::Success;
    }

    std::string payloadData;
    // compress the data before transmitting if specified in the collectionScheme
    if ( mCollectionSchemeParams.compression )
    {
        mLogger.trace( "DataCollectionSender::transmit",
                       "Compress the payload before transmitting since compression flag is true" );
        if ( snappy::Compress( mProtoOutput.data(), mProtoOutput.size(), &payloadData ) == 0u )
        {
            mLogger.trace( "DataCollectionSender::transmit", "Error in compressing the payload" );
            return ConnectivityError::WrongInputData;
        }
    }
    else
    {
        // Using payloadData as a container to send(), hence assign it to original uncompressed payload
        payloadData = mProtoOutput;
    }

    ConnectivityError ret = mSender->send(
        reinterpret_cast<const uint8_t *>( payloadData.data() ), payloadData.size(), mCollectionSchemeParams );
    if ( ret != ConnectivityError::Success )
    {
        mLogger.error( "DataCollectionSender::transmit",
                       "Failed to send vehicle data proto with error: " + std::to_string( static_cast<int>( ret ) ) );
    }
    else
    {
        mLogger.info( "DataCollectionSender::transmit",
                      "A Payload of size: " + std::to_string( payloadData.length() ) +
                          " bytes has been unloaded to AWS IoT Core" );
    }
    return ret;
}

ConnectivityError
DataCollectionSender::transmit( const std::string &payload )
{
    if ( mSendDestination != SendDestination::MQTT )
    {
        mLogger.trace( "DataCollectionSender::transmit",
                       "Upload destination is not  set to AWS IoT Core. Skipping this request" );
        return ConnectivityError::WrongInputData;
    }

    auto res = mSender->send( reinterpret_cast<const uint8_t *>( payload.data() ), payload.size() );
    if ( res != ConnectivityError::Success )
    {
        mLogger.error( "DataCollectionSender::transmit",
                       "offboardconnectivity error " + std::to_string( static_cast<int>( res ) ) );
    }
    return res;
}

void
DataCollectionSender::serializeAndTransmit()
{
    if ( mSendDestination != SendDestination::MQTT )
    {
        mLogger.trace( "DataCollectionSender::serializeAndTransmit",
                       "Upload destination is not  set to AWS IoT Core. Skipping this request" );
        return;
    }

    // Note: a class member is used to store the serialized proto output to avoid heap fragmentation
    if ( !mProtoWriter.serializeVehicleData( &mProtoOutput ) )
    {
        mLogger.error( "DataCollectionSender::serializeAndTransmit", "serialization failed" );
    }
    else
    {
        // transmit the data to the cloud
        auto res = transmit();
        if ( res != ConnectivityError::Success )
        {
            mLogger.error( "DataCollectionSender::serializeAndTransmit",
                           "offboardconnectivity error while transmitting data" +
                               std::to_string( static_cast<int>( res ) ) );
        }
    }
}

void
DataCollectionSender::setCollectionSchemeParameters(
    const TriggeredCollectionSchemeDataPtr &triggeredCollectionSchemeDataPtr )
{
    mCollectionSchemeParams.persist = triggeredCollectionSchemeDataPtr->metaData.persist;
    mCollectionSchemeParams.compression = triggeredCollectionSchemeDataPtr->metaData.compress;
    mCollectionSchemeParams.priority = triggeredCollectionSchemeDataPtr->metaData.priority;
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
