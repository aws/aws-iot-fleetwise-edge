// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ICollectionScheme.h"
#include "IConnectionTypes.h"
#include "PayloadManager.h"
#include "StreambufBuilder.h"
#include "TransferManagerWrapper.h"
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/utils/memory/stl/AWSString.h>
#include <aws/transfer/TransferHandle.h>
#include <aws/transfer/TransferManager.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <streambuf>
#include <string>
#include <unordered_map>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief   A wrapper module for the Aws S3 Client APIs. Aws::InitAPI must be called before using this class.
 **/
class S3Sender
{
public:
    /**
     * @param payloadManager the payload manager to be used when data needs to be persisted. nullptr
     *                       can be passed. In such case, data won't be persisted after a failure.
     * @param createTransferManagerWrapper a factory function that creates a new Transfer Manager instance
     * @param multipartSize the size that will be used to decide whether to try a multipart upload
     */
    S3Sender(
        std::shared_ptr<PayloadManager> payloadManager,
        std::function<std::shared_ptr<TransferManagerWrapper>(
            Aws::Client::ClientConfiguration &clientConfiguration,
            Aws::Transfer::TransferManagerConfiguration &transferManagerConfiguration )> createTransferManagerWrapper,
        size_t multipartSize );
    virtual ~S3Sender() = default;

    S3Sender() = delete;
    S3Sender( const S3Sender & ) = delete;
    S3Sender &operator=( const S3Sender & ) = delete;
    S3Sender( S3Sender && ) = delete;
    S3Sender &operator=( S3Sender && ) = delete;

    virtual bool disconnect();

    virtual ConnectivityError sendStream( std::unique_ptr<StreambufBuilder> streambufBuilder,
                                          const S3UploadMetadata &uploadMetadata,
                                          const std::string &objectKey,
                                          std::function<void( bool success )> resultCallback );

private:
    size_t mMultipartSize;
    std::unordered_map<std::string, std::shared_ptr<TransferManagerWrapper>> mRegionToTransferManagerWrapper;
    std::shared_ptr<PayloadManager> mPayloadManager;
    std::function<std::shared_ptr<TransferManagerWrapper>(
        Aws::Client::ClientConfiguration &clientConfiguration,
        Aws::Transfer::TransferManagerConfiguration &transferManagerConfiguration )>
        mCreateTransferManagerWrapper;

    struct OngoingUploadMetadata
    {
        std::shared_ptr<std::streambuf> streambuf;
        std::function<void( bool success )> resultCallback;
        std::shared_ptr<TransferManagerWrapper> transferManagerWrapper;
        std::shared_ptr<Aws::Transfer::TransferHandle> transferHandle;
        uint8_t attempts;
    };
    std::mutex mQueuedAndOngoingUploadsLookupMutex;
    std::unordered_map<Aws::String, OngoingUploadMetadata> mOngoingUploads;
    struct QueuedUploadMetadata
    {
        std::unique_ptr<StreambufBuilder> streambufBuilder;
        S3UploadMetadata uploadMetadata;
        std::string objectKey;
        std::function<void( bool success )> resultCallback;
    };
    std::queue<QueuedUploadMetadata> mQueuedUploads;

    /**
     * @brief TODO
     */
    void submitQueuedUploads();

    /**
     * @brief Get the TransferManagerWrapper instance based on the upload metadata.
     * @param uploadMetadata the metadata for the new transfer being initiated.
     * @return the TransferManagerWrapper instance that satisfies the metadata. Depending on the metadata,
     * a new instance might be created (e.g. if uploading to multiple regions, each region requires a
     * separate instance).
     */
    std::shared_ptr<TransferManagerWrapper> getTransferManagerWrapper( const S3UploadMetadata &uploadMetadata );

    /**
     * @brief Callback to be used to check the results of a transfer.
     *
     * This implements the callback interface defined by TransferManager and it is called whenever
     * the status of a transfer changes.
     */
    void transferStatusUpdatedCallback( const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle );

    /**
     * @brief Pass data stream to the payload manager to persist
     *
     * @param streambufBuilder object that can create the stream with data to persist
     * @param objectKey S3 object key
     */
    void persistS3Request( std::unique_ptr<StreambufBuilder> streambufBuilder, std::string objectKey );
};

} // namespace IoTFleetWise
} // namespace Aws
