// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Credentials.h"
#include <aws/common/error.h>
#include <aws/core/auth/AWSCredentials.h>
#include <aws/crt/Types.h>
#include <aws/crt/auth/Credentials.h>
#include <cstdint>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>

using ::testing::_;
using ::testing::DoAll;
using ::testing::Exactly;
using ::testing::InvokeArgument;
using ::testing::Return;
using ::testing::StrictMock;

namespace
{
constexpr char DUMMY_CLIENT_ID[] = "clientId";
constexpr char DUMMY_PRIVATE_KEY[] = "privateKey";
constexpr char DUMMY_CERT[] = "certificate";
constexpr char DUMMY_ENDPOINT[] = "endpointUrl";
constexpr char DUMMY_ROLE_ALIAS[] = "roleAlias";

constexpr char TEST_ACCESS_KEY_ID[] = "ACCESS_KEY_ID";
constexpr char TEST_SECRET_ACCESS_KEY[] = "SECRET_ACCESS_KEY";
constexpr char TEST_SESSION_TOKEN[] = "SESSION_TOKEN";
} // namespace

namespace Aws
{
namespace IoTFleetWise
{

class CrtCredentialsProviderMock : public Aws::Crt::Auth::ICredentialsProvider
{
public:
    MOCK_METHOD( bool,
                 GetCredentials,
                 ( const Aws::Crt::Auth::OnCredentialsResolved &onCredentialsResolved ),
                 ( const ) );
    MOCK_METHOD( aws_credentials_provider *, GetUnderlyingHandle, (), ( const, noexcept ) );
    MOCK_METHOD( bool, IsValid, (), ( const, noexcept ) );
};

class CredentialsTest : public ::testing::Test
{
protected:
    CredentialsTest()
        : crtCredentialsProviderMock( std::make_shared<StrictMock<CrtCredentialsProviderMock>>() )
        , awsCredentialsProviderAdapter( crtCredentialsProviderMock )
        , validCrtCredentials(
              std::make_shared<Aws::Crt::Auth::Credentials>( Aws::Crt::ByteCursorFromCString( accessKeyId.c_str() ),
                                                             Aws::Crt::ByteCursorFromCString( secretAccessKey.c_str() ),
                                                             Aws::Crt::ByteCursorFromCString( sessionToken.c_str() ),
                                                             expirationTimepointInSeconds ) )
    {
    }

    void
    SetUp() override
    {
        validCrtCredentials =
            std::make_shared<Aws::Crt::Auth::Credentials>( Aws::Crt::ByteCursorFromCString( accessKeyId.c_str() ),
                                                           Aws::Crt::ByteCursorFromCString( secretAccessKey.c_str() ),
                                                           Aws::Crt::ByteCursorFromCString( sessionToken.c_str() ),
                                                           expirationTimepointInSeconds );
        ON_CALL( *crtCredentialsProviderMock, GetCredentials( _ ) )
            .WillByDefault( DoAll( InvokeArgument<0>( nullptr, AWS_ERROR_INVALID_ARGUMENT ), Return( false ) ) );
    }

    const std::string accessKeyId = "ACCESS_KEY";
    const std::string secretAccessKey = "SECRET_ACCESS_KEY";
    const std::string sessionToken = "SESSION_TOKEN";
    uint64_t expirationTimepointInSeconds = 4102484400; // 2100-01-01
    std::shared_ptr<StrictMock<CrtCredentialsProviderMock>> crtCredentialsProviderMock;
    CrtCredentialsProviderAdapter awsCredentialsProviderAdapter;
    std::shared_ptr<Aws::Crt::Auth::Credentials> validCrtCredentials;
};

TEST_F( CredentialsTest, skipRefreshCredentialsIfValid )
{
    EXPECT_CALL( *crtCredentialsProviderMock, GetCredentials( _ ) )
        .Times( Exactly( 1 ) )
        .WillRepeatedly( DoAll( InvokeArgument<0>( validCrtCredentials, AWS_ERROR_SUCCESS ), Return( true ) ) );

    auto credentials = awsCredentialsProviderAdapter.GetAWSCredentials();
    ASSERT_EQ( credentials.GetAWSAccessKeyId(), accessKeyId );
    ASSERT_EQ( credentials.GetAWSSecretKey(), secretAccessKey );
    ASSERT_EQ( credentials.GetSessionToken(), sessionToken );
    ASSERT_EQ( credentials.GetExpiration().Seconds(), expirationTimepointInSeconds );
    ASSERT_FALSE( credentials.IsExpiredOrEmpty() );

    auto secondCredentials = awsCredentialsProviderAdapter.GetAWSCredentials();
    ASSERT_EQ( credentials, secondCredentials );
}

TEST_F( CredentialsTest, wrappedCrtCredentialsProviderFails )
{
    EXPECT_CALL( *crtCredentialsProviderMock, GetCredentials( _ ) )
        .Times( Exactly( 1 ) )
        .WillRepeatedly( DoAll( InvokeArgument<0>( nullptr, AWS_ERROR_INVALID_ARGUMENT ), Return( true ) ) );

    auto credentials = awsCredentialsProviderAdapter.GetAWSCredentials();
    ASSERT_TRUE( credentials.IsExpiredOrEmpty() );
}

TEST_F( CredentialsTest, noCrtCredentialsProvider )
{
    CrtCredentialsProviderAdapter awsCredentialsProviderAdapter( nullptr );

    EXPECT_CALL( *crtCredentialsProviderMock, GetCredentials( _ ) ).Times( Exactly( 0 ) );

    auto credentials = awsCredentialsProviderAdapter.GetAWSCredentials();
    ASSERT_TRUE( credentials.IsExpiredOrEmpty() );
}

} // namespace IoTFleetWise
} // namespace Aws
