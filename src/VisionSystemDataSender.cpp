// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "VisionSystemDataSender.h"
#include "CollectionInspectionAPITypes.h"
#include "IConnectionTypes.h"
#include "LoggingModule.h"
#include "QueueTypes.h"
#include "SignalTypes.h"
#include "StreambufBuilder.h"
#include <boost/variant.hpp>
#include <cstdint>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

class VisionSystemDataToPersist : public DataToPersist
{
public:
    VisionSystemDataToPersist( std::string objectKey,
                               // coverity[pass_by_value] conflicts with clang-tidy's modernize-pass-by-value rule
                               S3UploadParams s3UploadParams,
                               std::shared_ptr<std::streambuf> data )
        : mObjectKey( std::move( objectKey ) )
        , mS3UploadParams( std::move( s3UploadParams ) )
        , mData( std::move( data ) )
    {
    }

    SenderDataType
    getDataType() const override
    {
        return SenderDataType::VISION_SYSTEM;
    }

    Json::Value
    getMetadata() const override
    {
        // VisionSystemData persistency is not fully supported yet. When it is this should contain
        // additional metadata to allow retrying this data.
        Json::Value metadata;
        return metadata;
    }

    std::string
    getFilename() const override
    {
        return mObjectKey;
    }

    boost::variant<std::shared_ptr<std::string>, std::shared_ptr<std::streambuf>>
    getData() const override
    {
        return mData;
    }

private:
    std::string mObjectKey;
    S3UploadParams mS3UploadParams;
    std::shared_ptr<std::streambuf> mData;
};

VisionSystemDataSender::VisionSystemDataSender( std::shared_ptr<DataSenderQueue> uploadedS3Objects,
                                                std::shared_ptr<S3Sender> s3Sender,
                                                std::shared_ptr<DataSenderIonWriter> ionWriter,
                                                std::string vehicleName )
    : mUploadedS3Objects( std::move( uploadedS3Objects ) )
    , mIonWriter( std::move( ionWriter ) )
    , mS3Sender{ std::move( s3Sender ) }
    , mVehicleName( std::move( vehicleName ) )

{
}

void
VisionSystemDataSender::processData( std::shared_ptr<const DataToSend> data, OnDataProcessedCallback callback )
{
    if ( data == nullptr )
    {
        FWE_LOG_WARN( "Nothing to send as the input is empty" );
        return;
    }

    auto triggeredVisionSystemData = std::dynamic_pointer_cast<const TriggeredVisionSystemData>( data );
    if ( triggeredVisionSystemData == nullptr )
    {
        FWE_LOG_WARN( "Nothing to send as the input is not a valid TriggeredVisionSystemData" );
        return;
    }

    std::string firstSignalValues = "[";
    uint32_t signalPrintCounter = 0;
    uint32_t maxNumberOfSignalsToTrace = 6;
    std::string firstSignalTimestamp;
    for ( auto &s : triggeredVisionSystemData->signals )
    {
        if ( firstSignalTimestamp.empty() )
        {
            firstSignalTimestamp = " first signal timestamp: " + std::to_string( s.receiveTime );
        }
        signalPrintCounter++;
        if ( signalPrintCounter > maxNumberOfSignalsToTrace )
        {
            firstSignalValues += " ...";
            break;
        }
        auto signalValue = s.getValue();
        firstSignalValues += std::to_string( s.signalID ) + ":";
        switch ( signalValue.getType() )
        {
        case SignalType::COMPLEX_SIGNAL:
            firstSignalValues += std::to_string( signalValue.value.uint32Val ) + ",";
            break;
        default:
            // Only raw data is used for VisionSystemData
            break;
        }
    }
    firstSignalValues += "]";

    FWE_LOG_INFO(
        "FWE VisionSystemData ready to be sent with eventID " + std::to_string( triggeredVisionSystemData->eventID ) +
        " from " + triggeredVisionSystemData->metadata.collectionSchemeID +
        " Signals:" + std::to_string( triggeredVisionSystemData->signals.size() ) + " " + firstSignalValues +
        firstSignalTimestamp + " trigger timestamp: " + std::to_string( triggeredVisionSystemData->triggerTime ) );

    transformVisionSystemDataToIon( triggeredVisionSystemData, callback );
}

void
VisionSystemDataSender::onChangeCollectionSchemeList(
    const std::shared_ptr<const ActiveCollectionSchemes> &activeCollectionSchemes )
{
    FWE_LOG_INFO( "New active collection scheme list was handed over to Data Sender" );
    std::lock_guard<std::mutex> lock( mActiveCollectionSchemeMutex );
    mActiveCollectionSchemes = activeCollectionSchemes;
}

S3UploadMetadata
VisionSystemDataSender::getS3UploadMetadataForCollectionScheme( const std::string &collectionSchemeID )
{
    std::lock_guard<std::mutex> lock( mActiveCollectionSchemeMutex );
    if ( mActiveCollectionSchemes != nullptr )
    {
        for ( const auto &scheme : mActiveCollectionSchemes->activeCollectionSchemes )
        {
            if ( scheme->getCollectionSchemeID() == collectionSchemeID )
            {
                return scheme->getS3UploadMetadata();
            }
        }
    }
    return {};
}

void
VisionSystemDataSender::transformVisionSystemDataToIon(
    std::shared_ptr<const TriggeredVisionSystemData> triggeredVisionSystemData, OnDataProcessedCallback callback )
{
    if ( triggeredVisionSystemData->signals.empty() )
    {
        return;
    }
    if ( mS3Sender == nullptr )
    {
        FWE_LOG_ERROR( "Can not send data to S3 as S3Sender is not initalized. Please make sure config parameters in "
                       "section credentialsProvider are correct" );
        return;
    }
    if ( mIonWriter == nullptr )
    {
        FWE_LOG_WARN( "IonWriter is not set for the upload to S3" );
        return;
    }
    bool rawDataAvailableToSend = false;
    bool vehicleDataIsSet = false;
    // Append signals with raw data to Ion file
    for ( const auto &signal : triggeredVisionSystemData->signals )
    {
        if ( signal.value.type != SignalType::COMPLEX_SIGNAL )
        {
            continue;
        }
        rawDataAvailableToSend = true;
        if ( !vehicleDataIsSet )
        {
            vehicleDataIsSet = true;
            // Setup the next Ion file data only once
            mIonWriter->setupVehicleData( triggeredVisionSystemData );
        }
        mIonWriter->append( signal );
    }
    if ( !rawDataAvailableToSend )
    {
        return;
    }

    auto s3UploadMetadata =
        getS3UploadMetadataForCollectionScheme( triggeredVisionSystemData->metadata.collectionSchemeID );
    if ( s3UploadMetadata == S3UploadMetadata() )
    {
        FWE_LOG_WARN( "Collection scheme " + triggeredVisionSystemData->metadata.collectionSchemeID +
                      " no longer active" );
        return;
    }

    // Get stream for the Ion file and upload it with S3 sender
    auto streambufBuilder = mIonWriter->getStreambufBuilder();

    std::string objectKey = s3UploadMetadata.prefix + std::to_string( triggeredVisionSystemData->eventID ) + "-" +
                            std::to_string( triggeredVisionSystemData->triggerTime ) + &DEFAULT_KEY_SUFFIX[0];
    auto resultCallback = [this, objectKey, triggeredVisionSystemData, callback](
                              ConnectivityError result, std::shared_ptr<std::streambuf> data ) -> void {
        if ( result != ConnectivityError::Success )
        {
            std::shared_ptr<const VisionSystemDataToPersist> dataToPersist;
            if ( triggeredVisionSystemData->metadata.persist && data != nullptr )
            {
                dataToPersist = std::make_shared<VisionSystemDataToPersist>( objectKey, S3UploadParams(), data );
            }
            callback( false, dataToPersist );
            return;
        }

        auto collectedData = std::make_shared<TriggeredCollectionSchemeData>();
        collectedData->metadata = triggeredVisionSystemData->metadata;
        collectedData->eventID = triggeredVisionSystemData->eventID;
        collectedData->triggerTime = triggeredVisionSystemData->triggerTime;
        collectedData->uploadedS3Objects.push_back( UploadedS3Object{ objectKey, UploadedS3ObjectDataFormat::Cdr } );
        if ( !mUploadedS3Objects->push( std::move( collectedData ) ) )
        {
            FWE_LOG_ERROR( "Collected data output buffer is full" );
        }
        callback( true, nullptr );
    };
    mS3Sender->sendStream( std::move( streambufBuilder ), s3UploadMetadata, objectKey, resultCallback );
}

void
VisionSystemDataSender::processPersistedData( std::istream &data,
                                              const Json::Value &metadata,
                                              OnPersistedDataProcessedCallback callback )
{
    static_cast<void>( data );
    static_cast<void>( metadata );
    static_cast<void>( callback );

    FWE_LOG_WARN( "Upload of persisted data is not supported for VisionSystemData" );
}

} // namespace IoTFleetWise
} // namespace Aws
