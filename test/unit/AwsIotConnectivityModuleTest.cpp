// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "AwsIotConnectivityModule.h"
#include "AwsBootstrap.h"
#include "AwsIotChannel.h"
#include "AwsSDKMemoryManager.h"
#include "CacheAndPersist.h"
#include "IConnectionTypes.h"
#include "ISender.h"
#include "MqttClientWrapper.h"
#include "MqttClientWrapperMock.h"
#include "MqttConnectionWrapper.h"
#include "MqttConnectionWrapperMock.h"
#include "PayloadManager.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <aws/crt/Types.h>
#include <aws/iot/MqttClient.h>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <future>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <list>
#include <memory>
#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/ossl_typ.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

void
makeCert( std::string &keyPem, std::string &certPem )
{
    std::unique_ptr<BIGNUM, decltype( &BN_free )> bn( BN_new(), BN_free );
    if ( !BN_set_word( bn.get(), RSA_F4 ) )
    {
        throw std::runtime_error( "error initializing big num" );
    }
    auto rsa = RSA_new(); // Ownership passed to pkey below
    if ( !RSA_generate_key_ex( rsa, 512, bn.get(), NULL ) )
    {
        RSA_free( rsa );
        throw std::runtime_error( "error generating key" );
    }
    std::unique_ptr<EVP_PKEY, decltype( &EVP_PKEY_free )> pkey( EVP_PKEY_new(), EVP_PKEY_free );
    if ( !EVP_PKEY_assign_RSA( pkey.get(), rsa ) )
    {
        throw std::runtime_error( "error assigning key" );
    }

    std::unique_ptr<X509, decltype( &X509_free )> cert( X509_new(), X509_free );
    X509_set_version( cert.get(), 2 );
    ASN1_INTEGER_set( X509_get_serialNumber( cert.get() ), 0 );
    X509_gmtime_adj( X509_get_notBefore( cert.get() ), 0 );
    X509_gmtime_adj( X509_get_notAfter( cert.get() ), 60 * 60 * 24 * 365 );
    X509_set_pubkey( cert.get(), pkey.get() );

    auto name = X509_get_subject_name( cert.get() );
    X509_NAME_add_entry_by_txt( name, "C", MBSTRING_ASC, (const unsigned char *)"US", -1, -1, 0 );
    X509_NAME_add_entry_by_txt( name, "CN", MBSTRING_ASC, (const unsigned char *)"Dummy", -1, -1, 0 );
    X509_set_issuer_name( cert.get(), name );

    if ( !X509_sign( cert.get(), pkey.get(), EVP_md5() ) )
    {
        throw std::runtime_error( "error signing certificate" );
    }

    std::unique_ptr<BIO, decltype( &BIO_free )> bio( BIO_new( BIO_s_mem() ), BIO_free );
    PEM_write_bio_PrivateKey( bio.get(), pkey.get(), NULL, NULL, 0, NULL, NULL );
    keyPem.resize( BIO_pending( bio.get() ) );
    BIO_read( bio.get(), &keyPem[0], static_cast<int>( keyPem.size() ) );

    PEM_write_bio_X509( bio.get(), cert.get() );
    certPem.resize( BIO_pending( bio.get() ) );
    BIO_read( bio.get(), &certPem[0], static_cast<int>( certPem.size() ) );
}

class AwsIotConnectivityModuleTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        makeCert( mKey, mCert );
        // Needs to initialize the SDK before creating a ClientConfiguration
        AwsBootstrap::getInstance().getClientBootStrap();
        mMqttConnectionMock = std::make_shared<NiceMock<MqttConnectionWrapperMock>>();
        mMqttClientWrapperMock = std::make_shared<NiceMock<MqttClientWrapperMock>>();
        ON_CALL( *mMqttClientWrapperMock, MockedOperatorBool() ).WillByDefault( Return( true ) );
        ON_CALL( *mMqttClientWrapperMock, NewConnection( _ ) ).WillByDefault( Return( mMqttConnectionMock ) );
        ON_CALL( *mMqttConnectionMock, SetOnMessageHandler( _ ) ).WillByDefault( Return( true ) );
        ON_CALL( *mMqttConnectionMock, Connect( _, _, _, _ ) )
            .WillByDefault( Invoke( [this]( const char *, bool, uint16_t, uint32_t ) noexcept -> bool {
                mMqttConnectionMock->mOnConnectionCompleted(
                    *mMqttConnectionMock, 0, Aws::Crt::Mqtt::ReturnCode::AWS_MQTT_CONNECT_ACCEPTED, true );
                return true;
            } ) );
        ON_CALL( *mMqttConnectionMock, Disconnect() ).WillByDefault( Return( true ) );

        mCreateMqttClientWrapper = [this]() -> std::shared_ptr<MqttClientWrapper> {
            return mMqttClientWrapperMock;
        };
    }

    std::string mKey;
    std::string mCert;
    std::shared_ptr<NiceMock<MqttConnectionWrapperMock>> mMqttConnectionMock;
    std::shared_ptr<NiceMock<MqttClientWrapperMock>> mMqttClientWrapperMock;
    std::function<std::shared_ptr<MqttClientWrapper>()> mCreateMqttClientWrapper;
};

/** @brief  Test attempting to disconnect when connection has already failed */
TEST_F( AwsIotConnectivityModuleTest, disconnectAfterFailedConnect )
{
    std::shared_ptr<AwsIotConnectivityModule> m =
        std::make_shared<AwsIotConnectivityModule>( "", "", "", "", "", mCreateMqttClientWrapper );
    ASSERT_FALSE( m->connect() );
    // disconnect must only disconnect when connection is available so this should not seg fault
    m->disconnect();
}

/** @brief Test successful connection */
TEST_F( AwsIotConnectivityModuleTest, connectSuccessfully )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>(
        mKey, mCert, "", "https://abcd", "clientIdTest", mCreateMqttClientWrapper, true );

    ASSERT_TRUE( m->connect() );

    mMqttConnectionMock->mOnDisconnect( *mMqttConnectionMock );

    m->disconnect();
}

/** @brief Test successful connection with root CA */
TEST_F( AwsIotConnectivityModuleTest, connectSuccessfullyWithRootCA )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>(
        mKey, mCert, mCert, "https://abcd", "clientIdTest", mCreateMqttClientWrapper, true );

    ASSERT_TRUE( m->connect() );

    mMqttConnectionMock->mOnDisconnect( *mMqttConnectionMock );

    m->disconnect();
}

/** @brief Test trying to connect, where certificate is invalid */
TEST_F( AwsIotConnectivityModuleTest, connectFailsBadCert )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>(
        mKey, "", "", "https://abcd", "clientIdTest", mCreateMqttClientWrapper, true );

    ASSERT_FALSE( m->connect() );
}

/** @brief Test trying to connect, where creation of the client fails */
TEST_F( AwsIotConnectivityModuleTest, connectFailsOnClientCreation )
{
    EXPECT_CALL( *mMqttClientWrapperMock, MockedOperatorBool() )
        .Times( AtLeast( 1 ) )
        .WillRepeatedly( Return( false ) );

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>(
        mKey, mCert, "", "https://abcd", "clientIdTest", mCreateMqttClientWrapper, true );
    ASSERT_FALSE( m->connect() );
}

/** @brief Test opening a connection, then interrupting it and resuming it */
TEST_F( AwsIotConnectivityModuleTest, connectionInterrupted )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>(
        mKey, mCert, "", "https://abcd", "clientIdTest", mCreateMqttClientWrapper );
    ASSERT_TRUE( m->connect() );

    mMqttConnectionMock->mOnConnectionInterrupted( *mMqttConnectionMock, 10 );
    mMqttConnectionMock->mOnConnectionResumed(
        *mMqttConnectionMock, Aws::Crt::Mqtt::ReturnCode::AWS_MQTT_CONNECT_ACCEPTED, true );

    mMqttConnectionMock->mOnDisconnect( *mMqttConnectionMock );
}

/** @brief Test connecting when it fails after a delay */
TEST_F( AwsIotConnectivityModuleTest, connectFailsServerUnavailableWithDelay )
{
    std::atomic<bool> killAllThread( false );
    std::promise<void> completed;
    std::thread completeThread( [this, &killAllThread, &completed]() {
        while ( mMqttConnectionMock->mOnConnectionCompleted == nullptr && !killAllThread )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        mMqttConnectionMock->mOnConnectionCompleted(
            *mMqttConnectionMock, 0, Aws::Crt::Mqtt::ReturnCode::AWS_MQTT_CONNECT_SERVER_UNAVAILABLE, true );
        completed.set_value();
    } );

    std::thread disconnectThread( [this, &killAllThread, &completed]() {
        while ( mMqttConnectionMock->mOnDisconnect == nullptr && !killAllThread )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        }
        completed.get_future().wait();
        std::this_thread::sleep_for( std::chrono::milliseconds( 5 ) );
        mMqttConnectionMock->mOnDisconnect( *mMqttConnectionMock );
    } );

    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>(
        mKey, mCert, "", "https://abcd", "clientIdTest", mCreateMqttClientWrapper );

    EXPECT_CALL( *mMqttConnectionMock, Connect( _, _, _, _ ) ).Times( 1 ).WillOnce( Return( true ) );
    // We want to see exactly one call to disconnect
    EXPECT_CALL( *mMqttConnectionMock, Disconnect() ).Times( 1 ).WillRepeatedly( Return( true ) );

    ASSERT_FALSE( m->connect() );
    std::this_thread::sleep_for( std::chrono::milliseconds( 20 ) );
    killAllThread = true;
    completeThread.join();
    disconnectThread.join();
}

/** @brief Test subscribing without a configured topic, expect an error */
TEST_F( AwsIotConnectivityModuleTest, subscribeWithoutTopic )
{
    std::shared_ptr<AwsIotConnectivityModule> m =
        std::make_shared<AwsIotConnectivityModule>( "", "", "", "", "", nullptr );
    AwsIotChannel c( m.get(), nullptr, m->mConnection );
    ASSERT_EQ( c.subscribe(), ConnectivityError::NotConfigured );
    c.invalidateConnection();
}

/** @brief Test subscribing without being connected, expect an error */
TEST_F( AwsIotConnectivityModuleTest, subscribeWithoutBeingConnected )
{
    std::shared_ptr<AwsIotConnectivityModule> m =
        std::make_shared<AwsIotConnectivityModule>( "", "", "", "", "", nullptr );
    AwsIotChannel c( m.get(), nullptr, m->mConnection );
    c.setTopic( "topic" );
    ASSERT_EQ( c.subscribe(), ConnectivityError::NoConnection );
    c.invalidateConnection();
}

/** @brief Test successful subscription */
TEST_F( AwsIotConnectivityModuleTest, subscribeSuccessfully )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>(
        mKey, mCert, "", "https://abcd", "clientIdTest", mCreateMqttClientWrapper );

    ASSERT_TRUE( m->connect() );
    AwsIotChannel c( m.get(), nullptr, m->mConnection );

    c.setTopic( "topic" );
    EXPECT_CALL( *mMqttConnectionMock, Subscribe( _, _, _, _ ) )
        .Times( 1 )
        .WillRepeatedly( Invoke( [this]( const char *,
                                         aws_mqtt_qos,
                                         MqttConnectionWrapper::OnMessageReceivedHandler &&,
                                         MqttConnectionWrapper::OnSubAckHandler &&onSubAck ) -> bool {
            onSubAck( *mMqttConnectionMock, 10, "topic", aws_mqtt_qos::AWS_MQTT_QOS_AT_MOST_ONCE, 0 );
            return true;
        } ) );
    ASSERT_EQ( c.subscribe(), ConnectivityError::Success );

    EXPECT_CALL( *mMqttConnectionMock, Unsubscribe( _, _ ) )
        .Times( AtLeast( 1 ) )
        .WillRepeatedly(
            Invoke( [this]( const char *,
                            MqttConnectionWrapper::OnOperationCompleteHandler &&onOpComplete ) noexcept -> uint16_t {
                onOpComplete( *mMqttConnectionMock, 0, 0 );
                return 0;
            } ) );
    c.unsubscribe();
    mMqttConnectionMock->mOnDisconnect( *mMqttConnectionMock );
    c.invalidateConnection();
}

/** @brief Test without a configured topic, expect an error */
TEST_F( AwsIotConnectivityModuleTest, sendWithoutTopic )
{
    std::shared_ptr<AwsIotConnectivityModule> m =
        std::make_shared<AwsIotConnectivityModule>( "", "", "", "", "", nullptr );
    AwsIotChannel c( m.get(), nullptr, m->mConnection );
    std::uint8_t input[] = { 0xca, 0xfe };
    ASSERT_EQ( c.sendBuffer( input, sizeof( input ) ), ConnectivityError::NotConfigured );
    c.invalidateConnection();
}

/** @brief Test sending without a connection, expect an error */
TEST_F( AwsIotConnectivityModuleTest, sendWithoutConnection )
{
    std::shared_ptr<AwsIotConnectivityModule> m =
        std::make_shared<AwsIotConnectivityModule>( "", "", "", "", "", nullptr );
    AwsIotChannel c( m.get(), nullptr, m->mConnection );
    std::uint8_t input[] = { 0xca, 0xfe };
    c.setTopic( "topic" );
    ASSERT_EQ( c.sendBuffer( input, sizeof( input ) ), ConnectivityError::NoConnection );
    c.invalidateConnection();
}

/** @brief Test passing a null pointer, expect an error */
TEST_F( AwsIotConnectivityModuleTest, sendWrongInput )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>(
        mKey, mCert, "", "https://abcd", "clientIdTest", mCreateMqttClientWrapper );
    AwsIotChannel c( m.get(), nullptr, m->mConnection );
    ASSERT_TRUE( m->connect() );
    c.setTopic( "topic" );
    ASSERT_EQ( c.sendBuffer( nullptr, 10 ), ConnectivityError::WrongInputData );
    mMqttConnectionMock->mOnDisconnect( *mMqttConnectionMock );
    c.invalidateConnection();
}

/** @brief Test sending a message larger then the maximum send size, expect an error */
TEST_F( AwsIotConnectivityModuleTest, sendTooBig )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>(
        mKey, mCert, "", "https://abcd", "clientIdTest", mCreateMqttClientWrapper );
    AwsIotChannel c( m.get(), nullptr, m->mConnection );

    ASSERT_TRUE( m->connect() );
    c.setTopic( "topic" );
    std::vector<uint8_t> a;
    a.resize( c.getMaxSendSize() + 1U );
    ASSERT_EQ( c.sendBuffer( a.data(), a.size() ), ConnectivityError::WrongInputData );
    mMqttConnectionMock->mOnDisconnect( *mMqttConnectionMock );
    c.invalidateConnection();
}

/** @brief Test sending multiple messages. The API supports queuing of messages, so send more than one
 * message, allow one to be sent, queue another and then send the rest. Also indicate one of
 * messages as failed to send to check that path. */
TEST_F( AwsIotConnectivityModuleTest, sendMultiple )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>(
        mKey, mCert, "", "https://abcd", "clientIdTest", mCreateMqttClientWrapper );
    ASSERT_TRUE( m->connect() );
    AwsIotChannel c( m.get(), nullptr, m->mConnection );

    std::uint8_t input[] = { 0xca, 0xfe };
    c.setTopic( "topic" );
    std::list<MqttConnectionWrapper::OnOperationCompleteHandler> completeHandlers;
    EXPECT_CALL( *mMqttConnectionMock, Publish( _, _, _, _, _ ) )
        .Times( 3 )
        .WillRepeatedly( Invoke(
            [&completeHandlers]( const char *,
                                 aws_mqtt_qos,
                                 bool,
                                 const struct aws_byte_buf &,
                                 MqttConnectionWrapper::OnOperationCompleteHandler &&onOpComplete ) noexcept -> bool {
                completeHandlers.push_back( std::move( onOpComplete ) );
                return true;
            } ) );

    // Queue 2 packets
    ASSERT_EQ( c.sendBuffer( input, sizeof( input ) ), ConnectivityError::Success );
    ASSERT_EQ( c.sendBuffer( input, sizeof( input ) ), ConnectivityError::Success );

    // Confirm 1st (success as packetId is 1---v):
    completeHandlers.front().operator()( *mMqttConnectionMock, 1, 0 );
    completeHandlers.pop_front();

    // Queue another:
    ASSERT_EQ( c.sendBuffer( input, sizeof( input ) ), ConnectivityError::Success );

    // Confirm 2nd (success as packetId is 2---v):
    completeHandlers.front().operator()( *mMqttConnectionMock, 2, 0 );
    completeHandlers.pop_front();
    // Confirm 3rd (failure as packetId is 0---v) (Not a test case failure, but a stimulated failure for code coverage)
    completeHandlers.front().operator()( *mMqttConnectionMock, 0, 0 );
    completeHandlers.pop_front();

    ASSERT_EQ( c.getPayloadCountSent(), 2 );

    mMqttConnectionMock->mOnDisconnect( *mMqttConnectionMock );
    c.invalidateConnection();
}

/** @brief Test SDK exceeds RAM and Channel stops sending */
TEST_F( AwsIotConnectivityModuleTest, sdkRAMExceeded )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>(
        mKey, mCert, "", "https://abcd", "clientIdTest", mCreateMqttClientWrapper );
    ASSERT_TRUE( m->connect() );

    auto &memMgr = AwsSDKMemoryManager::getInstance();
    void *alloc1 = memMgr.AllocateMemory( 600000000, alignof( std::size_t ) );
    ASSERT_NE( alloc1, nullptr );
    memMgr.FreeMemory( alloc1 );

    void *alloc2 = memMgr.AllocateMemory( 600000010, alignof( std::size_t ) );
    ASSERT_NE( alloc2, nullptr );
    memMgr.FreeMemory( alloc2 );

    AwsIotChannel c( m.get(), nullptr, m->mConnection );
    c.setTopic( "topic" );
    std::array<std::uint8_t, 2> input = { 0xCA, 0xFE };
    const auto required = input.size() * sizeof( std::uint8_t );
    {
        void *alloc3 =
            memMgr.AllocateMemory( 50 * AwsSDKMemoryManager::getInstance().getLimit(), alignof( std::size_t ) );
        ASSERT_NE( alloc3, nullptr );

        ASSERT_EQ( c.sendBuffer( input.data(), input.size() * sizeof( std::uint8_t ) ),
                   ConnectivityError::QuotaReached );
        memMgr.FreeMemory( alloc3 );
    }
    {
        constexpr auto offset = alignof( std::max_align_t );
        // check that we will be out of memory even if we allocate less than the max because of the allocator's offset
        // in the below alloc we are leaving space for the input
        auto alloc4 = memMgr.AllocateMemory( AwsSDKMemoryManager::getInstance().getLimit() - ( offset + required ),
                                             alignof( std::size_t ) );
        ASSERT_NE( alloc4, nullptr );
        ASSERT_EQ( c.sendBuffer( input.data(), sizeof( input ) ), ConnectivityError::QuotaReached );
        memMgr.FreeMemory( alloc4 );

        // check that allocation and hence send succeed when there is just enough memory
        // here we subtract the offset twice - once for MAXIMUM_AWS_SDK_HEAP_MEMORY_BYTES and once for the input
        auto alloc5 = memMgr.AllocateMemory(
            AwsSDKMemoryManager::getInstance().getLimit() - ( ( 2 * offset ) + required ), alignof( std::size_t ) );
        ASSERT_NE( alloc5, nullptr );

        std::list<MqttConnectionWrapper::OnOperationCompleteHandler> completeHandlers;
        EXPECT_CALL( *mMqttConnectionMock, Publish( _, _, _, _, _ ) )
            .Times( AnyNumber() )
            .WillRepeatedly(
                Invoke( [&completeHandlers](
                            const char *,
                            aws_mqtt_qos,
                            bool,
                            const struct aws_byte_buf &,
                            MqttConnectionWrapper::OnOperationCompleteHandler &&onOpComplete ) noexcept -> bool {
                    completeHandlers.push_back( std::move( onOpComplete ) );
                    return true;
                } ) );

        ASSERT_EQ( c.sendBuffer( input.data(), sizeof( input ) ), ConnectivityError::Success );
        memMgr.FreeMemory( alloc5 );

        // // Confirm 1st (success as packetId is 1---v):
        completeHandlers.front().operator()( *mMqttConnectionMock, 1, 0 );
        completeHandlers.pop_front();
    }

    mMqttConnectionMock->mOnDisconnect( *mMqttConnectionMock );
    c.invalidateConnection();
}

/** @brief Test sending file over MQTT without topic */
TEST_F( AwsIotConnectivityModuleTest, sendFileOverMQTTNoTopic )
{
    std::shared_ptr<AwsIotConnectivityModule> m =
        std::make_shared<AwsIotConnectivityModule>( "", "", "", "", "", nullptr );
    AwsIotChannel c( m.get(), nullptr, m->mConnection );
    std::string filename{ "testFile.json" };
    ASSERT_EQ( c.sendFile( filename, 0 ), ConnectivityError::NotConfigured );
    c.invalidateConnection();
}

/** @brief Test sending file over MQTT, payload manager not defined */
TEST_F( AwsIotConnectivityModuleTest, sendFileOverMQTTNoPayloadManager )
{
    std::shared_ptr<AwsIotConnectivityModule> m =
        std::make_shared<AwsIotConnectivityModule>( "", "", "", "", "", nullptr );
    AwsIotChannel c( m.get(), nullptr, m->mConnection );
    std::string filename{ "testFile.json" };
    c.setTopic( "topic" );
    ASSERT_EQ( c.sendFile( filename, 0 ), ConnectivityError::NotConfigured );
    c.invalidateConnection();
}

/** @brief Test sending file over MQTT, filename not defined */
TEST_F( AwsIotConnectivityModuleTest, sendFileOverMQTTNoFilename )
{
    std::shared_ptr<AwsIotConnectivityModule> m =
        std::make_shared<AwsIotConnectivityModule>( "", "", "", "", "", nullptr );
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        const std::shared_ptr<CacheAndPersist> persistencyPtr =
            std::make_shared<CacheAndPersist>( std::string( buffer ) + "/Persistency", 131072 );
        persistencyPtr->init();
        const std::shared_ptr<PayloadManager> payloadManager = std::make_shared<PayloadManager>( persistencyPtr );
        AwsIotChannel c( m.get(), payloadManager, m->mConnection );
        std::string filename;
        c.setTopic( "topic" );
        ASSERT_EQ( c.sendFile( filename, 0 ), ConnectivityError::WrongInputData );
        c.invalidateConnection();
    }
}

/** @brief Test sending file over MQTT, file size too big */
TEST_F( AwsIotConnectivityModuleTest, sendFileOverMQTTBigFile )
{
    std::shared_ptr<AwsIotConnectivityModule> m =
        std::make_shared<AwsIotConnectivityModule>( "", "", "", "", "", nullptr );
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        const std::shared_ptr<CacheAndPersist> persistencyPtr =
            std::make_shared<CacheAndPersist>( std::string( buffer ) + "/Persistency", 131072 );
        persistencyPtr->init();
        const std::shared_ptr<PayloadManager> payloadManager = std::make_shared<PayloadManager>( persistencyPtr );
        AwsIotChannel c( m.get(), payloadManager, m->mConnection );
        std::string filename = "testFile.json";
        c.setTopic( "topic" );
        ASSERT_EQ( c.sendFile( filename, 150000 ), ConnectivityError::WrongInputData );
        c.invalidateConnection();
    }
}

TEST_F( AwsIotConnectivityModuleTest, sendFileOverMQTTNoFile )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>(
            mKey, mCert, "", "https://abcd", "clientIdTest", mCreateMqttClientWrapper );

        const std::shared_ptr<CacheAndPersist> persistencyPtr =
            std::make_shared<CacheAndPersist>( std::string( buffer ) + "/Persistency", 131072 );
        persistencyPtr->init();
        const std::shared_ptr<PayloadManager> payloadManager = std::make_shared<PayloadManager>( persistencyPtr );

        AwsIotChannel c( m.get(), payloadManager, m->mConnection );

        ASSERT_TRUE( m->connect() );
        c.setTopic( "topic" );

        std::string filename = "testFile.json";
        ASSERT_EQ( c.sendFile( filename, 100 ), ConnectivityError::WrongInputData );

        mMqttConnectionMock->mOnDisconnect( *mMqttConnectionMock );
        c.invalidateConnection();
    }
}

TEST_F( AwsIotConnectivityModuleTest, sendFileOverMQTTSdkRAMExceeded )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>(
            mKey, mCert, "", "https://abcd", "clientIdTest", mCreateMqttClientWrapper );
        ASSERT_TRUE( m->connect() );

        auto &memMgr = AwsSDKMemoryManager::getInstance();
        void *alloc1 = memMgr.AllocateMemory( 600000000, alignof( std::size_t ) );
        ASSERT_NE( alloc1, nullptr );
        memMgr.FreeMemory( alloc1 );

        void *alloc2 = memMgr.AllocateMemory( 600000010, alignof( std::size_t ) );
        ASSERT_NE( alloc2, nullptr );
        memMgr.FreeMemory( alloc2 );

        const std::shared_ptr<CacheAndPersist> persistencyPtr =
            std::make_shared<CacheAndPersist>( std::string( buffer ) + "/Persistency", 131072 );
        persistencyPtr->init();

        const std::shared_ptr<PayloadManager> payloadManager = std::make_shared<PayloadManager>( persistencyPtr );
        AwsIotChannel c( m.get(), payloadManager, m->mConnection );

        c.setTopic( "topic" );
        // Fake file content
        std::array<std::uint8_t, 2> input = { 0xCA, 0xFE };
        const auto required = input.size() * sizeof( std::uint8_t );
        std::string filename = "testFile.bin";
        {
            void *alloc3 =
                memMgr.AllocateMemory( 50 * AwsSDKMemoryManager::getInstance().getLimit(), alignof( std::size_t ) );
            ASSERT_NE( alloc3, nullptr );
            CollectionSchemeParams collectionSchemeParams;
            collectionSchemeParams.persist = true;
            collectionSchemeParams.compression = false;
            ASSERT_EQ( c.sendFile( filename, input.size() * sizeof( std::uint8_t ), collectionSchemeParams ),
                       ConnectivityError::QuotaReached );
            memMgr.FreeMemory( alloc3 );
        }
        {
            constexpr auto offset = alignof( std::max_align_t );
            // check that we will be out of memory even if we allocate less than the max because of the allocator's
            // offset in the below alloc we are leaving space for the input
            auto alloc4 = memMgr.AllocateMemory( AwsSDKMemoryManager::getInstance().getLimit() - ( offset + required ),
                                                 alignof( std::size_t ) );
            ASSERT_NE( alloc4, nullptr );
            ASSERT_EQ( c.sendFile( filename, sizeof( input ) ), ConnectivityError::QuotaReached );
            memMgr.FreeMemory( alloc4 );
        }

        mMqttConnectionMock->mOnDisconnect( *mMqttConnectionMock );
        c.invalidateConnection();
    }
}

TEST_F( AwsIotConnectivityModuleTest, sendFileOverMQTT )
{
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        int ret = std::system( "rm -rf ./Persistency && mkdir ./Persistency" );
        ASSERT_FALSE( WIFEXITED( ret ) == 0 );

        std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>(
            mKey, mCert, "", "https://abcd", "clientIdTest", mCreateMqttClientWrapper );

        const std::shared_ptr<CacheAndPersist> persistencyPtr =
            std::make_shared<CacheAndPersist>( std::string( buffer ) + "/Persistency", 131072 );
        persistencyPtr->init();

        std::string testData = "abcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qaabcdefjh!24$iklmnop!24$3@qabbbb";
        const uint8_t *stringData = reinterpret_cast<const uint8_t *>( testData.data() );

        std::string filename = "testFile.bin";
        persistencyPtr->write( stringData, testData.size(), DataType::EDGE_TO_CLOUD_PAYLOAD, filename );

        const std::shared_ptr<PayloadManager> payloadManager = std::make_shared<PayloadManager>( persistencyPtr );

        AwsIotChannel c( m.get(), payloadManager, m->mConnection );
        ASSERT_TRUE( m->connect() );
        c.setTopic( "topic" );

        std::list<MqttConnectionWrapper::OnOperationCompleteHandler> completeHandlers;
        EXPECT_CALL( *mMqttConnectionMock, Publish( _, _, _, _, _ ) )
            .Times( 2 )
            .WillRepeatedly(
                Invoke( [&completeHandlers](
                            const char *,
                            aws_mqtt_qos,
                            bool,
                            const struct aws_byte_buf &,
                            MqttConnectionWrapper::OnOperationCompleteHandler &&onOpComplete ) noexcept -> bool {
                    completeHandlers.push_back( std::move( onOpComplete ) );
                    return true;
                } ) );

        ASSERT_EQ( c.sendFile( filename, testData.size() ), ConnectivityError::Success );

        // Confirm 1st (success as packetId is 1---v):
        completeHandlers.front().operator()( *mMqttConnectionMock, 1, 0 );
        completeHandlers.pop_front();

        // Test callback return false
        persistencyPtr->write( stringData, testData.size(), DataType::EDGE_TO_CLOUD_PAYLOAD, filename );
        ASSERT_EQ( c.sendFile( filename, testData.size() ), ConnectivityError::Success );

        completeHandlers.front().operator()( *mMqttConnectionMock, 0, 0 );
        completeHandlers.pop_front();

        mMqttConnectionMock->mOnDisconnect( *mMqttConnectionMock );
        c.invalidateConnection();
    }
}

/** @brief Test sending file over MQTT, no connection */
TEST_F( AwsIotConnectivityModuleTest, sendFileOverMQTTNoConnection )
{
    std::shared_ptr<AwsIotConnectivityModule> m =
        std::make_shared<AwsIotConnectivityModule>( "", "", "", "", "", nullptr );
    char buffer[PATH_MAX];
    if ( getcwd( buffer, sizeof( buffer ) ) != NULL )
    {
        const std::shared_ptr<CacheAndPersist> persistencyPtr =
            std::make_shared<CacheAndPersist>( std::string( buffer ) + "/Persistency", 131072 );
        persistencyPtr->init();
        const std::shared_ptr<PayloadManager> payloadManager = std::make_shared<PayloadManager>( persistencyPtr );
        AwsIotChannel c( m.get(), payloadManager, m->mConnection );
        std::string filename = "testFile.json";
        c.setTopic( "topic" );
        ASSERT_EQ( c.sendFile( filename, 100 ), ConnectivityError::NoConnection );
        CollectionSchemeParams collectionSchemeParams;
        collectionSchemeParams.persist = true;
        collectionSchemeParams.compression = false;
        ASSERT_EQ( c.sendFile( filename, 100, collectionSchemeParams ), ConnectivityError::NoConnection );
        c.invalidateConnection();
    }
}

/** @brief Test the separate thread with exponential backoff that tries to connect until connection succeeds */
TEST_F( AwsIotConnectivityModuleTest, asyncConnect )
{
    std::shared_ptr<AwsIotConnectivityModule> m = std::make_shared<AwsIotConnectivityModule>(
        mKey, mCert, "", "https://abcd", "clientIdTest", mCreateMqttClientWrapper, true );

    EXPECT_CALL( *mMqttConnectionMock, Connect( _, _, _, _ ) ).Times( 1 ).WillOnce( Return( true ) );

    ASSERT_TRUE( m->connect() );

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) ); // first attempt should come immediately

    EXPECT_CALL( *mMqttConnectionMock, Connect( _, _, _, _ ) ).Times( 1 ).WillOnce( Return( true ) );
    mMqttConnectionMock->mOnConnectionCompleted(
        *mMqttConnectionMock, 0, Aws::Crt::Mqtt::ReturnCode::AWS_MQTT_CONNECT_SERVER_UNAVAILABLE, true );
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    mMqttConnectionMock->mOnDisconnect( *mMqttConnectionMock );
    std::this_thread::sleep_for( std::chrono::milliseconds( 1100 ) ); // minimum wait time 1 second

    EXPECT_CALL( *mMqttConnectionMock, Connect( _, _, _, _ ) ).Times( 1 ).WillOnce( Return( true ) );
    mMqttConnectionMock->mOnConnectionCompleted(
        *mMqttConnectionMock, 0, Aws::Crt::Mqtt::ReturnCode::AWS_MQTT_CONNECT_SERVER_UNAVAILABLE, true );
    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
    mMqttConnectionMock->mOnDisconnect( *mMqttConnectionMock );
    std::this_thread::sleep_for( std::chrono::milliseconds( 2100 ) ); // exponential backoff now 2 seconds

    EXPECT_CALL( *mMqttConnectionMock, Connect( _, _, _, _ ) ).Times( 0 );
    mMqttConnectionMock->mOnConnectionCompleted(
        *mMqttConnectionMock, 0, Aws::Crt::Mqtt::ReturnCode::AWS_MQTT_CONNECT_ACCEPTED, true );

    std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

    mMqttConnectionMock->mOnDisconnect( *mMqttConnectionMock );
}

} // namespace IoTFleetWise
} // namespace Aws
