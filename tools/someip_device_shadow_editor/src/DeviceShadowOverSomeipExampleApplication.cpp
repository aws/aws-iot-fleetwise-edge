// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DeviceShadowOverSomeipExampleApplication.hpp"
#include "v1/commonapi/DeviceShadowOverSomeipInterface.hpp"
#include <CommonAPI/CommonAPI.hpp>
#include <iostream>
#include <stdexcept>

static std::string
commonapiCallStatusToString( const CommonAPI::CallStatus callStatus )
{
    std::string status{};

    switch ( callStatus )
    {
    case CommonAPI::CallStatus::SUCCESS:
        status = "SUCCESS";
        break;
    case CommonAPI::CallStatus::OUT_OF_MEMORY:
        status = "OUT_OF_MEMORY";
        break;
    case CommonAPI::CallStatus::NOT_AVAILABLE:
        status = "NOT_AVAILABLE";
        break;
    case CommonAPI::CallStatus::CONNECTION_FAILED:
        status = "CONNECTION_FAILED";
        break;
    case CommonAPI::CallStatus::REMOTE_ERROR:
        status = "REMOTE_ERROR";
        break;
    case CommonAPI::CallStatus::UNKNOWN:
        status = "UNKNOWN";
        break;
    case CommonAPI::CallStatus::INVALID_VALUE:
        status = "INVALID_VALUE";
        break;
    case CommonAPI::CallStatus::SUBSCRIPTION_REFUSED:
        status = "SUBSCRIPTION_REFUSED";
        break;
    case CommonAPI::CallStatus::SERIALIZATION_ERROR:
        status = "SERIALIZATION_ERROR";
        break;
    default:
        status = "Unknown error " + std::to_string( static_cast<int>( callStatus ) );
        break;
    }

    return status;
}

static std::string
someipErrorCodeToString( v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode error )
{
    switch ( error )
    {
    case v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::NO_ERROR:
        return "NO_ERROR";
    case v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::INVALID_REQUEST:
        return "INVALID_REQUEST";
    case v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::SHADOW_SERVICE_UNREACHABLE:
        return "SHADOW_SERVICE_UNREACHABLE";
    case v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::UNKNOWN:
        return "UNKNOWN";
    }

    return "UNDEFINED";
}

std::string
DeviceShadowOverSomeipExampleApplication::getInstance() const
{
    return mInstance;
}

void
DeviceShadowOverSomeipExampleApplication::init( std::string domain, std::string instance, std::string connection )
{
    mInstance = instance;
    mProxy = CommonAPI::Runtime::get()->buildProxy<v1::commonapi::DeviceShadowOverSomeipInterfaceProxy>(
        domain, instance, connection );

    if ( mProxy == nullptr )
    {
        throw std::runtime_error( "Failed to build DeviceShadowOverSomeipInterfaceProxy" );
    }

    mShadowChangedSubscription = mProxy->getShadowChangedEvent().subscribe(
        []( const std::string &shadowName, const std::string &shadowDocument ) {
            std::cout << "Shadow changed: shadowName = " << shadowName << "; shadowDocument = " << shadowDocument
                      << std::endl;
        } );

    mInitialized = true;
}

void
DeviceShadowOverSomeipExampleApplication::deinit()
{
    if ( mInitialized )
    {
        if ( mShadowChangedSubscription != 0 )
        {
            mProxy->getShadowChangedEvent().unsubscribe( mShadowChangedSubscription );
        }

        mInitialized = false;
    }
}

std::string
DeviceShadowOverSomeipExampleApplication::getShadow( const std::string &shadowName )
{
    if ( !mInitialized )
    {
        throw std::runtime_error( "Not initialized yet" );
    }

    CommonAPI::CallStatus callStatus{};
    CommonAPI::CallInfo callInfo( DEFAULT_METHOD_TIMEOUT_MS );

    v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode error;
    std::string errorMessage;
    std::string shadowDocument;

    mProxy->getShadow( shadowName, callStatus, error, errorMessage, shadowDocument, &callInfo );

    if ( callStatus != CommonAPI::CallStatus::SUCCESS )
    {
        throw std::runtime_error( "Error calling getShadow: " + commonapiCallStatusToString( callStatus ) );
    }

    if ( error != v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::NO_ERROR )
    {
        throw std::runtime_error( "getShadow error: " + someipErrorCodeToString( error ) +
                                  " with message: " + errorMessage );
    }

    return shadowDocument;
}

std::string
DeviceShadowOverSomeipExampleApplication::updateShadow( const std::string &shadowName,
                                                        const std::string &updateDocument )
{
    if ( !mInitialized )
    {
        throw std::runtime_error( "Not initialized yet" );
    }

    CommonAPI::CallStatus callStatus{};
    CommonAPI::CallInfo callInfo( DEFAULT_METHOD_TIMEOUT_MS );

    v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode error;
    std::string errorMessage;
    std::string shadowDocument;

    mProxy->updateShadow( shadowName, updateDocument, callStatus, error, errorMessage, shadowDocument, &callInfo );

    if ( callStatus != CommonAPI::CallStatus::SUCCESS )
    {
        throw std::runtime_error( "Error calling updateShadow: " + commonapiCallStatusToString( callStatus ) );
    }

    if ( error != v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::NO_ERROR )
    {
        throw std::runtime_error( "updateShadow error: " + someipErrorCodeToString( error ) +
                                  " with message: " + errorMessage );
    }

    return shadowDocument;
}

void
DeviceShadowOverSomeipExampleApplication::deleteShadow( const std::string &shadowName )
{
    if ( !mInitialized )
    {
        throw std::runtime_error( "Not initialized yet" );
    }

    CommonAPI::CallStatus callStatus{};
    CommonAPI::CallInfo callInfo( DEFAULT_METHOD_TIMEOUT_MS );

    v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode error;
    std::string errorMessage;

    mProxy->deleteShadow( shadowName, callStatus, error, errorMessage, &callInfo );

    if ( callStatus != CommonAPI::CallStatus::SUCCESS )
    {
        throw std::runtime_error( "Error calling deleteShadow: " + commonapiCallStatusToString( callStatus ) );
    }

    if ( error != v1::commonapi::DeviceShadowOverSomeipInterface::ErrorCode::NO_ERROR )
    {
        throw std::runtime_error( "deleteShadow error: " + someipErrorCodeToString( error ) +
                                  " with message: " + errorMessage );
    }
}
