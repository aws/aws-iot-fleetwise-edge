// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "MyS3Upload.h"
#include <aws/core/utils/memory/stl/AWSMap.h>
#include <aws/core/utils/memory/stl/AWSString.h>
#include <aws/crt/io/Bootstrap.h>
#include <aws/iotfleetwise/LoggingModule.h>
#include <sstream>
#include <utility>

MyS3Upload::MyS3Upload(
    std::function<std::shared_ptr<Aws::IoTFleetWise::TransferManagerWrapper>()> createTransferManagerWrapper,
    std::string bucketName )
    : mCreateTransferManagerWrapper( std::move( createTransferManagerWrapper ) )
    , mBucketName( std::move( bucketName ) )
{
}

void
MyS3Upload::doUpload( const std::string &localFilePath, const std::string &remoteObjectKey )
{
    if ( !mTransferManagerWrapper )
    {
        // Only create the TransferManager on demand, as this will obtain credentials from IoT Credentials Provider
        mTransferManagerWrapper = mCreateTransferManagerWrapper();
    }
    FWE_LOG_INFO( "Starting upload of " + localFilePath + " to bucket " + mBucketName + " with object key " +
                  remoteObjectKey );
    mTransferManagerWrapper->UploadFile(
        localFilePath, mBucketName, remoteObjectKey, "application/octet-stream", Aws::Map<Aws::String, Aws::String>() );
}

void
// coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
MyS3Upload::transferStatusUpdatedCallback( const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle )
{
    if ( ( transferHandle->GetStatus() == Aws::Transfer::TransferStatus::NOT_STARTED ) ||
         ( transferHandle->GetStatus() == Aws::Transfer::TransferStatus::IN_PROGRESS ) )
    {
        return;
    }
    if ( transferHandle->GetStatus() != Aws::Transfer::TransferStatus::COMPLETED )
    {
        FWE_LOG_ERROR( "Transfer error for file " + transferHandle->GetTargetFilePath() + ", object " +
                       transferHandle->GetKey() );
        return;
    }
    FWE_LOG_INFO( "Upload complete for file " + transferHandle->GetTargetFilePath() + ", object " +
                  transferHandle->GetKey() );
}

void
// coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
MyS3Upload::transferErrorCallback( const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle,
                                   const Aws::Client::AWSError<Aws::S3::S3Errors> &error )
{
    std::stringstream ss;
    ss << error;
    FWE_LOG_ERROR( "Transfer error for file " + transferHandle->GetTargetFilePath() + ", object " +
                   transferHandle->GetKey() + ": " + ss.str() );
}
