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
    UploadFile( const Aws::String &fileName,
                const Aws::String &bucketName,
                const Aws::String &keyName,
                const Aws::String &contentType,
                const Aws::Map<Aws::String, Aws::String> &metadata,
                const std::shared_ptr<const Aws::Client::AsyncCallerContext> &context = nullptr )
    {
        return mTransferManager->UploadFile( fileName, bucketName, keyName, contentType, metadata, context );
    };

    virtual std::shared_ptr<Aws::Transfer::TransferHandle>
    UploadFile( const std::shared_ptr<Aws::IOStream> &stream,
                const Aws::String &bucketName,
                const Aws::String &keyName,
                const Aws::String &contentType,
                const Aws::Map<Aws::String, Aws::String> &metadata,
                const std::shared_ptr<const Aws::Client::AsyncCallerContext> &context = nullptr )
    {
        return mTransferManager->UploadFile( stream, bucketName, keyName, contentType, metadata, context );
    };

    virtual std::shared_ptr<Aws::Transfer::TransferHandle>
    RetryUpload( const Aws::String &fileName, const std::shared_ptr<Aws::Transfer::TransferHandle> &retryHandle )
    {

        return mTransferManager->RetryUpload( fileName, retryHandle );
    };

    virtual std::shared_ptr<Aws::Transfer::TransferHandle>
    RetryUpload( const std::shared_ptr<Aws::IOStream> &stream,
                 const std::shared_ptr<Aws::Transfer::TransferHandle> &retryHandle )
    {
        return mTransferManager->RetryUpload( stream, retryHandle );
    };

private:
    std::shared_ptr<Aws::Transfer::TransferManager> mTransferManager;
};

using CreateTransferManagerWrapper = std::function<std::shared_ptr<TransferManagerWrapper>(
    Aws::Client::ClientConfiguration &clientConfiguration,
    Aws::Transfer::TransferManagerConfiguration &transferManagerConfiguration )>;

} // namespace IoTFleetWise
} // namespace Aws
