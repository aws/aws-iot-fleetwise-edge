// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DeviceShadowOverSomeip.h"
#include "IConnectionTypes.h"
#include "LoggingModule.h"
#include "TopicConfig.h"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cstddef>
#include <cstdint>
#include <json/json.h>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

static inline v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode
connectivityToDeviceShadowError( ConnectivityError error )
{
    switch ( error )
    {
    case ConnectivityError::Success:
        return v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::NO_ERROR;
    case ConnectivityError::NotConfigured:
    case ConnectivityError::WrongInputData:
    case ConnectivityError::TypeNotSupported:
        return v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::INVALID_REQUEST;
    case ConnectivityError::NoConnection:
    case ConnectivityError::QuotaReached:
    case ConnectivityError::TransmissionError:
        return v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::SHADOW_SERVICE_UNREACHABLE;
    }
    return v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::UNKNOWN;
}

DeviceShadowOverSomeip::DeviceShadowOverSomeip( std::shared_ptr<ISender> sender )
    : mMqttSender( std::move( sender ) )
    , mClientTokenRandomPrefix( boost::uuids::to_string( boost::uuids::random_generator()() ) + "-" )
{
}

void
DeviceShadowOverSomeip::onDataReceived( const ReceivedConnectivityMessage &receivedMessage )
{
    std::string responseDocument( receivedMessage.buf, receivedMessage.buf + receivedMessage.size );

    // Ignore requests and delta updates:
    if ( boost::ends_with( receivedMessage.mqttTopic, "/get" ) ||
         boost::ends_with( receivedMessage.mqttTopic, "/update" ) ||
         boost::ends_with( receivedMessage.mqttTopic, "/delete" ) ||
         boost::ends_with( receivedMessage.mqttTopic, "/update/delta" ) )
    {
        return;
    }

    // Documents update:
    const std::string updateDocumentsSuffix = "/update/documents";
    if ( boost::ends_with( receivedMessage.mqttTopic, updateDocumentsSuffix ) )
    {
        if ( !boost::starts_with( receivedMessage.mqttTopic, mMqttSender->getTopicConfig().deviceShadowPrefix ) )
        {
            FWE_LOG_ERROR( "Received documents update for incorrect thing" );
            return;
        }
        std::string shadowName;
        auto namedPrefix = mMqttSender->getTopicConfig().namedDeviceShadowPrefix;
        if ( boost::starts_with( receivedMessage.mqttTopic, namedPrefix ) )
        {
            shadowName = receivedMessage.mqttTopic.substr( namedPrefix.size(),
                                                           receivedMessage.mqttTopic.size() - namedPrefix.size() -
                                                               updateDocumentsSuffix.size() );
        }
        FWE_LOG_INFO( "Received documents update" + ( shadowName.empty() ? "" : " for shadow " + shadowName ) );

        fireShadowChangedEvent( shadowName, responseDocument );
        return;
    }

    // Otherwise it's a response to a request, get the callback via the clientToken:
    Json::Reader jsonReader;
    Json::Value responseJson;
    if ( !jsonReader.parse( responseDocument, responseJson ) )
    {
        FWE_LOG_ERROR( "JSON parse error" );
        return;
    }
    const auto &clientToken = responseJson["clientToken"].asString();
    ResponseCallback callback;
    {
        std::lock_guard<std::mutex> lock( mRequestTableMutex );
        auto requestIt = mRequestTable.find( clientToken );
        if ( requestIt == mRequestTable.end() )
        {
            // Ignore responses to requests from other clients
            return;
        }
        callback = std::move( requestIt->second );
        mRequestTable.erase( requestIt );
    }

    v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode errorCode;
    std::string errorMessage;
    if ( boost::ends_with( receivedMessage.mqttTopic, "/accepted" ) )
    {
        errorCode = v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::NO_ERROR;
        FWE_LOG_INFO( "Received accepted response for clientToken " + clientToken );
    }
    else if ( boost::ends_with( receivedMessage.mqttTopic, "/rejected" ) )
    {
        errorCode = v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::REJECTED;
        errorMessage = responseJson["message"].asString();
        FWE_LOG_ERROR( "Received rejected response for clientToken " + clientToken + " with message " + errorMessage );
    }
    else
    {
        errorCode = v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::UNKNOWN;
        FWE_LOG_ERROR( "Received unknown response for clientToken " + clientToken );
    }

    callback( errorCode, errorMessage, responseDocument );
}

void
DeviceShadowOverSomeip::sendRequest( const std::string &topic,
                                     const std::string &requestDocument,
                                     ResponseCallback callback )
{
    // Add the client token to the request document:
    Json::Reader jsonReader;
    Json::Value requestJson;
    if ( !jsonReader.parse( requestDocument, requestJson ) )
    {
        FWE_LOG_ERROR( "JSON parse error" );
        callback( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::INVALID_REQUEST, "JSON parse error", "" );
        return;
    }
    // coverity[misra_cpp_2008_rule_5_2_10_violation] For std::atomic this must be performed in a single statement
    // coverity[autosar_cpp14_m5_2_10_violation] For std::atomic this must be performed in a single statement
    auto clientToken = mClientTokenRandomPrefix + std::to_string( mClientTokenCounter++ );
    requestJson["clientToken"] = clientToken;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    auto requestDocumentWithClientToken = Json::writeString( builder, requestJson );

    FWE_LOG_INFO( "Sending  request to topic " + topic + " with clientToken " + clientToken );
    // It can happen that the response is received via onDataReceived before the transmit callback
    // below is called, so add the request to the request table now, and erase it again in the case
    // of connectivity error.
    {
        std::lock_guard<std::mutex> lock( mRequestTableMutex );
        mRequestTable.emplace( clientToken, callback );
    }
    mMqttSender->sendBuffer(
        topic,
        reinterpret_cast<const uint8_t *>( requestDocumentWithClientToken.data() ),
        requestDocumentWithClientToken.size(),
        [this, clientToken, callback]( ConnectivityError result ) {
            if ( result != ConnectivityError::Success )
            {
                FWE_LOG_ERROR( "Connectivity error: " + connectivityErrorToString( result ) );
                size_t requestsErased{};
                {
                    std::lock_guard<std::mutex> lock( mRequestTableMutex );
                    requestsErased = mRequestTable.erase( clientToken );
                }
                if ( requestsErased == 1 )
                {
                    callback( connectivityToDeviceShadowError( result ), connectivityErrorToString( result ), "" );
                }
                return;
            }
        } );
}

void
DeviceShadowOverSomeip::getShadow( const std::shared_ptr<CommonAPI::ClientId> _client,
                                   std::string _shadowName,
                                   getShadowReply_t _reply )
{
    static_cast<void>( _client );
    sendRequest( mMqttSender->getTopicConfig().getDeviceShadowTopic( _shadowName ), "{}", _reply );
}

void
DeviceShadowOverSomeip::updateShadow( const std::shared_ptr<CommonAPI::ClientId> _client,
                                      std::string _shadowName,
                                      std::string _updateDocument,
                                      updateShadowReply_t _reply )
{
    static_cast<void>( _client );
    sendRequest( mMqttSender->getTopicConfig().updateDeviceShadowTopic( _shadowName ), _updateDocument, _reply );
}

void
DeviceShadowOverSomeip::deleteShadow( const std::shared_ptr<CommonAPI::ClientId> _client,
                                      std::string _shadowName,
                                      deleteShadowReply_t _reply )
{
    static_cast<void>( _client );
    sendRequest( mMqttSender->getTopicConfig().deleteDeviceShadowTopic( _shadowName ),
                 "{}",
                 [_reply]( auto errorCode, const auto &errorMessage, const auto &responseDocument ) {
                     static_cast<void>( responseDocument );
                     _reply( errorCode, errorMessage );
                 } );
}

} // namespace IoTFleetWise
} // namespace Aws
