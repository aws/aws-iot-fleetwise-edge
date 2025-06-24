// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/S3Sender.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include "aws/iotfleetwise/TraceModule.h"
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/utils/memory/stl/AWSMap.h>
#include <aws/core/utils/memory/stl/AWSString.h>
#include <aws/crt/io/Bootstrap.h>
#include <aws/s3-crt/model/PutObjectRequest.h>
#include <aws/s3/model/CreateMultipartUploadRequest.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/UploadPartRequest.h>
#include <aws/transfer/TransferManager.h>
#include <istream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
constexpr size_t DEFAULT_MULTIPART_SIZE = 5 * 1024 * 1024; // 5MB
constexpr char ALLOCATION_TAG[] = "FWE_S3Sender";
constexpr uint8_t MAX_ATTEMPTS = 2;
constexpr uint8_t MAX_SIMULTANEOUS_FILES = 1;
constexpr uint32_t DEFAULT_CONNECT_TIMEOUT_MS = 3000; // 3 seconds default
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

S3Sender::S3Sender( CreateTransferManagerWrapper createTransferManagerWrapper,
                    size_t multipartSize,
                    uint32_t connectTimeoutMs )
    : mMultipartSize{ multipartSize == 0 ? DEFAULT_MULTIPART_SIZE : multipartSize }
    , mConnectTimeoutMs{ connectTimeoutMs > 0 ? connectTimeoutMs : DEFAULT_CONNECT_TIMEOUT_MS }
    , mCreateTransferManagerWrapper( std::move( createTransferManagerWrapper ) )
{
    if ( mCreateTransferManagerWrapper == nullptr )
    {
        throw std::invalid_argument( "createTransferManagerWrapper can't be null" );
    }
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

        for ( const auto &ongoingUpload : mOngoingUploads )
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

void
// unique_ptr is indeed used to transfer ownership, but coverity flags the early return path
// coverity[autosar_cpp14_a8_4_11_violation:FALSE]
S3Sender::sendStream( std::unique_ptr<StreambufBuilder> streambufBuilder,
                      const S3UploadMetadata &uploadMetadata,
                      const std::string &objectKey,
                      ResultCallback resultCallback )
{
    {
        std::lock_guard<std::mutex> lock( mQueuedAndOngoingUploadsLookupMutex );

        FWE_LOG_INFO( "Queuing async upload for object " + objectKey + " to the bucket " + uploadMetadata.bucketName +
                      " , Current queue size: " + std::to_string( mQueuedUploads.size() ) );
        mQueuedUploads.push(
            { std::move( streambufBuilder ), uploadMetadata, objectKey, std::move( resultCallback ) } );
        TraceModule::get().setVariable( TraceVariable::QUEUED_S3_OBJECTS, mQueuedUploads.size() );
    }

    submitQueuedUploads();
}

void
S3Sender::submitQueuedUploads()
{
    std::lock_guard<std::mutex> lock( mQueuedAndOngoingUploadsLookupMutex );

    while ( ( mOngoingUploads.size() < MAX_SIMULTANEOUS_FILES ) && ( !mQueuedUploads.empty() ) )
    {
        ResultCallback resultCallback;
        QueuedUploadMetadata &queuedUploadMetadata = mQueuedUploads.front();
        auto streambuf = queuedUploadMetadata.streambufBuilder->build();
        S3UploadMetadata uploadMetadata = queuedUploadMetadata.uploadMetadata;
        std::string objectKey = queuedUploadMetadata.objectKey;
        resultCallback = queuedUploadMetadata.resultCallback;
        mQueuedUploads.pop();
        TraceModule::get().setVariable( TraceVariable::QUEUED_S3_OBJECTS, mQueuedUploads.size() );

        if ( streambuf == nullptr )
        {
            FWE_LOG_WARN( "Skipping upload of object " + objectKey + " to the bucket " + uploadMetadata.bucketName +
                          " because its data is not available anymore" );
            resultCallback( ConnectivityError::WrongInputData, nullptr );
            continue;
        }

        auto transferManagerWrapper = getTransferManagerWrapper( uploadMetadata );

        auto data = Aws::MakeShared<Aws::IOStream>( &ALLOCATION_TAG[0], streambuf.get() );

        if ( !data->good() )
        {
            FWE_LOG_ERROR( "Could not prepare data for the upload of object " + objectKey + " to the bucket " +
                           uploadMetadata.bucketName );
            resultCallback( ConnectivityError::WrongInputData, nullptr );
            continue;
        }

        FWE_LOG_INFO( "Starting async upload for object " + objectKey + " to the bucket " + uploadMetadata.bucketName );

        auto transferHandle = transferManagerWrapper->UploadFile( data,
                                                                  uploadMetadata.bucketName,
                                                                  objectKey,
                                                                  "application/octet-stream",
                                                                  Aws::Map<Aws::String, Aws::String>() );

        // Store streambuf pointer in the member map
        mOngoingUploads[objectKey] = { std::move( streambuf ),
                                       std::move( resultCallback ),
                                       std::move( transferManagerWrapper ),
                                       std::move( transferHandle ),
                                       1 };
    }
}

std::shared_ptr<TransferManagerWrapper>
S3Sender::getTransferManagerWrapper( const S3UploadMetadata &uploadMetadata )
{
    if ( mRegionToTransferManagerWrapper.find( uploadMetadata.region ) == mRegionToTransferManagerWrapper.end() )
    {
        FWE_LOG_INFO( "Creating new S3 client for region " + uploadMetadata.region );

        Aws::Client::ClientConfigurationInitValues initValues;
        // The SDK can use IMDS to determine the region, but since we will pass the region we don't
        // want the SDK to use it, specially because in non-EC2 environments without any AWS SDK
        // config at all, this can cause delays when setting up the client:
        // https://github.com/aws/aws-sdk-cpp/issues/1511
        initValues.shouldDisableIMDS = true;
        Aws::Client::ClientConfiguration clientConfig( initValues );
        clientConfig.region = uploadMetadata.region;

        clientConfig.connectTimeoutMs = static_cast<long int>( mConnectTimeoutMs );

        Aws::Transfer::TransferManagerConfiguration transferConfig( nullptr );
        transferConfig.bufferSize = mMultipartSize;

        Aws::S3::Model::PutObjectRequest putObjectTemplate;
        putObjectTemplate.WithExpectedBucketOwner( uploadMetadata.bucketOwner );
        transferConfig.putObjectTemplate = std::move( putObjectTemplate );

        Aws::S3::Model::CreateMultipartUploadRequest createMultipartUploadTemplate;
        createMultipartUploadTemplate.WithExpectedBucketOwner( uploadMetadata.bucketOwner );
        transferConfig.createMultipartUploadTemplate = std::move( createMultipartUploadTemplate );

        Aws::S3::Model::UploadPartRequest uploadPartTemplate;
        uploadPartTemplate.WithExpectedBucketOwner( uploadMetadata.bucketOwner );
        transferConfig.uploadPartTemplate = std::move( uploadPartTemplate );

        transferConfig.transferStatusUpdatedCallback =
            // coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
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
// coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
S3Sender::transferStatusUpdatedCallback( const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle )
{
    // coverity[check_return] Status can be used directly without being checked
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

    ResultCallback resultCallback;
    std::shared_ptr<std::streambuf> streambuf;
    {
        std::lock_guard<std::mutex> lock( mQueuedAndOngoingUploadsLookupMutex );
        resultCallback = mOngoingUploads[transferHandle->GetKey()].resultCallback;
        streambuf = mOngoingUploads[transferHandle->GetKey()].streambuf;
        mOngoingUploads.erase( transferHandle->GetKey() );
    }

    submitQueuedUploads();

    resultCallback( result ? ConnectivityError::Success : ConnectivityError::TransmissionError,
                    std::move( streambuf ) );
}

} // namespace IoTFleetWise
} // namespace Aws
