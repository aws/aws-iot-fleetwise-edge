// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/LastKnownStateIngestion.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <google/protobuf/message.h>
#include <iterator>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

LastKnownStateIngestion::LastKnownStateIngestion()
    : mStateTemplatesDiff( std::make_shared<StateTemplatesDiff>() )
{
}

LastKnownStateIngestion::~LastKnownStateIngestion()
{
    // delete any global objects that were allocated by the Protocol Buffer library
    google::protobuf::ShutdownProtobufLibrary();
}

bool
LastKnownStateIngestion::isReady() const
{
    return mReady;
}

bool
LastKnownStateIngestion::copyData( const std::uint8_t *inputBuffer, const size_t size )
{
    if ( isReady() )
    {
        return false;
    }

    // It is possible that the data is empty, if, for example, the list of state templates is empty.
    if ( ( inputBuffer == nullptr ) || ( size == 0 ) )
    {
        mProtoBinaryData.clear();
        FWE_LOG_TRACE( "Received empty data" );
        return true;
    }

    // We have to guard against document sizes that are too large
    if ( size > LAST_KNOWN_STATE_BYTE_SIZE_LIMIT )
    {
        FWE_LOG_ERROR( "LastKnownState binary too big. Size: " + std::to_string( size ) +
                       " limit: " + std::to_string( LAST_KNOWN_STATE_BYTE_SIZE_LIMIT ) );
        return false;
    }

    mProtoBinaryData.assign( inputBuffer, inputBuffer + size );

    mReady = false;

    FWE_LOG_TRACE( "Copy of LastKnownState data success" );
    return true;
}

bool
LastKnownStateIngestion::build()
{
    // Verify we have not accidentally linked against a version of the library which is incompatible with the version of
    // the headers we compiled with.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    if ( isReady() )
    {
        return false;
    }

    if ( !mProtoStateTemplates.ParseFromArray( mProtoBinaryData.data(), static_cast<int>( mProtoBinaryData.size() ) ) )
    {
        FWE_LOG_ERROR( "Failed to parse LastKnownState proto" );
        return false;
    }

    auto decoderManifestId = mProtoStateTemplates.decoder_manifest_sync_id();
    if ( decoderManifestId.empty() )
    {
        FWE_LOG_ERROR( "Missing decoder manifest ID" );
        return false;
    }
    mStateTemplatesDiff->version = mProtoStateTemplates.version();

    std::copy( mProtoStateTemplates.state_template_sync_ids_to_remove().begin(),
               mProtoStateTemplates.state_template_sync_ids_to_remove().end(),
               std::back_inserter( mStateTemplatesDiff->stateTemplatesToRemove ) );

    for ( auto &protoStateTemplateInformation : mProtoStateTemplates.state_templates_to_add() )
    {
        auto stateTemplateId = protoStateTemplateInformation.state_template_sync_id();
        if ( stateTemplateId.empty() )
        {
            FWE_LOG_ERROR( "State template does not have a valid ID" );
            continue;
        }
        FWE_LOG_INFO( "Building LastKnownState with ID: " + stateTemplateId );

        auto stateTemplate = std::make_shared<StateTemplateInformation>();
        stateTemplate->id = stateTemplateId;
        stateTemplate->decoderManifestID = decoderManifestId;

        if ( protoStateTemplateInformation.has_periodic_update_strategy() )
        {
            auto periodicUpdateStrategy = protoStateTemplateInformation.periodic_update_strategy();
            stateTemplate->updateStrategy = LastKnownStateUpdateStrategy::PERIODIC;
            stateTemplate->periodMs = periodicUpdateStrategy.period_ms();
            for ( auto &signalID : protoStateTemplateInformation.signal_ids() )
            {
                stateTemplate->signals.emplace_back( LastKnownStateSignalInformation{ signalID } );
            }
        }
        else if ( protoStateTemplateInformation.has_on_change_update_strategy() )
        {
            stateTemplate->updateStrategy = LastKnownStateUpdateStrategy::ON_CHANGE;
            for ( auto &signalID : protoStateTemplateInformation.signal_ids() )
            {
                stateTemplate->signals.emplace_back( LastKnownStateSignalInformation{ signalID } );
            }
        }
        else
        {
            FWE_LOG_ERROR( "Invalid LastKnownState update strategy for state template " + stateTemplate->id );
            continue;
        }

        mStateTemplatesDiff->stateTemplatesToAdd.emplace_back( stateTemplate );
    }

    if ( mStateTemplatesDiff->stateTemplatesToAdd.empty() && mStateTemplatesDiff->stateTemplatesToRemove.empty() )
    {
        FWE_LOG_ERROR( "LastKnownState message has no state templates" );
        return false;
    }

    FWE_LOG_TRACE( "LastKnownState build succeeded" );

    mReady = true;
    return true;
}

std::shared_ptr<const StateTemplatesDiff>
LastKnownStateIngestion::getStateTemplatesDiff() const
{
    return mStateTemplatesDiff;
}

} // namespace IoTFleetWise
} // namespace Aws
