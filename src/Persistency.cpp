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

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
#include <utility>
#endif

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
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    case DataType::STATE_TEMPLATE_LIST:
        infoStr = "Retrieved a StateTemplateList of size ";
        errStr = "Failed to retrieve the StateTemplateList from the persistency module due to an error: ";
        break;
#endif
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
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    // coverity[autosar_cpp14_m0_1_9_violation] - Second if-statement always follows same path as first
    // coverity[misra_cpp_2008_rule_0_1_9_violation] - Second if-statement always follows same path as first
    else if ( retrieveType == DataType::STATE_TEMPLATE_LIST )
    {
        if ( mLastKnownStateIngestion == nullptr )
        {
            mLastKnownStateIngestion = std::make_shared<LastKnownStateIngestion>();
        }
        mLastKnownStateIngestion->copyData( protoOutput.data(), protoSize );
        mProcessStateTemplates = true;
    }
#endif
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
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    case DataType::STATE_TEMPLATE_LIST: {
        // Different from the other data, we can't just store mLastKnownStateIngestion->getData()
        // because it is just a diff of the previous ingested messages.
        // So we need to reconstruct a protobuf with all state templates.
        Schemas::LastKnownState::StateTemplates protoStateTemplates;
        protoStateTemplates.set_version( mLastStateTemplatesDiffVersion );
        for ( auto &stateTemplate : mStateTemplates )
        {
            protoStateTemplates.set_decoder_manifest_sync_id( stateTemplate.second->decoderManifestID );
            auto *protoStateTemplate = protoStateTemplates.add_state_templates_to_add();
            protoStateTemplate->set_state_template_sync_id( stateTemplate.first );
            for ( auto &signal : stateTemplate.second->signals )
            {
                protoStateTemplate->add_signal_ids( signal.signalID );
            }
            switch ( stateTemplate.second->updateStrategy )
            {
            case LastKnownStateUpdateStrategy::PERIODIC: {
                auto periodicUpdateStrategy = std::make_unique<Schemas::LastKnownState::PeriodicUpdateStrategy>();
                periodicUpdateStrategy->set_period_ms( stateTemplate.second->periodMs );
                protoStateTemplate->set_allocated_periodic_update_strategy( periodicUpdateStrategy.release() );
                break;
            }
            case LastKnownStateUpdateStrategy::ON_CHANGE: {
                auto onChangeUpdateStrategy = std::make_unique<Schemas::LastKnownState::OnChangeUpdateStrategy>();
                protoStateTemplate->set_allocated_on_change_update_strategy( onChangeUpdateStrategy.release() );
                break;
            }
            }
        }
        protoInput = std::vector<uint8_t>( protoStateTemplates.ByteSizeLong() );
        if ( !protoStateTemplates.SerializeToArray( protoInput.data(), static_cast<int>( protoInput.capacity() ) ) )
        {
            FWE_LOG_ERROR( "Failed to serialize StateTemplateList" );
            return;
        }
        logStr = "The StateTemplateList";
        break;
    }
#endif
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
