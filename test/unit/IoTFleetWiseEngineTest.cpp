
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "IoTFleetWiseEngine.h"
#include "CANDataTypes.h"
#include "CollectionInspectionAPITypes.h"
#include "IoTFleetWiseConfig.h"
#include "WaitUntil.h"
#include <array>
#include <cstdint>
#include <fstream>
#include <gtest/gtest.h>
#include <json/json.h>
#include <linux/can.h>
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
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#ifdef FWE_FEATURE_ROS2
#include <rclcpp/rclcpp.hpp>
#endif

namespace Aws
{
namespace IoTFleetWise
{

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

class IoTFleetWiseEngineTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        if ( !socketAvailable() )
        {
            GTEST_SKIP() << "Skipping test fixture due to unavailability of socket";
        }
#ifdef FWE_FEATURE_IWAVE_GPS
        std::ofstream iWaveGpsFile( "/tmp/engineTestIWaveGPSfile.txt" );
        iWaveGpsFile << "NO valid NMEA data";
        iWaveGpsFile.close();
#endif
#ifdef FWE_FEATURE_ROS2
        rclcpp::init( 0, NULL );
#endif
    }
    void
    TearDown() override
    {
#ifdef FWE_FEATURE_ROS2
        rclcpp::shutdown();
#endif
    }

    static bool
    socketAvailable()
    {
        auto sock = socket( PF_CAN, SOCK_DGRAM, CAN_ISOTP );
        if ( sock < 0 )
        {
            return false;
        }
        close( sock );
        return true;
    }
};

TEST_F( IoTFleetWiseEngineTest, InitAndStartEngine )
{
    Json::Value config;
    ASSERT_TRUE( IoTFleetWiseConfig::read( "static-config-ok.json", config ) );
    IoTFleetWiseEngine engine;

    std::string keyPem;
    std::string certPem;
    makeCert( keyPem, certPem );
    std::ofstream dummyPrivateKeyFile( "/tmp/dummyPrivateKey.key" );
    dummyPrivateKeyFile << keyPem;
    dummyPrivateKeyFile.close();
    std::ofstream dummyCertificateFile( "/tmp/dummyCertificate.pem" );
    dummyCertificateFile << certPem;
    dummyCertificateFile.close();

    ASSERT_TRUE( engine.connect( config ) );

    ASSERT_TRUE( engine.start() );
    ASSERT_TRUE( engine.isAlive() );
    ASSERT_TRUE( engine.disconnect() );
    ASSERT_TRUE( engine.stop() );
}

TEST_F( IoTFleetWiseEngineTest, InitAndStartEngineInlineCreds )
{
    Json::Value config;
    ASSERT_TRUE( IoTFleetWiseConfig::read( "static-config-inline-creds.json", config ) );
    IoTFleetWiseEngine engine;

    std::string keyPem;
    std::string certPem;
    makeCert( keyPem, certPem );
    config["staticConfig"]["mqttConnection"]["certificate"] = certPem;
    config["staticConfig"]["mqttConnection"]["privateKey"] = keyPem;
    config["staticConfig"]["mqttConnection"]["rootCA"] = certPem;

    ASSERT_TRUE( engine.connect( config ) );

    ASSERT_TRUE( engine.start() );
    ASSERT_TRUE( engine.isAlive() );
    ASSERT_TRUE( engine.disconnect() );
    ASSERT_TRUE( engine.stop() );
}

TEST_F( IoTFleetWiseEngineTest, CheckPublishDataQueue )
{
    Json::Value config;
    ASSERT_TRUE( IoTFleetWiseConfig::read( "static-config-ok.json", config ) );
    IoTFleetWiseEngine engine;
    ASSERT_TRUE( engine.connect( config ) );

    // Push to the publish data queue
    std::shared_ptr<TriggeredCollectionSchemeData> collectedDataPtr = std::make_shared<TriggeredCollectionSchemeData>();
    collectedDataPtr->metadata.collectionSchemeID = "123";
    collectedDataPtr->metadata.decoderID = "456";
    collectedDataPtr->triggerTime = 800;
    {
        CollectedSignal collectedSignalMsg1( 120 /*signalId*/, 800 /*receiveTime*/, 77.88 /*value*/ );
        collectedDataPtr->signals.push_back( collectedSignalMsg1 );
        CollectedSignal collectedSignalMsg2( 10 /*signalId*/, 1000 /*receiveTime*/, 46.5 /*value*/ );
        collectedDataPtr->signals.push_back( collectedSignalMsg2 );
        CollectedSignal collectedSignalMsg3( 12 /*signalId*/, 1200 /*receiveTime*/, 98.9 /*value*/ );
        collectedDataPtr->signals.push_back( collectedSignalMsg3 );
    }
    {
        std::array<uint8_t, MAX_CAN_FRAME_BYTE_SIZE> data = { 1, 2, 3, 4, 5, 6, 7, 8 };
        CollectedCanRawFrame canFrames1( 12 /*frameId*/, 1 /*nodeId*/, 815 /*receiveTime*/, data, sizeof data );
        collectedDataPtr->canFrames.push_back( canFrames1 );
        CollectedCanRawFrame canFrames2( 4 /*frameId*/, 2 /*nodeId*/, 1100 /*receiveTime*/, data, sizeof data );
        collectedDataPtr->canFrames.push_back( canFrames2 );
        CollectedCanRawFrame canFrames3( 6 /*frameId*/, 3 /*nodeId*/, 1300 /*receiveTime*/, data, sizeof data );
        collectedDataPtr->canFrames.push_back( canFrames3 );
    }

    ASSERT_TRUE( engine.mCollectedDataReadyToPublish->push( collectedDataPtr ) );

    ASSERT_TRUE( engine.start() );

    WAIT_ASSERT_TRUE( engine.isAlive() );

    WAIT_ASSERT_TRUE( engine.disconnect() );
    ASSERT_TRUE( engine.stop() );
}

TEST_F( IoTFleetWiseEngineTest, InitAndFailToStartCorruptConfig )
{
    Json::Value config;
    // Read should succeed
    ASSERT_TRUE( IoTFleetWiseConfig::read( "static-config-corrupt.json", config ) );
    IoTFleetWiseEngine engine;
    // Connect should fail as the Config file has a non complete Bus definition
    ASSERT_FALSE( engine.connect( config ) );
}

} // namespace IoTFleetWise
} // namespace Aws
