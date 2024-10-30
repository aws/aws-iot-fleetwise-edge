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
        OnDataSentCallback callback;
    };

    MOCK_METHOD( bool, isAlive, (), ( override ) );

    MOCK_METHOD( size_t, getMaxSendSize, (), ( const, override ) );

    void
    sendBuffer( const std::uint8_t *buf, size_t size, OnDataSentCallback callback ) override
    {
        std::lock_guard<std::mutex> lock( mSentBufferDataMutex );
        mSentBufferData.push_back( SentBufferData{ std::string( buf, buf + size ), callback } );
        mockedSendBuffer( buf, size, callback );
    }

    void
    sendBufferToTopic( __attribute__( ( unused ) ) const std::string &topic,
                       const std::uint8_t *buf,
                       size_t size,
                       OnDataSentCallback callback ) override
    {
        // TODO: Make a map of topic to SentBufferData. For now, mark topic parameter as unused
        std::lock_guard<std::mutex> lock( mSentBufferDataMutex );
        mSentBufferData.push_back( SentBufferData{ std::string( buf, buf + size ), callback } );
        mockedSendBuffer( buf, size, callback );
    }

    std::vector<SentBufferData>
    getSentBufferData()
    {
        std::lock_guard<std::mutex> lock( mSentBufferDataMutex );
        return mSentBufferData;
    }

    void
    clearSentBufferData()
    {
        std::lock_guard<std::mutex> lock( mSentBufferDataMutex );
        mSentBufferData.clear();
    }

    MOCK_METHOD( void, mockedSendBuffer, ( const std::uint8_t *buf, size_t size, OnDataSentCallback callback ) );

    MOCK_METHOD( unsigned, getPayloadCountSent, (), ( const, override ) );

private:
    // Record the calls so that we can wait for asynchronous calls to happen.
    std::vector<SentBufferData> mSentBufferData;
    std::mutex mSentBufferDataMutex;
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
