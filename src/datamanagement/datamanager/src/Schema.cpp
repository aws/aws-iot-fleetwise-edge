// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "Schema.h"

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{
using namespace Aws::IoTFleetWise::Platform::Linux;
using namespace Aws::IoTFleetWise::Schemas;
using namespace Aws::IoTFleetWise::OffboardConnectivity;

Schema::Schema( std::shared_ptr<IReceiver> receiverDecoderManifest,
                std::shared_ptr<IReceiver> receiverCollectionSchemeList,
                std::shared_ptr<ISender> sender )
    : mDecoderManifestCb( *this )
    , mCollectionSchemeListCb( *this )
    , mSender( std::move( sender ) )
{
    // Register the listeners
    receiverCollectionSchemeList->subscribeListener( &mCollectionSchemeListCb );
    receiverDecoderManifest->subscribeListener( &mDecoderManifestCb );
}

void
Schema::setCollectionSchemeList( const CollectionSchemeListPtr collectionSchemeListPtr )
{
    notifyListeners<const ICollectionSchemeListPtr &>( &CollectionSchemeManagementListener::onCollectionSchemeUpdate,
                                                       collectionSchemeListPtr );
}

void
Schema::setDecoderManifest( const DecoderManifestPtr decoderManifestPtr )
{
    notifyListeners<const IDecoderManifestPtr &>( &CollectionSchemeManagementListener::onDecoderManifestUpdate,
                                                  decoderManifestPtr );
}

bool
Schema::sendCheckin( const std::vector<std::string> &documentARNs )
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
        return false;
    }
    else
    {
        // transmit the data to the cloud
        FWE_LOG_TRACE( "Sending a Checkin message to the backend" );
        return transmitCheckin();
    }
}

bool
Schema::transmitCheckin()
{
    if ( mSender == nullptr )
    {
        FWE_LOG_ERROR( "Invalid sender instance" );
        return false;
    }

    auto res = mSender->sendBuffer( reinterpret_cast<const uint8_t *>( mProtoCheckinMsgOutput.data() ),
                                    mProtoCheckinMsgOutput.size() );

    if ( res == ConnectivityError::Success )
    {
        FWE_LOG_TRACE( "Checkin Message sent to the backend" );

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

        FWE_LOG_TRACE( checkinDebugString );
        return true;
    }
    else if ( res == ConnectivityError::NoConnection )
    {
        return false;
    }
    else
    {
        FWE_LOG_ERROR( "offboardconnectivity error, will retry sending the checkin message" );
        return false;
    }
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
