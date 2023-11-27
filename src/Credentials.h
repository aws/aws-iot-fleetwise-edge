// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/io/Bootstrap.h>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

class CrtCredentialsProviderAdapter : public Aws::Auth::AWSCredentialsProvider
{
public:
    /**
     * @param crtCredentialsProvider the CRT credentials provider that should be wrapped
     */
    CrtCredentialsProviderAdapter( std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> crtCredentialsProvider )
        : mCrtCredentialsProvider( std::move( crtCredentialsProvider ) )
    {
    }

    Aws::Auth::AWSCredentials GetAWSCredentials() override;

private:
    std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> mCrtCredentialsProvider;
    Aws::Auth::AWSCredentials mAwsCredentials;
    std::mutex mCredentialsMutex;
};

/**
 * @brief Helper function to wire up dependencies and create a X509 credentials provider
 * @param clientBootstrap pointer to AWS client bootstrap. Note AwsBootstrap is responsible for the client
 *        bootstrap lifecycle
 * @param clientId the id that is used to identify this connection instance
 * @param privateKey Contents of the private key .pem file provided during setup of the AWS
 *                   Iot Thing.
 * @param certificate Contents of the certificate .crt.txt file provided during setup
 *                    of the AWS Iot Thing.
 * @param credentialProviderEndpointUrl AWS IoT Core Credentials Provider endpoint
 * @param roleAlias IoT Role Alias for IAM policy
 * @return the created credentials provider (with the CRT interface, not AWS SDK Credentials Provider).
 */
std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> createX509CredentialsProvider(
    Aws::Crt::Io::ClientBootstrap *clientBootstrap,
    const std::string &clientId,
    const std::string &privateKey,
    const std::string &certificate,
    const std::string &credentialProviderEndpointUrl,
    const std::string &roleAlias );

} // namespace IoTFleetWise
} // namespace Aws
