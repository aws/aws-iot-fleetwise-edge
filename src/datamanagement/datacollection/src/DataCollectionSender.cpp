// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "DataCollectionSender.h"
#include "LoggingModule.h"
#include "TraceModule.h"
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
                                            unsigned maxMessageCount,
                                            CANInterfaceIDTranslator &canIDTranslator )
    : mSender( std::move( sender ) )
    , mTransmitThreshold{ ( maxMessageCount > 0U ) ? maxMessageCount : UINT_MAX }
    , mProtoWriter( canIDTranslator )
{
}

void
DataCollectionSender::send( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeDataPtr )
{
    if ( triggeredCollectionSchemeDataPtr == nullptr )
    {
        FWE_LOG_WARN( "Nothing to send as the input is empty" );
        return;
    }

    // Assign a unique event id to the edge to cloud payload
    mCollectionEventID = triggeredCollectionSchemeDataPtr->eventID;

    setCollectionSchemeParameters( *triggeredCollectionSchemeDataPtr );

    if ( mSendDestination == SendDestination::MQTT )
    {
        mProtoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, mCollectionEventID );
    }

    // Iterate through all the signals and add to the protobuf
    for ( const auto &signal : triggeredCollectionSchemeDataPtr->signals )
    {
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

    if ( mSendDestination == SendDestination::MQTT )
    {
        // Serialize and transmit any remaining messages
        if ( mProtoWriter.getVehicleDataMsgCount() >= 1U )
        {
            FWE_LOG_TRACE( "The data collection snapshot has been written on disk and is now scheduled for upload to "
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
        FWE_LOG_TRACE( "Upload destination is not  set to AWS IoT Core. Skipping this request" );
        return ConnectivityError::Success;
    }

    std::string payloadData;
    // compress the data before transmitting if specified in the collectionScheme
    if ( mCollectionSchemeParams.compression )
    {
        FWE_LOG_TRACE( "Compress the payload before transmitting since compression flag is true" );
        if ( snappy::Compress( mProtoOutput.data(), mProtoOutput.size(), &payloadData ) == 0U )
        {
            FWE_LOG_TRACE( "Error in compressing the payload" );
            return ConnectivityError::WrongInputData;
        }
    }
    else
    {
        // Using payloadData as a container to send(), hence assign it to original uncompressed payload
        payloadData = mProtoOutput;
    }

    ConnectivityError ret = mSender->sendBuffer(
        reinterpret_cast<const uint8_t *>( payloadData.data() ), payloadData.size(), mCollectionSchemeParams );
    if ( ret != ConnectivityError::Success )
    {
        FWE_LOG_ERROR( "Failed to send vehicle data proto with error: " + std::to_string( static_cast<int>( ret ) ) );
    }
    else
    {
        TraceModule::get().sectionEnd( TraceSection::COLLECTION_SCHEME_CHANGE_TO_FIRST_DATA );
        TraceModule::get().incrementVariable( TraceVariable::MQTT_SIGNAL_MESSAGES_SENT_OUT );
        FWE_LOG_INFO( "A Payload of size: " + std::to_string( payloadData.length() ) +
                      " bytes has been uploaded to AWS IoT Core" );
    }
    return ret;
}

ConnectivityError
DataCollectionSender::transmit( const std::string &payload )
{
    if ( mSendDestination != SendDestination::MQTT )
    {
        FWE_LOG_TRACE( "Upload destination is not  set to AWS IoT Core. Skipping this request" );
        return ConnectivityError::WrongInputData;
    }

    auto res = mSender->sendBuffer( reinterpret_cast<const uint8_t *>( payload.data() ), payload.size() );
    if ( res != ConnectivityError::Success )
    {
        FWE_LOG_ERROR( "offboardconnectivity error " + std::to_string( static_cast<int>( res ) ) );
    }
    return res;
}

void
DataCollectionSender::serializeAndTransmit()
{
    if ( mSendDestination != SendDestination::MQTT )
    {
        FWE_LOG_TRACE( "Upload destination is not  set to AWS IoT Core. Skipping this request" );
        return;
    }

    // Note: a class member is used to store the serialized proto output to avoid heap fragmentation
    if ( !mProtoWriter.serializeVehicleData( &mProtoOutput ) )
    {
        FWE_LOG_ERROR( "serialization failed" );
    }
    else
    {
        // transmit the data to the cloud
        auto res = transmit();
        if ( res != ConnectivityError::Success )
        {
            FWE_LOG_ERROR( "offboardconnectivity error while transmitting data" +
                           std::to_string( static_cast<int>( res ) ) );
        }
    }
}

void
DataCollectionSender::setCollectionSchemeParameters(
    const TriggeredCollectionSchemeData &triggeredCollectionSchemeDataPtr )
{
    mCollectionSchemeParams.persist = triggeredCollectionSchemeDataPtr.metaData.persist;
    mCollectionSchemeParams.compression = triggeredCollectionSchemeDataPtr.metaData.compress;
    mCollectionSchemeParams.priority = triggeredCollectionSchemeDataPtr.metaData.priority;
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
