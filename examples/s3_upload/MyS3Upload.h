// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aws/iotfleetwise/TransferManagerWrapper.h>
#include <aws/transfer/TransferManager.h>
#include <functional>
#include <memory>
#include <string>

class MyS3Upload
{
public:
    MyS3Upload(
        std::function<std::shared_ptr<Aws::IoTFleetWise::TransferManagerWrapper>()> createTransferManagerWrapper,
        std::string bucketName );

    void doUpload( const std::string &localFilePath, const std::string &remoteObjectKey );

    void
    // coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
    transferStatusUpdatedCallback( const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle );

    void
    // coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
    transferErrorCallback( const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle,
                           const Aws::Client::AWSError<Aws::S3::S3Errors> &error );

private:
    std::shared_ptr<Aws::IoTFleetWise::TransferManagerWrapper> mTransferManagerWrapper;
    std::string mBucketName;
    std::function<std::shared_ptr<Aws::IoTFleetWise::TransferManagerWrapper>()> mCreateTransferManagerWrapper;
};
