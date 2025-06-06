// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/TransferManagerWrapper.h"
#include <gmock/gmock.h>

namespace Aws
{
namespace IoTFleetWise
{

class TransferManagerWrapperMock : public TransferManagerWrapper
{
public:
    TransferManagerWrapperMock()
        : TransferManagerWrapper( nullptr ){};

    MOCK_METHOD( Aws::Transfer::TransferStatus, MockedWaitUntilAllFinished, ( int64_t timeoutMs ) );

    Aws::Transfer::TransferStatus
    WaitUntilAllFinished( int64_t timeoutMs = std::numeric_limits<int64_t>::max() ) override
    {
        return MockedWaitUntilAllFinished( timeoutMs );
    }

    MOCK_METHOD( void, CancelAll, (), ( override ) );

    MOCK_METHOD( std::shared_ptr<Aws::Transfer::TransferHandle>,
                 MockedUploadFile,
                 (const Aws::String &,
                  const Aws::String &,
                  const Aws::String &,
                  const Aws::String &,
                  (const Aws::Map<Aws::String, Aws::String> &),
                  const std::shared_ptr<const Aws::Client::AsyncCallerContext> &));

    std::shared_ptr<Aws::Transfer::TransferHandle>
    UploadFile( const Aws::String &fileName,
                const Aws::String &bucketName,
                const Aws::String &keyName,
                const Aws::String &contentType,
                const Aws::Map<Aws::String, Aws::String> &metadata,
                const std::shared_ptr<const Aws::Client::AsyncCallerContext> &context = nullptr ) override
    {
        return MockedUploadFile( fileName, bucketName, keyName, contentType, metadata, context );
    }

    MOCK_METHOD( std::shared_ptr<Aws::Transfer::TransferHandle>,
                 MockedUploadFile,
                 (const std::shared_ptr<Aws::IOStream> &stream,
                  const Aws::String &,
                  const Aws::String &,
                  const Aws::String &,
                  (const Aws::Map<Aws::String, Aws::String> &),
                  const std::shared_ptr<const Aws::Client::AsyncCallerContext> &));

    std::shared_ptr<Aws::Transfer::TransferHandle>
    UploadFile( const std::shared_ptr<Aws::IOStream> &stream,
                const Aws::String &bucketName,
                const Aws::String &keyName,
                const Aws::String &contentType,
                const Aws::Map<Aws::String, Aws::String> &metadata,
                const std::shared_ptr<const Aws::Client::AsyncCallerContext> &context = nullptr ) override
    {
        return MockedUploadFile( stream, bucketName, keyName, contentType, metadata, context );
    }

    MOCK_METHOD( std::shared_ptr<Aws::Transfer::TransferHandle>,
                 RetryUpload,
                 (const Aws::String &, const std::shared_ptr<Aws::Transfer::TransferHandle> &),
                 ( override ) );

    MOCK_METHOD( std::shared_ptr<Aws::Transfer::TransferHandle>,
                 RetryUpload,
                 ( const std::shared_ptr<Aws::IOStream> &stream,
                   const std::shared_ptr<Aws::Transfer::TransferHandle> &retryHandle ),
                 ( override ) );

    MOCK_METHOD( void, MockedDownloadToDirectory, (const Aws::String &, const Aws::String &, const Aws::String &));

    void
    DownloadToDirectory( const Aws::String &directory,
                         const Aws::String &bucketName,
                         const Aws::String &prefix = Aws::String() ) override
    {
        MockedDownloadToDirectory( directory, bucketName, prefix );
    }

    MOCK_METHOD( std::shared_ptr<Aws::Transfer::TransferHandle>,
                 MockedDownloadFile,
                 (const Aws::String &,
                  const Aws::String &,
                  const Aws::String &,
                  const Aws::Transfer::DownloadConfiguration &,
                  const std::shared_ptr<const Aws::Client::AsyncCallerContext> &));

    std::shared_ptr<Aws::Transfer::TransferHandle>
    DownloadFile( const Aws::String &bucketName,
                  const Aws::String &keyName,
                  const Aws::String &writeToFile,
                  const Aws::Transfer::DownloadConfiguration &downloadConfig = Aws::Transfer::DownloadConfiguration(),
                  const std::shared_ptr<const Aws::Client::AsyncCallerContext> &context = nullptr ) override
    {
        return MockedDownloadFile( bucketName, keyName, writeToFile, downloadConfig, context );
    }
};

} // namespace IoTFleetWise
} // namespace Aws
