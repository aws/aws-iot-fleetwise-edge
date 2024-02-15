// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "S3Sender.h"
#include "LoggingModule.h"
#include <aws/core/utils/memory/stl/AWSMap.h>
#include <aws/crt/io/Bootstrap.h>
#include <aws/s3-crt/model/PutObjectRequest.h>
#include <aws/s3/model/CreateMultipartUploadRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/UploadPartRequest.h>
#include <aws/transfer/TransferHandle.h>
#include <aws/transfer/TransferManager.h>
#include <istream>
#include <utility>
#include <vector>

namespace
{
constexpr size_t DEFAULT_MULTIPART_SIZE = 5 * 1024 * 1024; // 5MB
constexpr char ALLOCATION_TAG[] = "FWE_S3Sender";
constexpr uint8_t MAX_ATTEMPTS = 2;
constexpr uint8_t MAX_SIMULTANEOUS_FILES = 1;
} // namespace

namespace Aws
{
namespace IoTFleetWise
{

static std::string
transferStatusToString( Aws::Transfer::TransferStatus transferStatus )
{
    std::stringstream ss;
    ss << transferStatus;
    return ss.str();
}

S3Sender::S3Sender(

    std::shared_ptr<PayloadManager> payloadManager,
    std::function<std::shared_ptr<TransferManagerWrapper>(
        Aws::Client::ClientConfiguration &clientConfiguration,
        Aws::Transfer::TransferManagerConfiguration &transferManagerConfiguration )> createTransferManagerWrapper,
    size_t multipartSize )
    : mMultipartSize{ multipartSize == 0 ? DEFAULT_MULTIPART_SIZE : multipartSize }
    , mPayloadManager( std::move( payloadManager ) )
    , mCreateTransferManagerWrapper( std::move( createTransferManagerWrapper ) )
{
}

bool
S3Sender::disconnect()
{
    FWE_LOG_INFO( "Disconnecting the S3 client" );

    std::vector<std::shared_ptr<TransferManagerWrapper>> transferManagers;

    {
        std::lock_guard<std::mutex> lock( mQueuedAndOngoingUploadsLookupMutex );

        // TODO: queued uploads should be persisted
        mQueuedUploads = std::queue<QueuedUploadMetadata>();

        for ( auto ongoingUpload : mOngoingUploads )
        {
            FWE_LOG_INFO( "Ongoing upload will be canceled for object " +
                          ongoingUpload.second.transferHandle->GetKey() );
            transferManagers.push_back( ongoingUpload.second.transferManagerWrapper );
        }
    }

    for ( auto transferManager : transferManagers )
    {
        FWE_LOG_INFO( "Cancelling all ongoing uploads and waiting for them to finish" );
        transferManager->CancelAll();
        transferManager->WaitUntilAllFinished();
    }
    FWE_LOG_INFO( "S3Sender disconnected successfully" );

    return true;
}

ConnectivityError
S3Sender::sendStream( std::unique_ptr<StreambufBuilder> streambufBuilder,
                      const S3UploadMetadata &uploadMetadata,
                      const std::string &objectKey,
                      std::function<void( bool success )> resultCallback )
{
    if ( streambufBuilder == nullptr )
    {
        FWE_LOG_ERROR( "No valid streambuf builder provided for the upload" );
        return ConnectivityError::WrongInputData;
    }

    if ( mCreateTransferManagerWrapper == nullptr )
    {
        FWE_LOG_ERROR( "No S3 client configured" );
        persistS3Request( std::move( streambufBuilder ), objectKey );
        return ConnectivityError::NotConfigured;
    }

    {
        std::lock_guard<std::mutex> lock( mQueuedAndOngoingUploadsLookupMutex );

        FWE_LOG_INFO( "Queuing async upload for object " + objectKey + " to the bucket " + uploadMetadata.bucketName );
        mQueuedUploads.push( { std::move( streambufBuilder ), uploadMetadata, objectKey, resultCallback } );
    }

    submitQueuedUploads();

    return ConnectivityError::Success;
} // namespace IoTFleetWise

void
S3Sender::submitQueuedUploads()
{
    std::lock_guard<std::mutex> lock( mQueuedAndOngoingUploadsLookupMutex );

    while ( ( mOngoingUploads.size() < MAX_SIMULTANEOUS_FILES ) && ( !mQueuedUploads.empty() ) )
    {
        QueuedUploadMetadata &queuedUploadMetadata = mQueuedUploads.front();
        auto streambuf = queuedUploadMetadata.streambufBuilder->build();
        auto uploadMetadata = queuedUploadMetadata.uploadMetadata;
        auto objectKey = queuedUploadMetadata.objectKey;
        auto resultCallback = queuedUploadMetadata.resultCallback;
        mQueuedUploads.pop();

        if ( streambuf == nullptr )
        {
            FWE_LOG_WARN( "Skipping upload of object " + objectKey + " to the bucket " + uploadMetadata.bucketName +
                          " because its data is not available anymore" );
            continue;
        }

        auto transferManagerWrapper = getTransferManagerWrapper( uploadMetadata );

        auto data = Aws::MakeShared<Aws::IOStream>( &ALLOCATION_TAG[0], streambuf.get() );

        if ( !data->good() )
        {
            FWE_LOG_ERROR( "Could not prepare data for the upload of object " + objectKey + " to the bucket " +
                           uploadMetadata.bucketName );
            continue;
        }

        FWE_LOG_INFO( "Starting async upload for object " + objectKey + " to the bucket " + uploadMetadata.bucketName );

        auto transferHandle = transferManagerWrapper->UploadFile( data,
                                                                  uploadMetadata.bucketName,
                                                                  objectKey,
                                                                  "application/octet-stream",
                                                                  Aws::Map<Aws::String, Aws::String>() );

        // Store streambuf pointer in the member map
        mOngoingUploads[objectKey] = {
            std::move( streambuf ), resultCallback, transferManagerWrapper, transferHandle, 1 };
    }
}

std::shared_ptr<TransferManagerWrapper>
S3Sender::getTransferManagerWrapper( const S3UploadMetadata &uploadMetadata )
{
    if ( mRegionToTransferManagerWrapper.find( uploadMetadata.region ) == mRegionToTransferManagerWrapper.end() )
    {
        FWE_LOG_INFO( "Creating new S3 client for region " + uploadMetadata.region );

        Aws::Client::ClientConfiguration clientConfig;
        clientConfig.region = uploadMetadata.region;

        Aws::Transfer::TransferManagerConfiguration transferConfig( nullptr );
        transferConfig.bufferSize = mMultipartSize;

        Aws::S3::Model::PutObjectRequest putObjectTemplate;
        putObjectTemplate.WithExpectedBucketOwner( uploadMetadata.bucketOwner );
        transferConfig.putObjectTemplate = putObjectTemplate;

        Aws::S3::Model::CreateMultipartUploadRequest createMultipartUploadTemplate;
        createMultipartUploadTemplate.WithExpectedBucketOwner( uploadMetadata.bucketOwner );
        transferConfig.createMultipartUploadTemplate = createMultipartUploadTemplate;

        Aws::S3::Model::UploadPartRequest uploadPartTemplate;
        uploadPartTemplate.WithExpectedBucketOwner( uploadMetadata.bucketOwner );
        transferConfig.uploadPartTemplate = uploadPartTemplate;

        transferConfig.transferStatusUpdatedCallback =
            [this]( const Aws::Transfer::TransferManager *transferManager,
                    const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle ) {
                static_cast<void>( transferManager );
                transferStatusUpdatedCallback( transferHandle );
            };

        mRegionToTransferManagerWrapper[uploadMetadata.region] =
            mCreateTransferManagerWrapper( clientConfig, transferConfig );
    }

    return mRegionToTransferManagerWrapper[uploadMetadata.region];
}

void
S3Sender::transferStatusUpdatedCallback( const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle )
{
    auto transferStatus = transferHandle->GetStatus();
    FWE_LOG_TRACE( "Transfer status for object " + transferHandle->GetKey() +
                   " updated: " + transferStatusToString( transferStatus ) +
                   "; CompletedParts: " + std::to_string( transferHandle->GetCompletedParts().size() ) +
                   "; PendingParts: " + std::to_string( transferHandle->GetPendingParts().size() ) +
                   "; QueuedParts: " + std::to_string( transferHandle->GetQueuedParts().size() ) +
                   "; FailedParts: " + std::to_string( transferHandle->GetFailedParts().size() ) );

    if ( ( transferStatus == Aws::Transfer::TransferStatus::NOT_STARTED ) ||
         ( transferStatus == Aws::Transfer::TransferStatus::IN_PROGRESS ) )
    {
        return;
    }

    if ( ( transferStatus != Aws::Transfer::TransferStatus::CANCELED ) &&
         ( transferStatus != Aws::Transfer::TransferStatus::FAILED ) &&
         ( transferStatus != Aws::Transfer::TransferStatus::COMPLETED ) &&
         ( transferStatus != Aws::Transfer::TransferStatus::ABORTED ) )
    {
        FWE_LOG_ERROR( "Unexpected transfer status '" + transferStatusToString( transferStatus ) + "' for object " +
                       transferHandle->GetKey() );
        return;
    }

    bool result = transferStatus == Aws::Transfer::TransferStatus::COMPLETED;
    if ( result )
    {
        FWE_LOG_INFO( "Finished async upload for object " + transferHandle->GetKey() );
    }
    else
    {
        FWE_LOG_ERROR( "Transfer failed: " + transferHandle->GetLastError().GetMessage() );
        if ( transferStatus == Aws::Transfer::TransferStatus::FAILED )
        {
            std::lock_guard<std::mutex> lock( mQueuedAndOngoingUploadsLookupMutex );
            OngoingUploadMetadata &metadata = mOngoingUploads[transferHandle->GetKey()];
            if ( metadata.attempts < MAX_ATTEMPTS )
            {
                FWE_LOG_INFO( "Retrying upload for object " + transferHandle->GetKey() );

                auto data = Aws::MakeShared<Aws::IOStream>( &ALLOCATION_TAG[0], metadata.streambuf.get() );
                if ( !data->good() )
                {
                    FWE_LOG_ERROR( "Could not prepare data for retrying the upload" );
                }
                else
                {
                    metadata.attempts++;
                    metadata.transferHandle =
                        metadata.transferManagerWrapper->RetryUpload( data, metadata.transferHandle );
                    return;
                }
            }
        }
    }

    std::function<void( bool success )> resultCallback;
    {
        std::lock_guard<std::mutex> lock( mQueuedAndOngoingUploadsLookupMutex );
        resultCallback = mOngoingUploads[transferHandle->GetKey()].resultCallback;
        mOngoingUploads.erase( transferHandle->GetKey() );
    }

    submitQueuedUploads();

    if ( resultCallback )
    {
        resultCallback( result );
    }
}

void
S3Sender::persistS3Request( std::unique_ptr<StreambufBuilder> streambufBuilder, std::string objectKey )
{
    // TODO: persist/retry on fail. Retry will only be executed as upload of the persisted data.
    // Temp change: dump data from stream into the file here
    if ( mPayloadManager == nullptr )
    {
        FWE_LOG_WARN( "Could not persist data for object " + objectKey + " : Payload manager is not configured." );
        return;
    }
    if ( streambufBuilder == nullptr )
    {
        FWE_LOG_WARN( "Could not persist data for object " + objectKey + " : Invalid streambuf builder provided." );
        return;
    }

    auto streambuf = streambufBuilder->build();
    if ( streambuf == nullptr )
    {
        FWE_LOG_WARN( "Could not persist data for object " + objectKey + " : Invalid stream." );
        return;
    }

    streambuf->pubseekoff( std::streambuf::off_type( 0 ), std::ios_base::beg, std::ios_base::in );
    mPayloadManager->storeIonData( std::move( streambuf ), objectKey );
}

} // namespace IoTFleetWise
} // namespace Aws
