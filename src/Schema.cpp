// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/Schema.h"
#include "aws/iotfleetwise/CollectionSchemeIngestionList.h"
#include "aws/iotfleetwise/DecoderManifestIngestion.h"
#include "aws/iotfleetwise/IConnectionTypes.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/TopicConfig.h"

namespace Aws
{
namespace IoTFleetWise
{

Schema::Schema( IReceiver &receiverDecoderManifest, IReceiver &receiverCollectionSchemeList, ISender &sender )
    : mMqttSender( sender )
{
    // Register the listeners
    receiverDecoderManifest.subscribeToDataReceived( [this]( const ReceivedConnectivityMessage &receivedMessage ) {
        onDecoderManifestReceived( receivedMessage.buf, receivedMessage.size );
    } );
    receiverCollectionSchemeList.subscribeToDataReceived( [this]( const ReceivedConnectivityMessage &receivedMessage ) {
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

    auto decoderManifestPtr = std::make_unique<DecoderManifestIngestion>();

    // Try to copy the binary data into the decoderManifest object
    if ( !decoderManifestPtr->copyData( buf, size ) )
    {
        FWE_LOG_ERROR( "DecoderManifest copyData from IoT core failed" );
        return;
    }

    // Successful copy, so we cache the decoderManifest in the Schema object
    // coverity[autosar_cpp14_a20_8_6_violation] can't use make_shared as unique_ptr is moved
    mDecoderManifestListeners.notify( std::shared_ptr<DecoderManifestIngestion>( std::move( decoderManifestPtr ) ) );
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

    auto collectionSchemeListPtr = std::make_unique<CollectionSchemeIngestionList>();

    // Try to copy the binary data into the collectionSchemeList object
    if ( !collectionSchemeListPtr->copyData( buf, size ) )
    {
        FWE_LOG_ERROR( "CollectionSchemeList copyData from IoT core failed" );
        return;
    }

    mCollectionSchemeListeners.notify(
        // coverity[autosar_cpp14_a20_8_6_violation] can't use make_shared as unique_ptr is moved
        std::shared_ptr<CollectionSchemeIngestionList>( std::move( collectionSchemeListPtr ) ) );
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
    transmitCheckin( std::move( callback ) );
}

void
Schema::transmitCheckin( OnCheckinSentCallback callback )
{
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

    mMqttSender.sendBuffer( mMqttSender.getTopicConfig().checkinsTopic,
                            reinterpret_cast<const uint8_t *>( mProtoCheckinMsgOutput.data() ),
                            mProtoCheckinMsgOutput.size(),
                            [checkinDebugString = std::move( checkinDebugString ),
                             callback = std::move( callback )]( ConnectivityError result ) {
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
