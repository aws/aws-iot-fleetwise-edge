// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "S3Sender.h"
#include "AwsBootstrap.h"
#include "ICollectionScheme.h"
#include "IConnectionTypes.h"
#include "StreambufBuilder.h"
#include "StringbufBuilder.h"
#include "TransferManagerWrapper.h"
#include "TransferManagerWrapperMock.h"
#include <aws/core/client/ClientConfiguration.h>
#include <aws/s3-crt/model/PutObjectRequest.h>
#include <aws/transfer/TransferManager.h>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::Exactly;
using ::testing::MockFunction;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::StrictMock;

namespace
{
constexpr char TEST_BUCKET_NAME[] = "testBucket";
constexpr char TEST_OBJECT_NAME[] = "testObject";
constexpr char TEST_PREFIX[] = "testPrefix/";
constexpr char TEST_REGION[] = "us-east-1";
constexpr char TEST_BUCKET_OWNER_ACCOUNT_ID[] = "012345678901";
constexpr char TEST_OBJECT_KEY[] = "testObject";

} // namespace
class S3SenderTest : public ::testing::Test
{
protected:
    S3SenderTest()
        : transferManagerConfiguration( nullptr )
    {
    }

    void
    SetUp() override
    {
        // Needs to initialize the SDK before creating a ClientConfiguration
        AwsBootstrap::getInstance().getClientBootStrap();
        transferManagerWrapperMock = std::make_shared<StrictMock<TransferManagerWrapperMock>>();
        createTransferManagerWrapper = [this]( const Aws::Client::ClientConfiguration &,
                                               Aws::Transfer::TransferManagerConfiguration &transferConfig )
            -> std::shared_ptr<TransferManagerWrapper> {
            transferManagerConfiguration = transferConfig;
            return transferManagerWrapperMock;
        };
    }

    std::shared_ptr<StrictMock<TransferManagerWrapperMock>> transferManagerWrapperMock;
    Aws::Transfer::TransferManagerConfiguration transferManagerConfiguration;

    CreateTransferManagerWrapper createTransferManagerWrapper;
};

class S3SenderCanceledStatusTest : public S3SenderTest,
                                   public testing::WithParamInterface<Aws::Transfer::TransferStatus>
{
};

INSTANTIATE_TEST_SUITE_P( AllCanceledStatuses,
                          S3SenderCanceledStatusTest,
                          testing::Values( Aws::Transfer::TransferStatus::CANCELED,
                                           Aws::Transfer::TransferStatus::ABORTED ) );

TEST_P( S3SenderCanceledStatusTest, AsyncStreamUploadInitiatedCallbackCanceled )
{
    S3Sender sender{ createTransferManagerWrapper, 5 * 1024 * 1024 };
    MockFunction<void( ConnectivityError, std::shared_ptr<std::streambuf> )> resultCallback;
    auto transferHandle = std::make_shared<Aws::Transfer::TransferHandle>( TEST_BUCKET_NAME, TEST_OBJECT_KEY );

    EXPECT_CALL( *transferManagerWrapperMock,
                 MockedUploadFile( ::testing::A<const std::shared_ptr<Aws::IOStream> &>(),
                                   TEST_BUCKET_NAME,
                                   TEST_OBJECT_KEY,
                                   "application/octet-stream",
                                   _,
                                   _ ) )
        .WillOnce( Return( transferHandle ) );
    EXPECT_CALL( resultCallback, Call( _, _ ) ).Times( 0 );

    sender.sendStream( std::move( std::make_unique<Testing::StringbufBuilder>( "test" ) ),
                       S3UploadMetadata{ TEST_BUCKET_NAME, TEST_PREFIX, TEST_REGION, TEST_BUCKET_OWNER_ACCOUNT_ID },
                       TEST_OBJECT_KEY,
                       resultCallback.AsStdFunction() );
    ASSERT_NE( transferManagerConfiguration.transferStatusUpdatedCallback, nullptr );

    EXPECT_CALL( resultCallback, Call( ConnectivityError::TransmissionError, _ ) ).Times( 1 );
    transferHandle->UpdateStatus( GetParam() );
    transferManagerConfiguration.transferStatusUpdatedCallback( nullptr, transferHandle );
}

TEST_F( S3SenderTest, SendEmptyStream )
{
    S3Sender sender{ nullptr, 0 };
    MockFunction<void( ConnectivityError, std::shared_ptr<std::streambuf> )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::WrongInputData, _ ) ).Times( 1 );
    sender.sendStream( nullptr,
                       S3UploadMetadata{ TEST_BUCKET_NAME, TEST_PREFIX, TEST_REGION, TEST_BUCKET_OWNER_ACCOUNT_ID },
                       TEST_OBJECT_KEY,
                       resultCallback.AsStdFunction() );
}

TEST_F( S3SenderTest, AsyncStreamUploadInitiatedCallbackFailedFirstAttempt )
{
    S3Sender sender{ createTransferManagerWrapper, 5 * 1024 * 1024 };
    MockFunction<void( ConnectivityError, std::shared_ptr<std::streambuf> )> resultCallback;
    auto transferHandle = std::make_shared<Aws::Transfer::TransferHandle>( TEST_BUCKET_NAME, TEST_OBJECT_KEY );

    EXPECT_CALL( *transferManagerWrapperMock,
                 MockedUploadFile( ::testing::A<const std::shared_ptr<Aws::IOStream> &>(),
                                   TEST_BUCKET_NAME,
                                   TEST_OBJECT_KEY,
                                   "application/octet-stream",
                                   _,
                                   _ ) )
        .WillOnce( Return( transferHandle ) );
    EXPECT_CALL( resultCallback, Call( _, _ ) ).Times( 0 );

    sender.sendStream( std::move( std::make_unique<Testing::StringbufBuilder>( "test" ) ),
                       S3UploadMetadata{ TEST_BUCKET_NAME, TEST_PREFIX, TEST_REGION, TEST_BUCKET_OWNER_ACCOUNT_ID },
                       TEST_OBJECT_KEY,
                       resultCallback.AsStdFunction() );
    ASSERT_NE( transferManagerConfiguration.transferStatusUpdatedCallback, nullptr );

    EXPECT_CALL( resultCallback, Call( _, _ ) ).Times( 0 );
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::NOT_STARTED );
    transferManagerConfiguration.transferStatusUpdatedCallback( nullptr, transferHandle );

    EXPECT_CALL( resultCallback, Call( _, _ ) ).Times( 0 );
    EXPECT_CALL( *transferManagerWrapperMock, RetryUpload( ::testing::A<const std::shared_ptr<Aws::IOStream> &>(), _ ) )
        .WillOnce( Return( transferHandle ) );
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::FAILED );
    transferManagerConfiguration.transferStatusUpdatedCallback( nullptr, transferHandle );

    EXPECT_CALL( resultCallback, Call( ConnectivityError::Success, _ ) ).Times( 1 );
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::NOT_STARTED );
    transferHandle->Restart();
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::COMPLETED );
    transferManagerConfiguration.transferStatusUpdatedCallback( nullptr, transferHandle );
}

TEST_F( S3SenderTest, AsyncStreamUploadInitiatedCallbackFailedAllAttempts )
{
    S3Sender sender{ createTransferManagerWrapper, 5 * 1024 * 1024 };
    MockFunction<void( ConnectivityError, std::shared_ptr<std::streambuf> )> resultCallback;
    auto transferHandle = std::make_shared<Aws::Transfer::TransferHandle>( TEST_BUCKET_NAME, TEST_OBJECT_KEY );

    EXPECT_CALL( *transferManagerWrapperMock,
                 MockedUploadFile( ::testing::A<const std::shared_ptr<Aws::IOStream> &>(),
                                   TEST_BUCKET_NAME,
                                   TEST_OBJECT_KEY,
                                   "application/octet-stream",
                                   _,
                                   _ ) )
        .WillOnce( Return( transferHandle ) );
    EXPECT_CALL( resultCallback, Call( _, _ ) ).Times( 0 );

    sender.sendStream( std::move( std::make_unique<Testing::StringbufBuilder>( "test" ) ),
                       S3UploadMetadata{ TEST_BUCKET_NAME, TEST_PREFIX, TEST_REGION, TEST_BUCKET_OWNER_ACCOUNT_ID },
                       TEST_OBJECT_KEY,
                       resultCallback.AsStdFunction() );
    ASSERT_NE( transferManagerConfiguration.transferStatusUpdatedCallback, nullptr );

    EXPECT_CALL( resultCallback, Call( _, _ ) ).Times( 0 );
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::NOT_STARTED );
    transferManagerConfiguration.transferStatusUpdatedCallback( nullptr, transferHandle );

    EXPECT_CALL( resultCallback, Call( _, _ ) ).Times( 0 );
    EXPECT_CALL( *transferManagerWrapperMock, RetryUpload( ::testing::A<const std::shared_ptr<Aws::IOStream> &>(), _ ) )
        .WillOnce( Return( transferHandle ) );
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::FAILED );
    transferManagerConfiguration.transferStatusUpdatedCallback( nullptr, transferHandle );

    std::shared_ptr<std::streambuf> failedData;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::TransmissionError, _ ) )
        .WillOnce( SaveArg<1>( &failedData ) );
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::NOT_STARTED );
    transferHandle->Restart();
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::FAILED );
    transferManagerConfiguration.transferStatusUpdatedCallback( nullptr, transferHandle );

    ASSERT_NE( failedData, nullptr );
    std::stringstream ss;
    ss << failedData.get();
    ASSERT_EQ( ss.str(), "test" );
}

TEST_F( S3SenderTest, NoCredentialsProviderForStreamUpload )
{
    S3Sender sender{ nullptr, 0 };
    MockFunction<void( ConnectivityError, std::shared_ptr<std::streambuf> )> resultCallback;
    EXPECT_CALL( resultCallback, Call( ConnectivityError::NotConfigured, _ ) ).Times( 1 );
    sender.sendStream( std::move( std::make_unique<Testing::StringbufBuilder>( "test" ) ),
                       S3UploadMetadata{ TEST_BUCKET_NAME, TEST_PREFIX, TEST_REGION, TEST_BUCKET_OWNER_ACCOUNT_ID },
                       TEST_OBJECT_KEY,
                       resultCallback.AsStdFunction() );
}

TEST_F( S3SenderTest, AsyncStreamUploadInitiatedCallbackSucceeded )
{
    S3Sender sender{ createTransferManagerWrapper, 0 };
    MockFunction<void( ConnectivityError, std::shared_ptr<std::streambuf> )> resultCallback;
    auto transferHandle = std::make_shared<Aws::Transfer::TransferHandle>( TEST_BUCKET_NAME, TEST_OBJECT_KEY );

    EXPECT_CALL( *transferManagerWrapperMock,
                 MockedUploadFile( ::testing::A<const std::shared_ptr<Aws::IOStream> &>(),
                                   TEST_BUCKET_NAME,
                                   TEST_OBJECT_KEY,
                                   "application/octet-stream",
                                   _,
                                   _ ) )
        .WillOnce( Return( transferHandle ) );
    EXPECT_CALL( resultCallback, Call( _, _ ) ).Times( 0 );

    sender.sendStream( std::move( std::make_unique<Testing::StringbufBuilder>( "test" ) ),
                       S3UploadMetadata{ TEST_BUCKET_NAME, TEST_PREFIX, TEST_REGION, TEST_BUCKET_OWNER_ACCOUNT_ID },
                       TEST_OBJECT_KEY,
                       resultCallback.AsStdFunction() );
    ASSERT_NE( transferManagerConfiguration.transferStatusUpdatedCallback, nullptr );

    EXPECT_CALL( resultCallback, Call( _, _ ) ).Times( 0 );
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::NOT_STARTED );
    transferManagerConfiguration.transferStatusUpdatedCallback( nullptr, transferHandle );

    EXPECT_CALL( resultCallback, Call( _, _ ) ).Times( 0 );
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::IN_PROGRESS );
    transferManagerConfiguration.transferStatusUpdatedCallback( nullptr, transferHandle );

    EXPECT_CALL( resultCallback, Call( ConnectivityError::Success, _ ) ).Times( 1 );
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::COMPLETED );
    transferManagerConfiguration.transferStatusUpdatedCallback( nullptr, transferHandle );
}

TEST_F( S3SenderTest, LimitNumberOfSimultaneousUploadsAndQueueTheRemaining )
{
    S3Sender sender{ createTransferManagerWrapper, 0 };
    MockFunction<void( ConnectivityError, std::shared_ptr<std::streambuf> )> resultCallback;
    auto transferHandle1 = std::make_shared<Aws::Transfer::TransferHandle>( TEST_BUCKET_NAME, "objectKey1" );

    EXPECT_CALL( *transferManagerWrapperMock,
                 MockedUploadFile( ::testing::A<const std::shared_ptr<Aws::IOStream> &>(),
                                   TEST_BUCKET_NAME,
                                   "objectKey1",
                                   "application/octet-stream",
                                   _,
                                   _ ) )
        .WillOnce( Return( transferHandle1 ) );
    EXPECT_CALL( resultCallback, Call( _, _ ) ).Times( 0 );

    // Hand over multiple files at once to the sender
    sender.sendStream( std::move( std::make_unique<Testing::StringbufBuilder>( "test" ) ),
                       S3UploadMetadata{ TEST_BUCKET_NAME, TEST_PREFIX, TEST_REGION, TEST_BUCKET_OWNER_ACCOUNT_ID },
                       "objectKey1",
                       resultCallback.AsStdFunction() );
    // The other files shouldn't be passed to transfer manager until the ongoing upload finishes
    sender.sendStream( std::move( std::make_unique<Testing::StringbufBuilder>( "test" ) ),
                       S3UploadMetadata{ TEST_BUCKET_NAME, TEST_PREFIX, TEST_REGION, TEST_BUCKET_OWNER_ACCOUNT_ID },
                       "objectKey2",
                       resultCallback.AsStdFunction() );
    sender.sendStream( std::move( std::make_unique<Testing::StringbufBuilder>( "test" ) ),
                       S3UploadMetadata{ TEST_BUCKET_NAME, TEST_PREFIX, TEST_REGION, TEST_BUCKET_OWNER_ACCOUNT_ID },
                       "objectKey3",
                       resultCallback.AsStdFunction() );

    // When the first upload completes, then the next should be sent
    auto transferHandle2 = std::make_shared<Aws::Transfer::TransferHandle>( TEST_BUCKET_NAME, "objectKey2" );
    EXPECT_CALL( *transferManagerWrapperMock,
                 MockedUploadFile( ::testing::A<const std::shared_ptr<Aws::IOStream> &>(),
                                   TEST_BUCKET_NAME,
                                   "objectKey2",
                                   "application/octet-stream",
                                   _,
                                   _ ) )
        .WillOnce( Return( transferHandle2 ) );
    EXPECT_CALL( resultCallback, Call( ConnectivityError::Success, _ ) ).Times( 1 );
    // Complete the first upload
    transferHandle1->UpdateStatus( Aws::Transfer::TransferStatus::COMPLETED );
    transferManagerConfiguration.transferStatusUpdatedCallback( nullptr, transferHandle1 );

    // When the first upload completes, then the next should be sent
    auto transferHandle3 = std::make_shared<Aws::Transfer::TransferHandle>( TEST_BUCKET_NAME, "objectKey3" );
    EXPECT_CALL( *transferManagerWrapperMock,
                 MockedUploadFile( ::testing::A<const std::shared_ptr<Aws::IOStream> &>(),
                                   TEST_BUCKET_NAME,
                                   "objectKey3",
                                   "application/octet-stream",
                                   _,
                                   _ ) )
        .WillOnce( Return( transferHandle3 ) );
    EXPECT_CALL( resultCallback, Call( ConnectivityError::Success, _ ) ).Times( 1 );
    // Complete the second upload
    transferHandle2->UpdateStatus( Aws::Transfer::TransferStatus::COMPLETED );
    transferManagerConfiguration.transferStatusUpdatedCallback( nullptr, transferHandle2 );
}

TEST_F( S3SenderTest, SkipQueuedUploadWhoseDataIsNotAvailableAnymore )
{
    S3Sender sender{ createTransferManagerWrapper, 0 };
    auto transferHandle1 = std::make_shared<Aws::Transfer::TransferHandle>( TEST_BUCKET_NAME, "objectKey1" );

    EXPECT_CALL( *transferManagerWrapperMock,
                 MockedUploadFile( ::testing::A<const std::shared_ptr<Aws::IOStream> &>(),
                                   TEST_BUCKET_NAME,
                                   "objectKey1",
                                   "application/octet-stream",
                                   _,
                                   _ ) )
        .WillOnce( Return( transferHandle1 ) );

    // Hand over multiple files at once to the sender
    MockFunction<void( ConnectivityError, std::shared_ptr<std::streambuf> )> resultCallback1;
    EXPECT_CALL( resultCallback1, Call( _, _ ) ).Times( 0 );
    sender.sendStream( std::move( std::make_unique<Testing::StringbufBuilder>( "test" ) ),
                       S3UploadMetadata{ TEST_BUCKET_NAME, TEST_PREFIX, TEST_REGION, TEST_BUCKET_OWNER_ACCOUNT_ID },
                       "objectKey1",
                       resultCallback1.AsStdFunction() );
    // Second file will return a null streambuf (as if its data were deleted), so it should be
    // skipped.
    MockFunction<void( ConnectivityError, std::shared_ptr<std::streambuf> )> resultCallback2;
    EXPECT_CALL( resultCallback2, Call( _, _ ) ).Times( 0 );
    sender.sendStream(
        std::move( std::make_unique<Testing::StringbufBuilder>( std::unique_ptr<std::streambuf>( nullptr ) ) ),
        S3UploadMetadata{ TEST_BUCKET_NAME, TEST_PREFIX, TEST_REGION, TEST_BUCKET_OWNER_ACCOUNT_ID },
        "objectKey2",
        resultCallback2.AsStdFunction() );
    MockFunction<void( ConnectivityError, std::shared_ptr<std::streambuf> )> resultCallback3;
    EXPECT_CALL( resultCallback3, Call( _, _ ) ).Times( 0 );
    sender.sendStream( std::move( std::make_unique<Testing::StringbufBuilder>( "test" ) ),
                       S3UploadMetadata{ TEST_BUCKET_NAME, TEST_PREFIX, TEST_REGION, TEST_BUCKET_OWNER_ACCOUNT_ID },
                       "objectKey3",
                       resultCallback3.AsStdFunction() );

    // When the first upload completes, then the second should be skipped and the third should be sent
    auto transferHandle3 = std::make_shared<Aws::Transfer::TransferHandle>( TEST_BUCKET_NAME, "objectKey3" );
    EXPECT_CALL( *transferManagerWrapperMock,
                 MockedUploadFile( ::testing::A<const std::shared_ptr<Aws::IOStream> &>(),
                                   TEST_BUCKET_NAME,
                                   "objectKey3",
                                   "application/octet-stream",
                                   _,
                                   _ ) )
        .WillOnce( Return( transferHandle3 ) );
    // Complete the first upload
    transferHandle1->UpdateStatus( Aws::Transfer::TransferStatus::COMPLETED );
    EXPECT_CALL( resultCallback1, Call( ConnectivityError::Success, _ ) ).Times( 1 );
    EXPECT_CALL( resultCallback2, Call( ConnectivityError::WrongInputData, _ ) ).Times( 1 );
    transferManagerConfiguration.transferStatusUpdatedCallback( nullptr, transferHandle1 );

    // Complete the third upload
    transferHandle3->UpdateStatus( Aws::Transfer::TransferStatus::COMPLETED );
    EXPECT_CALL( resultCallback3, Call( ConnectivityError::Success, _ ) ).Times( 1 );
    transferManagerConfiguration.transferStatusUpdatedCallback( nullptr, transferHandle3 );
}

TEST_F( S3SenderTest, CancelAllOngoingUploadsOnDisconnection )
{
    S3Sender sender{ createTransferManagerWrapper, 0 };
    MockFunction<void( ConnectivityError, std::shared_ptr<std::streambuf> )> resultCallback;
    auto transferHandle1 = std::make_shared<Aws::Transfer::TransferHandle>( TEST_BUCKET_NAME, "objectKey1" );

    EXPECT_CALL( *transferManagerWrapperMock,
                 MockedUploadFile( ::testing::A<const std::shared_ptr<Aws::IOStream> &>(),
                                   TEST_BUCKET_NAME,
                                   "objectKey1",
                                   "application/octet-stream",
                                   _,
                                   _ ) )
        .WillOnce( Return( transferHandle1 ) );
    EXPECT_CALL( resultCallback, Call( _, _ ) ).Times( 0 );

    // Hand over multiple files at once to the sender
    sender.sendStream( std::move( std::make_unique<Testing::StringbufBuilder>( "test" ) ),
                       S3UploadMetadata{ TEST_BUCKET_NAME, TEST_PREFIX, TEST_REGION, TEST_BUCKET_OWNER_ACCOUNT_ID },
                       "objectKey1",
                       resultCallback.AsStdFunction() );
    sender.sendStream( std::move( std::make_unique<Testing::StringbufBuilder>( "test" ) ),
                       S3UploadMetadata{ TEST_BUCKET_NAME, TEST_PREFIX, TEST_REGION, TEST_BUCKET_OWNER_ACCOUNT_ID },
                       "objectKey2",
                       resultCallback.AsStdFunction() );
    sender.sendStream( std::move( std::make_unique<Testing::StringbufBuilder>( "test" ) ),
                       S3UploadMetadata{ TEST_BUCKET_NAME, TEST_PREFIX, TEST_REGION, TEST_BUCKET_OWNER_ACCOUNT_ID },
                       "objectKey3",
                       resultCallback.AsStdFunction() );

    EXPECT_CALL( *transferManagerWrapperMock, CancelAll() );
    EXPECT_CALL( *transferManagerWrapperMock, MockedWaitUntilAllFinished( _ ) );
    sender.disconnect();
}

} // namespace IoTFleetWise
} // namespace Aws
