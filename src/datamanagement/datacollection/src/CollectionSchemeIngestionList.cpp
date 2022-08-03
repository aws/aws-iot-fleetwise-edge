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

#include "CollectionSchemeIngestionList.h"
#include <exception>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{

const std::vector<ICollectionSchemePtr> &
CollectionSchemeIngestionList::getCollectionSchemes() const
{
    if ( !mReady )
    {
        return EMPTY_COLLECTION_SCHEME_LIST;
    }

    return mVectorCollectionSchemePtr;
}

CollectionSchemeIngestionList::~CollectionSchemeIngestionList()
{
    // delete any global objects that were allocated by the Protocol Buffer library
    google::protobuf::ShutdownProtobufLibrary();
}

bool
CollectionSchemeIngestionList::copyData( const std::uint8_t *inputBuffer, const size_t size )
{
    // Set the ready flag to false, as we have new data that needs to be parsed
    mReady = false;

    // check for a null input buffer or size set to 0
    if ( inputBuffer == nullptr || size == 0 )
    {
        // Error, input buffer empty or invalid
        mLogger.error( "CollectionSchemeIngestionList::copyData()", "Input buffer empty" );
        return false;
    }

    // We have to guard against document sizes that are too large
    if ( size > COLLECTION_SCHEME_LIST_BYTE_SIZE_LIMIT )
    {
        mLogger.error( "CollectionSchemeIngestionList::copyData()", "Collection Schema document is too big. Ignoring" );
        return false;
    }

    // Copy the data of the inputBuffer to mProtoBinaryData
    mProtoBinaryData.assign( inputBuffer, inputBuffer + size );

    // Check to make sure the vector size is the same as our input size
    if ( mProtoBinaryData.size() != size )
    {
        mLogger.error( "CollectionSchemeIngestionList::copyData()", "Copied data size doesn't match." );
        return false;
    }

    mLogger.trace( "CollectionSchemeIngestionList::copyData()", "Collection Scheme Data copied successfully." );

    return true;
}

bool
CollectionSchemeIngestionList::isReady() const
{
    return mReady;
}

bool
CollectionSchemeIngestionList::build()
{
    // In this function we parse the binary proto data and make a list of collectionSchemes to send to
    // CollectionSchemeManagement

    // Verify we have not accidentally linked against a version of the library which is incompatible with the
    // version of the headers we compiled with.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    mLogger.trace( "CollectionSchemeIngestionList::build()", "Building CollectionScheme List" );

    // Ensure that we have data to parse
    if ( mProtoBinaryData.empty() || mProtoBinaryData.data() == nullptr )
    {
        mLogger.error( "CollectionSchemeIngestionList::build()", "Input buffer empty" );
        // Error, input buffer empty or invalid
        return false;
    }

    // Try to parse the binary data into our mProtoDecoderManifest member variable
    if ( !mCollectionSchemeListMsg.ParseFromArray( mProtoBinaryData.data(),
                                                   static_cast<int>( mProtoBinaryData.size() ) ) )
    {
        // Error parsing proto binary
        mLogger.error( "CollectionSchemeIngestionList::build()", "Error parsing collectionSchemes.proto binary" );
        return false;
    }

    // Ensure we start with an empty vector of collectionScheme pointers
    mVectorCollectionSchemePtr.clear();

    // Iterate through all the collectionSchemes in the collectionScheme list, make a shared pointer of them
    for ( int i = 0; i < mCollectionSchemeListMsg.collection_schemes_size(); i++ )
    {
        // Create a CollectionSchemeIngestion Pointer, or pICPPtr.
        auto pICPPtr = std::make_shared<CollectionSchemeIngestion>();

        // Stuff the pointer with the collectionScheme proto message data
        pICPPtr->copyData( std::make_shared<CollectionSchemesMsg::CollectionScheme>(
            mCollectionSchemeListMsg.collection_schemes( i ) ) );

        // Check to see if it successfully builds
        if ( pICPPtr->build() )
        {
            mLogger.trace( "CollectionSchemeIngestionList::build()",
                           "Adding CollectionScheme index: " + std::to_string( i ) + " of " +
                               std::to_string( mCollectionSchemeListMsg.collection_schemes_size() ) );

            // Add this newly created shared pointer to the vector of ICollectionScheme shared pointers.
            // It is implicitly upcasted to its base pointer
            mVectorCollectionSchemePtr.emplace_back( pICPPtr );
        }
        else
        {
            mLogger.error( "CollectionSchemeIngestionList::build()",
                           "CollectionScheme index: " + std::to_string( i ) + " failed to build. Dropping it. " );
        }
    }

    mLogger.trace( "CollectionSchemeIngestionList::build()", "Building CollectionScheme List complete." );

    // Set the ready flag to true, as the collection collectionSchemes are ready to read
    mReady = true;

    return true;
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
