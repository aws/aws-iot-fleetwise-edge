// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aws/core/client/ClientConfiguration.h>
#include <aws/transfer/TransferManager.h>
#include <functional>
#include <memory>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief   A wrapper around TransferManager so that we can provide different implementations.
 *
 * The original TransferManager can't be inherited from because it only declares private constructors.
 **/
class TransferManagerWrapper
{
public:
    /**
     * @param transferManager the transfer manager instance to be wrapped
     */
    TransferManagerWrapper( std::shared_ptr<Aws::Transfer::TransferManager> transferManager )
        : mTransferManager( std::move( transferManager ) ){};
    virtual ~TransferManagerWrapper() = default;

    TransferManagerWrapper() = delete;
    TransferManagerWrapper( const TransferManagerWrapper & ) = delete;
    TransferManagerWrapper &operator=( const TransferManagerWrapper & ) = delete;
    TransferManagerWrapper( TransferManagerWrapper && ) = delete;
    TransferManagerWrapper &operator=( TransferManagerWrapper && ) = delete;

    virtual Aws::Transfer::TransferStatus
    WaitUntilAllFinished( int64_t timeoutMs = std::numeric_limits<int64_t>::max() )
    {
        return mTransferManager->WaitUntilAllFinished( timeoutMs );
    }

    virtual void
    CancelAll()
    {
        return mTransferManager->CancelAll();
    }

    virtual std::shared_ptr<Aws::Transfer::TransferHandle>
    // coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
    UploadFile( const Aws::String &fileName,
                const Aws::String &bucketName,
                const Aws::String &keyName,
                const Aws::String &contentType,
                const Aws::Map<Aws::String, Aws::String> &metadata,
                // coverity[autosar_cpp14_a8_4_13_violation] smart pointer needed to match the expected signature
                const std::shared_ptr<const Aws::Client::AsyncCallerContext> &context = nullptr )
    {
        return mTransferManager->UploadFile( fileName, bucketName, keyName, contentType, metadata, context );
    };

    virtual std::shared_ptr<Aws::Transfer::TransferHandle>
    // coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
    UploadFile(
        // coverity[autosar_cpp14_a8_4_13_violation] smart pointer needed to match the expected signature
        const std::shared_ptr<Aws::IOStream> &stream,
        const Aws::String &bucketName,
        const Aws::String &keyName,
        const Aws::String &contentType,
        const Aws::Map<Aws::String, Aws::String> &metadata,
        // coverity[autosar_cpp14_a8_4_13_violation] smart pointer needed to match the expected signature
        const std::shared_ptr<const Aws::Client::AsyncCallerContext> &context = nullptr )
    {
        return mTransferManager->UploadFile( stream, bucketName, keyName, contentType, metadata, context );
    };

    virtual std::shared_ptr<Aws::Transfer::TransferHandle>
    // coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
    RetryUpload( const Aws::String &fileName,
                 // coverity[autosar_cpp14_a8_4_13_violation] smart pointer needed to match the expected signature
                 const std::shared_ptr<Aws::Transfer::TransferHandle> &retryHandle )
    {

        return mTransferManager->RetryUpload( fileName, retryHandle );
    };

    virtual std::shared_ptr<Aws::Transfer::TransferHandle>
    // coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
    RetryUpload(
        // coverity[autosar_cpp14_a8_4_13_violation] smart pointer needed to match the expected signature
        const std::shared_ptr<Aws::IOStream> &stream,
        // coverity[autosar_cpp14_a8_4_13_violation] smart pointer needed to match the expected signature
        const std::shared_ptr<Aws::Transfer::TransferHandle> &retryHandle )
    {
        return mTransferManager->RetryUpload( stream, retryHandle );
    };

    virtual void
    DownloadToDirectory( const Aws::String &directory,
                         const Aws::String &bucketName,
                         const Aws::String &prefix = Aws::String() )
    {
        mTransferManager->DownloadToDirectory( directory, bucketName, prefix );
    }

    virtual std::shared_ptr<Aws::Transfer::TransferHandle>
    // coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
    DownloadFile( const Aws::String &bucketName,
                  const Aws::String &keyName,
                  const Aws::String &writeToFile,
                  const Aws::Transfer::DownloadConfiguration &downloadConfig = Aws::Transfer::DownloadConfiguration(),
                  // coverity[autosar_cpp14_a8_4_13_violation] smart pointer needed to match the expected signature
                  const std::shared_ptr<const Aws::Client::AsyncCallerContext> &context = nullptr )
    {
        return mTransferManager->DownloadFile( bucketName, keyName, writeToFile, downloadConfig, context );
    }

private:
    std::shared_ptr<Aws::Transfer::TransferManager> mTransferManager;
};

using CreateTransferManagerWrapper = std::function<std::shared_ptr<TransferManagerWrapper>(
    Aws::Client::ClientConfiguration &clientConfiguration,
    Aws::Transfer::TransferManagerConfiguration &transferManagerConfiguration )>;

} // namespace IoTFleetWise
} // namespace Aws
