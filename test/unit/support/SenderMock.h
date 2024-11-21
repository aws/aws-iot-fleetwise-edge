// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "ISender.h"
#include <cstddef>
#include <cstdint>
#include <gmock/gmock.h>
#include <memory>
#include <string>
#include <unordered_map>
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
    SenderMock()
    {
        TopicConfigArgs topicConfigArgs;
        mTopicConfig = std::make_unique<TopicConfig>( "thing-name", topicConfigArgs );
    }

    SenderMock( const TopicConfig &topicConfig )
        : mTopicConfig( std::make_unique<TopicConfig>( topicConfig ) )
    {
    }

    struct SentBufferData
    {
        std::string data;
        OnDataSentCallback callback;
    };

    MOCK_METHOD( bool, isAlive, (), ( override ) );

    MOCK_METHOD( size_t, getMaxSendSize, (), ( const, override ) );

    void
    sendBuffer( const std::string &topic,
                const std::uint8_t *buf,
                size_t size,
                OnDataSentCallback callback,
                QoS qos = QoS::AT_LEAST_ONCE ) override
    {
        static_cast<void>( qos );
        std::lock_guard<std::mutex> lock( mSentBufferDataMutex );
        mSentBufferData[topic].push_back( SentBufferData{ std::string( buf, buf + size ), callback } );
        mockedSendBuffer( topic, buf, size, callback );
    }

    std::unordered_map<std::string, std::vector<SentBufferData>>
    getSentBufferData()
    {
        std::lock_guard<std::mutex> lock( mSentBufferDataMutex );
        return mSentBufferData;
    }

    std::vector<SentBufferData>
    getSentBufferDataByTopic( const std::string &topic )
    {
        std::lock_guard<std::mutex> lock( mSentBufferDataMutex );
        if ( mSentBufferData.find( topic ) == mSentBufferData.end() )
        {
            return {};
        }
        return mSentBufferData[topic];
    }

    void
    clearSentBufferData()
    {
        std::lock_guard<std::mutex> lock( mSentBufferDataMutex );
        mSentBufferData.clear();
    }

    MOCK_METHOD( void,
                 mockedSendBuffer,
                 ( const std::string &topic, const std::uint8_t *buf, size_t size, OnDataSentCallback callback ) );

    MOCK_METHOD( unsigned, getPayloadCountSent, (), ( const, override ) );

    const TopicConfig &
    getTopicConfig() const
    {
        return *mTopicConfig;
    }

private:
    // Record the calls so that we can wait for asynchronous calls to happen.
    std::unordered_map<std::string, std::vector<SentBufferData>> mSentBufferData;
    std::mutex mSentBufferDataMutex;
    std::unique_ptr<TopicConfig> mTopicConfig;
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
