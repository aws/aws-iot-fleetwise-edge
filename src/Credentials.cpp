// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/Credentials.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <aws/core/utils/StringUtils.h>
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/io/TlsOptions.h>
#include <chrono>
#include <future>

namespace Aws
{
namespace IoTFleetWise
{

Aws::Auth::AWSCredentials
CrtCredentialsProviderAdapter::GetAWSCredentials()
{
    std::lock_guard<std::mutex> lock( mCredentialsMutex );

    if ( !mAwsCredentials.IsExpiredOrEmpty() )
    {
        FWE_LOG_TRACE( "Skip refreshing credentials as the current credentials have not expired" );
        return mAwsCredentials;
    }

    if ( mCrtCredentialsProvider == nullptr )
    {
        FWE_LOG_ERROR( "No CRT credentials provider" );
        mAwsCredentials = Aws::Auth::AWSCredentials();
        return mAwsCredentials;
    }

    std::promise<std::pair<std::shared_ptr<Crt::Auth::Credentials>, int>> credentialsPromise;
    auto onCredentialsFetched = [&credentialsPromise]( std::shared_ptr<Crt::Auth::Credentials> creds, int errorCode ) {
        credentialsPromise.set_value( std::make_pair( creds, errorCode ) );
    };
    mCrtCredentialsProvider->GetCredentials( onCredentialsFetched );
    auto resp = credentialsPromise.get_future().get();
    auto creds = resp.first;
    int errorCode = resp.second;

    if ( errorCode != 0 )
    {
        auto errStr = Crt::ErrorDebugString( errorCode );
        errStr = errStr != nullptr ? errStr : "Unknown error";
        FWE_LOG_ERROR( "Unable to get the credentials, error: " + std::string( errStr ) );
        mAwsCredentials = Aws::Auth::AWSCredentials();
        return mAwsCredentials;
    }

    Aws::Utils::DateTime expiryTime(
        std::chrono::system_clock::time_point{ std::chrono::seconds{ creds->GetExpirationTimepointInSeconds() } } );
    mAwsCredentials = Aws::Auth::AWSCredentials( Aws::Utils::StringUtils::FromByteCursor( creds->GetAccessKeyId() ),
                                                 Aws::Utils::StringUtils::FromByteCursor( creds->GetSecretAccessKey() ),
                                                 Aws::Utils::StringUtils::FromByteCursor( creds->GetSessionToken() ),
                                                 expiryTime );
    FWE_LOG_INFO( "Successfully refreshed the Device Credentials. Expiration: " +
                  expiryTime.ToGmtString( Aws::Utils::DateFormat::ISO_8601 ) );
    return mAwsCredentials;
}

std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider>
createX509CredentialsProvider( Aws::Crt::Io::ClientBootstrap *clientBootstrap,
                               const std::string &clientId,
                               const std::string &privateKey,
                               const std::string &certificate,
                               const std::string &credentialProviderEndpointUrl,
                               const std::string &roleAlias )
{
    const auto certificateBytes = Aws::Crt::ByteCursorFromCString( certificate.c_str() );
    const auto privateKeyBytes = Aws::Crt::ByteCursorFromCString( privateKey.c_str() );
    Aws::Crt::Io::TlsContextOptions tlsCtxOptions =
        Aws::Crt::Io::TlsContextOptions::InitClientWithMtls( certificateBytes, privateKeyBytes );

    if ( !tlsCtxOptions )
    {
        auto errStr = Aws::Crt::ErrorDebugString( tlsCtxOptions.LastError() );
        errStr = errStr != nullptr ? errStr : "Unknown error";
        FWE_LOG_ERROR( "Unable to initialize tls context options, error: " + std::string( errStr ) );
        return nullptr;
    }
    auto x509TlsCtx = Aws::Crt::Io::TlsContext( tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT );
    if ( !x509TlsCtx )
    {
        auto errStr = Aws::Crt::ErrorDebugString( x509TlsCtx.GetInitializationError() );
        errStr = errStr != nullptr ? errStr : "Unknown error";
        FWE_LOG_ERROR( "Unable to create tls context, error: " + std::string( errStr ) );
        return nullptr;
    }

    Aws::Crt::Auth::CredentialsProviderX509Config x509Config;
    x509Config.TlsOptions = x509TlsCtx.NewConnectionOptions();

    if ( !x509Config.TlsOptions )
    {
        auto errStr = Aws::Crt::ErrorDebugString( x509Config.TlsOptions.LastError() );
        errStr = errStr != nullptr ? errStr : "Unknown error";
        FWE_LOG_ERROR( "Create tls options from tls context, error: " + std::string( errStr ) );
        return nullptr;
    }

    if ( clientBootstrap == nullptr )
    {
        FWE_LOG_ERROR( "ClientBootstrap is null" );
        return nullptr;
    }
    if ( !( *clientBootstrap ) )
    {
        auto errStr = Aws::Crt::ErrorDebugString( clientBootstrap->LastError() );
        errStr = errStr != nullptr ? errStr : "Unknown error";
        FWE_LOG_ERROR( "ClientBootstrap failed with error " + std::string( errStr ) );
        return nullptr;
    }
    x509Config.Bootstrap = clientBootstrap;
    auto endpointUrlStr = credentialProviderEndpointUrl.c_str();
    x509Config.Endpoint = endpointUrlStr != nullptr ? endpointUrlStr : "";
    auto roleAliasStr = roleAlias.c_str();
    x509Config.RoleAlias = roleAliasStr != nullptr ? roleAliasStr : "";
    auto thingNameStr = clientId.c_str();
    x509Config.ThingName = thingNameStr != nullptr ? thingNameStr : "";

    auto provider = Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderX509( x509Config );

    if ( !provider )
    {
        auto msg =
            std::string( "Could not create credentials provider with endpoint: '" + credentialProviderEndpointUrl +
                         "' role: '" + roleAlias + "' clientId: '" + clientId + "'" );
        FWE_LOG_ERROR( msg );
        return nullptr;
    }

    return provider;
}

} // namespace IoTFleetWise
} // namespace Aws
