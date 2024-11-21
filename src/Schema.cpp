// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Schema.h"
#include "IConnectionTypes.h"
#include "LoggingModule.h"
#include "TopicConfig.h"
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

Schema::Schema( std::shared_ptr<IReceiver> receiverDecoderManifest,
                std::shared_ptr<IReceiver> receiverCollectionSchemeList,
                std::shared_ptr<ISender> sender )
    : mMqttSender( std::move( sender ) )
{
    // Register the listeners
    receiverDecoderManifest->subscribeToDataReceived( [this]( const ReceivedConnectivityMessage &receivedMessage ) {
        onDecoderManifestReceived( receivedMessage.buf, receivedMessage.size );
    } );
    receiverCollectionSchemeList->subscribeToDataReceived(
        [this]( const ReceivedConnectivityMessage &receivedMessage ) {
            onCollectionSchemeReceived( receivedMessage.buf, receivedMessage.size );
        } );
}

void
Schema::onDecoderManifestReceived( const uint8_t *buf, size_t size )
{
    // Check for a empty input data
    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_ERROR( "Received empty Decoder Manifest List data from the Cloud" );
        return;
    }

    // Create an empty shared pointer which we'll copy the data to
    DecoderManifestPtr decoderManifestPtr = std::make_shared<DecoderManifestIngestion>();

    // Try to copy the binary data into the decoderManifest object
    if ( !decoderManifestPtr->copyData( buf, size ) )
    {
        FWE_LOG_ERROR( "DecoderManifest copyData from IoT core failed" );
        return;
    }

    // Successful copy, so we cache the decoderManifest in the Schema object
    mDecoderManifestListeners.notify( decoderManifestPtr );
    FWE_LOG_TRACE( "Received Decoder Manifest in PI DecoderManifestCb" );
}

void
Schema::onCollectionSchemeReceived( const uint8_t *buf, size_t size )
{
    // Check for a empty input data
    if ( ( buf == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_ERROR( "Received empty CollectionScheme List data from Cloud" );
        return;
    }

    // Create an empty shared pointer which we'll copy the data to
    CollectionSchemeListPtr collectionSchemeListPtr = std::make_shared<CollectionSchemeIngestionList>();

    // Try to copy the binary data into the collectionSchemeList object
    if ( !collectionSchemeListPtr->copyData( buf, size ) )
    {
        FWE_LOG_ERROR( "CollectionSchemeList copyData from IoT core failed" );
        return;
    }

    mCollectionSchemeListeners.notify( collectionSchemeListPtr );
    FWE_LOG_TRACE( "Received CollectionSchemeList" );
}

void
Schema::sendCheckin( const std::vector<SyncID> &documentARNs, OnCheckinSentCallback callback )
{
    mProtoCheckinMsg.Clear();

    for ( auto const &doc : documentARNs )
    {
        // Note: a class member is used to store the serialized proto output to avoid heap fragmentation
        mProtoCheckinMsg.add_document_sync_ids( doc );
    }

    // Add the timestamp
    mProtoCheckinMsg.set_timestamp_ms_epoch( mClock->systemTimeSinceEpochMs() );

    if ( !mProtoCheckinMsg.SerializeToString( &mProtoCheckinMsgOutput ) )
    {
        FWE_LOG_ERROR( "Checkin serialization failed" );
        callback( false );
        return;
    }

    // transmit the data to the cloud
    FWE_LOG_TRACE( "Sending a Checkin message to the backend" );
    transmitCheckin( callback );
}

void
Schema::transmitCheckin( OnCheckinSentCallback callback )
{
    if ( mMqttSender == nullptr )
    {
        FWE_LOG_ERROR( "Invalid sender instance" );
        callback( false );
        return;
    }

    // Trace log for more verbose Checkin Info
    std::string checkinDebugString;
    checkinDebugString = "Checkin data: timestamp: " + std::to_string( mProtoCheckinMsg.timestamp_ms_epoch() );
    checkinDebugString += " with " + std::to_string( mProtoCheckinMsg.document_sync_ids_size() ) + " documents: [";

    for ( int i = 0; i < mProtoCheckinMsg.document_sync_ids_size(); i++ )
    {
        if ( i > 0 )
        {
            checkinDebugString += ", ";
        }
        checkinDebugString += mProtoCheckinMsg.document_sync_ids( i );
    }
    checkinDebugString += "]";

    mMqttSender->sendBuffer( mMqttSender->getTopicConfig().checkinsTopic,
                             reinterpret_cast<const uint8_t *>( mProtoCheckinMsgOutput.data() ),
                             mProtoCheckinMsgOutput.size(),
                             [checkinDebugString, callback]( ConnectivityError result ) {
                                 if ( result == ConnectivityError::Success )
                                 {
                                     FWE_LOG_TRACE( "Checkin Message sent to the backend" );
                                     FWE_LOG_TRACE( checkinDebugString );
                                     callback( true );
                                 }
                                 else if ( result == ConnectivityError::NoConnection )
                                 {
                                     FWE_LOG_TRACE( "Couldn't send checkin message because there is no connection" );
                                     callback( false );
                                 }
                                 else
                                 {
                                     FWE_LOG_ERROR(
                                         "offboardconnectivity error, will retry sending the checkin message" );
                                     callback( false );
                                 }
                             } );
}

} // namespace IoTFleetWise
} // namespace Aws
