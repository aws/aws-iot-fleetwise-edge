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
    : mDecoderManifestCb( *this, mLogger )
    , mCollectionSchemeListCb( *this, mLogger )
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
        mProtoCheckinMsg.add_document_arns( doc );
    }

    // Add the timestamp
    mProtoCheckinMsg.set_timestamp_ms_epoch( mClock->timeSinceEpochMs() );

    if ( !mProtoCheckinMsg.SerializeToString( &mProtoCheckinMsgOutput ) )
    {
        mLogger.error( "Schema::sendCheckin", "Checkin serialization failed" );
        return false;
    }
    else
    {
        // transmit the data to the cloud
        mLogger.trace( "Schema::sendCheckin", "Sending a Checkin message to the backend" );
        return transmitCheckin();
    }
}

bool
Schema::transmitCheckin()
{
    if ( mSender == nullptr )
    {
        mLogger.error( "Schema::transmitCheckin", "Invalid sender instance" );
        return false;
    }

    auto res = mSender->send( reinterpret_cast<const uint8_t *>( mProtoCheckinMsgOutput.data() ),
                              mProtoCheckinMsgOutput.size() );

    if ( res == ConnectivityError::Success )
    {
        mLogger.trace( "Schema::transmitCheckin", "Checkin Message sent to the backend" );

        // Trace log for more verbose Checkin Info
        std::string checkinDebugString;
        checkinDebugString = "Checkin data: timestamp: " + std::to_string( mProtoCheckinMsg.timestamp_ms_epoch() );
        checkinDebugString += " with " + std::to_string( mProtoCheckinMsg.document_arns_size() ) + " documents: [";

        for ( int i = 0; i < mProtoCheckinMsg.document_arns_size(); i++ )
        {
            checkinDebugString += " " + mProtoCheckinMsg.document_arns( i );
        }
        checkinDebugString += " ]. ";

        mLogger.trace( "Schema::transmitCheckin", checkinDebugString );
        return true;
    }
    else if ( res == ConnectivityError::NoConnection )
    {
        mLogger.warn( "Schema::transmitCheckin", "Connection not established" );
        return false;
    }
    else
    {
        mLogger.error( "Schema::transmitCheckin",
                       "offboardconnectivity error, will retry sending the checkin message" );
        return false;
    }
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
