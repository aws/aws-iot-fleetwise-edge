// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionSchemeIngestionList.h"
#include "CollectionSchemeManager.h" // IWYU pragma: associated
#include "DecoderManifestIngestion.h"
#include "EnumUtility.h"
#include "LoggingModule.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

bool
CollectionSchemeManager::retrieve( DataType retrieveType )
{
    size_t protoSize = 0;
    ErrorCode ret = ErrorCode::SUCCESS;
    std::vector<uint8_t> protoOutput;
    std::string infoStr;
    std::string errStr;

    if ( mSchemaPersistency == nullptr )
    {
        FWE_LOG_INFO( "Persistency module not available" );
        return false;
    }
    switch ( retrieveType )
    {
    case DataType::COLLECTION_SCHEME_LIST:
        infoStr = "Retrieved a CollectionSchemeList of size ";
        errStr = "Failed to retrieve the CollectionSchemeList from the persistency module due to an error: ";
        break;
    case DataType::DECODER_MANIFEST:
        infoStr = "Retrieved a DecoderManifest of size ";
        errStr = "Failed to retrieve the DecoderManifest from the persistency module due to an error: ";
        break;
    default:
        FWE_LOG_ERROR( "Unknown data type: " + std::to_string( toUType( retrieveType ) ) );
        return false;
    }

    protoSize = mSchemaPersistency->getSize( retrieveType );
    if ( protoSize <= 0 )
    {
        FWE_LOG_INFO( infoStr + "zero" );
        return false;
    }
    protoOutput.resize( protoSize );
    ret = mSchemaPersistency->read( protoOutput.data(), protoSize, retrieveType );
    if ( ret != ErrorCode::SUCCESS )
    {
        auto error = mSchemaPersistency->getErrorString( ret );
        errStr += error != nullptr ? error : "Unknown error";
        FWE_LOG_ERROR( errStr );
        return false;
    }
    FWE_LOG_INFO( infoStr + std::to_string( protoSize ) + " successfully" );
    if ( retrieveType == DataType::COLLECTION_SCHEME_LIST )
    {
        // updating mCollectionSchemeList
        if ( mCollectionSchemeList == nullptr )
        {
            mCollectionSchemeList = std::make_shared<CollectionSchemeIngestionList>();
        }
        mCollectionSchemeList->copyData( protoOutput.data(), protoSize );
        mProcessCollectionScheme = true;
    }
    // coverity[autosar_cpp14_m0_1_9_violation] - Second if-statement always follows same path as first
    // coverity[misra_cpp_2008_rule_0_1_9_violation] - Second if-statement always follows same path as first
    else if ( retrieveType == DataType::DECODER_MANIFEST )
    {
        // updating mDecoderManifest
        if ( mDecoderManifest == nullptr )
        {
            mDecoderManifest = std::make_shared<DecoderManifestIngestion>();
        }
        mDecoderManifest->copyData( protoOutput.data(), protoSize );
        mProcessDecoderManifest = true;
    }
    return true;
}

void
CollectionSchemeManager::store( DataType storeType )
{
    ErrorCode ret = ErrorCode::SUCCESS;
    std::vector<uint8_t> protoInput;
    std::string logStr;

    if ( mSchemaPersistency == nullptr )
    {
        FWE_LOG_INFO( "Persistency module not available" );
        return;
    }
    if ( ( storeType == DataType::COLLECTION_SCHEME_LIST ) && ( mCollectionSchemeList == nullptr ) )
    {
        FWE_LOG_ERROR( "Invalid CollectionSchemeList" );
        return;
    }
    if ( ( storeType == DataType::DECODER_MANIFEST ) && ( mDecoderManifest == nullptr ) )
    {
        FWE_LOG_ERROR( "Invalid DecoderManifest" );
        return;
    }
    switch ( storeType )
    {
    case DataType::COLLECTION_SCHEME_LIST:
        protoInput = mCollectionSchemeList->getData();
        logStr = "The CollectionSchemeList";
        break;
    case DataType::DECODER_MANIFEST:
        protoInput = mDecoderManifest->getData();
        logStr = "The DecoderManifest";
        break;
    default:
        FWE_LOG_ERROR( "cannot store unsupported type of " + std::to_string( toUType( storeType ) ) );
        return;
    }

    if ( protoInput.empty() )
    {
        FWE_LOG_ERROR( logStr + " data size is zero" );
        return;
    }
    ret = mSchemaPersistency->write( protoInput.data(), protoInput.size(), storeType );
    if ( ret != ErrorCode::SUCCESS )
    {
        logStr += " because of this error: ";
        auto error = mSchemaPersistency->getErrorString( ret );
        logStr += error != nullptr ? error : "Unknown error";
        FWE_LOG_ERROR( "failed to persist " + logStr );
    }
    else
    {
        FWE_LOG_TRACE( logStr + " persisted successfully" );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
