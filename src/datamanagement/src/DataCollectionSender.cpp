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

// Includes
#include "DataCollectionSender.h"
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
                                            CANInterfaceIDTranslator &canIDTranslator )
    : mSender( sender )
    , mJsonOutputEnabled( jsonOutputEnabled )
    , mProtoWriter( canIDTranslator )
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

    mProtoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, mCollectionEventID );

    // Iterate through all the signals and add to the protobuf
    for ( const auto &signal : triggeredCollectionSchemeDataPtr->signals )
    {
        if ( mJsonOutputEnabled )
        {
            mJsonWriter.append( signal );
        }
        mProtoWriter.append( signal );
        if ( mProtoWriter.getVehicleDataMsgCount() >= mTransmitThreshold )
        {
            serializeAndTransmit();
            // Setup the next payload chunk
            mProtoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, mCollectionEventID );
        }
    }

    // Iterate through all the raw CAN frames and add to the protobuf
    for ( const auto &canFrame : triggeredCollectionSchemeDataPtr->canFrames )
    {
        if ( mJsonOutputEnabled )
        {
            mJsonWriter.append( canFrame );
        }
        mProtoWriter.append( canFrame );
        if ( mProtoWriter.getVehicleDataMsgCount() >= mTransmitThreshold )
        {
            serializeAndTransmit();
            // Setup the next payload chunk
            mProtoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, mCollectionEventID );
        }
    }

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

    // Add Geohash to the payload
    if ( triggeredCollectionSchemeDataPtr->mGeohashInfo.hasItems() )
    {
        if ( mJsonOutputEnabled )
        {
            mJsonWriter.append( triggeredCollectionSchemeDataPtr->mGeohashInfo );
        }
        mProtoWriter.append( triggeredCollectionSchemeDataPtr->mGeohashInfo );
        if ( mProtoWriter.getVehicleDataMsgCount() >= mTransmitThreshold )
        {
            serializeAndTransmit();
            // Setup the next payload chunk
            mProtoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, mCollectionEventID );
        }
    }

    if ( mJsonOutputEnabled && ( mJsonWriter.getJSONMessageCount() > 0U ) )
    {
        mJsonWriter.flushToFile( triggeredCollectionSchemeDataPtr->triggerTime );
    }
    // Serialize and transmit any remaining messages
    if ( mProtoWriter.getVehicleDataMsgCount() >= 1U )
    {
        mLogger.trace( "DataCollectionSender::send", "Sending data out" );
        serializeAndTransmit();
    }
}

ConnectivityError
DataCollectionSender::transmit()
{
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
                      "Sent out " + std::to_string( payloadData.length() ) +
                          " bytes of vehicle data proto with compression: " +
                          std::to_string( static_cast<int>( mCollectionSchemeParams.compression ) ) );
    }
    return ret;
}

ConnectivityError
DataCollectionSender::transmit( const std::string &payload )
{
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

uint32_t
DataCollectionSender::getCollectionEventId() const
{
    return mCollectionEventID;
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
