// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ISender.h"
#include <cstddef>
#include <cstdint>
#include <gmock/gmock.h>
#include <memory>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace Testing
{

class SenderMock : public ISender
{
public:
    struct SentBufferData
    {
        std::string data;
        CollectionSchemeParams collectionSchemeParams;
    };

    MOCK_METHOD( bool, isAlive, (), ( override ) );

    MOCK_METHOD( size_t, getMaxSendSize, (), ( const, override ) );

    ConnectivityError
    sendBuffer( const std::uint8_t *buf, size_t size, CollectionSchemeParams collectionSchemeParams ) override
    {
        std::lock_guard<std::mutex> lock( mSentBufferDataMutex );
        mSentBufferData.push_back( SentBufferData{ std::string( buf, buf + size ), collectionSchemeParams } );
        return mockedSendBuffer( buf, size, collectionSchemeParams );
    }

    std::vector<SentBufferData>
    getSentBufferData()
    {
        std::lock_guard<std::mutex> lock( mSentBufferDataMutex );
        return mSentBufferData;
    }

    MOCK_METHOD( ConnectivityError,
                 mockedSendBuffer,
                 ( const std::uint8_t *buf, size_t size, CollectionSchemeParams collectionSchemeParams ) );

    MOCK_METHOD( ConnectivityError,
                 sendFile,
                 ( const std::string &filePath, size_t size, CollectionSchemeParams collectionSchemeParams ),
                 ( override ) );

private:
    // Record the calls so that we can wait for asynchronous calls to happen.
    std::vector<SentBufferData> mSentBufferData;
    std::mutex mSentBufferDataMutex;
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
