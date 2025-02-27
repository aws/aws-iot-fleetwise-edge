// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionSchemeManagerMock.h"
#include "CollectionSchemeManagerTest.h" // IWYU pragma: associated
#include "Testing.h"
#include "aws/iotfleetwise/CANInterfaceIDTranslator.h"
#include "aws/iotfleetwise/CacheAndPersist.h"
#include "aws/iotfleetwise/CheckinSender.h"
#include "aws/iotfleetwise/ICollectionSchemeList.h"
#include <boost/filesystem.hpp>
#include <cstdlib>
#include <fstream> // IWYU pragma: keep
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#ifdef FWE_FEATURE_STORE_AND_FORWARD
#include "collection_schemes.pb.h"
#include "common_types.pb.h"
#endif

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::Return;
using ::testing::ReturnRef;

/** @brief
 * This test validates the usage of the store API of the persistency module.
 */
TEST( PersistencyTest, storeTest )
{
    SyncID strDecoderManifestID1 = "DM1";
    // create testPersistency
    std::shared_ptr<mockCacheAndPersist> testPersistency = std::make_shared<mockCacheAndPersist>();
    // build decodermanifest testDM1
    std::shared_ptr<mockDecoderManifest> testDM1 = std::make_shared<mockDecoderManifest>();
    std::shared_ptr<mockCollectionSchemeList> testPL = std::make_shared<mockCollectionSchemeList>();
    std::vector<uint8_t> dataPL = { '1', '2', '3', '4' };
    std::vector<uint8_t> dataDM = { '1', '2', '3', '4' };
    std::vector<uint8_t> dataEmpty;

    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper gmocktest(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), strDecoderManifestID1 );
    EXPECT_CALL( *testPL, getData() )
        .WillOnce( ReturnRef( dataEmpty ) )
        .WillOnce( ReturnRef( dataPL ) )
        .WillOnce( ReturnRef( dataPL ) );
    EXPECT_CALL( *testDM1, getData() )
        .WillOnce( ReturnRef( dataEmpty ) )
        .WillOnce( ReturnRef( dataDM ) )
        .WillOnce( ReturnRef( dataDM ) );
    EXPECT_CALL( *testPersistency, write( _, _, DataType::COLLECTION_SCHEME_LIST, _ ) )
        .WillOnce( Return( ErrorCode::SUCCESS ) )
        .WillOnce( Return( ErrorCode::MEMORY_FULL ) );
    EXPECT_CALL( *testPersistency, write( _, _, DataType::DECODER_MANIFEST, _ ) )
        .WillOnce( Return( ErrorCode::SUCCESS ) )
        .WillOnce( Return( ErrorCode::MEMORY_FULL ) );

    // all nullptr
    gmocktest.store( DataType::COLLECTION_SCHEME_LIST );
    // collectionSchemeList nullptr
    gmocktest.setCollectionSchemePersistency( testPersistency );
    gmocktest.store( DataType::COLLECTION_SCHEME_LIST );
    // DM nullptr
    gmocktest.store( DataType::DECODER_MANIFEST );
    // wrong data type
    gmocktest.store( DataType::EDGE_TO_CLOUD_PAYLOAD );
    gmocktest.setCollectionSchemeList( testPL );
    gmocktest.setDecoderManifest( testDM1 );
    // getData() return empty
    gmocktest.store( DataType::COLLECTION_SCHEME_LIST );
    gmocktest.store( DataType::DECODER_MANIFEST );
    // getData() return >0, write returns success
    gmocktest.store( DataType::COLLECTION_SCHEME_LIST );
    gmocktest.store( DataType::DECODER_MANIFEST );
    // getData() return >0, write returns memory_full
    gmocktest.store( DataType::COLLECTION_SCHEME_LIST );
    gmocktest.store( DataType::DECODER_MANIFEST );
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    auto lastKnownStateIngestionMock = std::make_shared<LastKnownStateIngestionMock>();
    EXPECT_CALL( *lastKnownStateIngestionMock, getData() ).WillRepeatedly( ReturnRef( dataPL ) );
    EXPECT_CALL( *testPersistency, write( _, _, DataType::STATE_TEMPLATE_LIST, _ ) )
        .WillRepeatedly( Return( ErrorCode::SUCCESS ) );
    gmocktest.store( DataType::STATE_TEMPLATE_LIST );
    gmocktest.setLastKnownStateIngestion( lastKnownStateIngestionMock );
    gmocktest.store( DataType::STATE_TEMPLATE_LIST );
#endif
}

/** @brief
 * This test validates the usage of the retrieve API of the persistency module.
 */
TEST( PersistencyTest, retrieveTest )
{
    SyncID strDecoderManifestID1 = "DM1";
    // create testPersistency
    std::shared_ptr<mockCacheAndPersist> testPersistency = std::make_shared<mockCacheAndPersist>();
    // build decodermanifest testDM1
    std::shared_ptr<mockDecoderManifest> testDM = std::make_shared<mockDecoderManifest>();
    std::shared_ptr<mockCollectionSchemeList> testPL = std::make_shared<mockCollectionSchemeList>();

    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper gmocktest(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ), strDecoderManifestID1 );
    EXPECT_CALL( *testPersistency, getSize( DataType::COLLECTION_SCHEME_LIST, _ ) )
        .WillOnce( Return( 0 ) )
        .WillOnce( Return( 100 ) )
        .WillOnce( Return( 100 ) );
    EXPECT_CALL( *testPersistency, getSize( DataType::DECODER_MANIFEST, _ ) )
        .WillOnce( Return( 0 ) )
        .WillOnce( Return( 100 ) )
        .WillOnce( Return( 100 ) );
    EXPECT_CALL( *testPersistency, read( _, _, DataType::COLLECTION_SCHEME_LIST, _ ) )
        .WillOnce( Return( ErrorCode::FILESYSTEM_ERROR ) )
        .WillOnce( Return( ErrorCode::SUCCESS ) );
    EXPECT_CALL( *testPersistency, read( _, _, DataType::DECODER_MANIFEST, _ ) )
        .WillOnce( Return( ErrorCode::FILESYSTEM_ERROR ) )
        .WillOnce( Return( ErrorCode::SUCCESS ) );
    EXPECT_CALL( *testPL, copyData( _, 100 ) ).WillOnce( Return( true ) );
    EXPECT_CALL( *testDM, copyData( _, 100 ) ).WillOnce( Return( true ) );

    // all nullptr
    ASSERT_FALSE( gmocktest.retrieve( DataType::COLLECTION_SCHEME_LIST ) );
    // wrong datatype
    gmocktest.setCollectionSchemePersistency( testPersistency );
    ASSERT_FALSE( gmocktest.retrieve( DataType::EDGE_TO_CLOUD_PAYLOAD ) );
    // zero size
    ASSERT_FALSE( gmocktest.retrieve( DataType::COLLECTION_SCHEME_LIST ) );
    ASSERT_FALSE( gmocktest.retrieve( DataType::DECODER_MANIFEST ) );

    gmocktest.setCollectionSchemeList( testPL );
    gmocktest.setDecoderManifest( testDM );
    // read failure
    ASSERT_FALSE( gmocktest.retrieve( DataType::COLLECTION_SCHEME_LIST ) );
    ASSERT_FALSE( gmocktest.getmProcessCollectionScheme() );
    ASSERT_FALSE( gmocktest.retrieve( DataType::DECODER_MANIFEST ) );
    ASSERT_FALSE( gmocktest.getmProcessDecoderManifest() );
    // read success collectionSchemeList valid, dm valid
    ASSERT_TRUE( gmocktest.retrieve( DataType::COLLECTION_SCHEME_LIST ) );
    ASSERT_TRUE( gmocktest.getmProcessCollectionScheme() );
    ASSERT_TRUE( gmocktest.retrieve( DataType::DECODER_MANIFEST ) );
    ASSERT_TRUE( gmocktest.getmProcessDecoderManifest() );
}

/** @brief
 * This test validates the usage of the store/retrieve APIs combined of the persistency module.
 */
TEST( PersistencyTest, StoreAndRetrieve )
{
    auto persistenceRootDir = getTempDir();
    std::shared_ptr<CacheAndPersist> testPersistency =
        std::make_shared<CacheAndPersist>( persistenceRootDir.string(), 4096 );
    ASSERT_TRUE( testPersistency->init() );
    std::string dataPL =
        "if the destructor for an automatic object is explicitly invoked, and the block is subsequently left in a "
        "manner that would ordinarily invoke implicit destruction of the object, the behavior is undefined";
    std::string dataDM =
        "explicit calls of destructors are rarely needed. One use of such calls is for objects placed at specific "
        "addresses using a placement new-expression. Such use of explicit placement and destruction of objects can be "
        "necessary to cope with dedicated hardware resources and for writing memory management facilities.";
    size_t sizePL = dataPL.length();
    size_t sizeDM = dataDM.length();

    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper testCollectionSchemeManager(
        testPersistency, canIDTranslator, std::make_shared<CheckinSender>( nullptr ) );
    std::vector<std::shared_ptr<ICollectionScheme>> emptyCP;
    ICollectionSchemeListPtr storePL = std::make_shared<ICollectionSchemeListTest>( emptyCP );
    storePL->copyData( reinterpret_cast<const uint8_t *>( dataPL.c_str() ), sizePL );
    IDecoderManifestPtr storeDM = std::make_shared<IDecoderManifestTest>( "DM" );
    storeDM->copyData( reinterpret_cast<const uint8_t *>( dataDM.c_str() ), sizeDM );
    testCollectionSchemeManager.setCollectionSchemeList( storePL );
    testCollectionSchemeManager.setDecoderManifest( storeDM );
    testCollectionSchemeManager.store( DataType::COLLECTION_SCHEME_LIST );
    testCollectionSchemeManager.store( DataType::DECODER_MANIFEST );

    ICollectionSchemeListPtr retrievePL = std::make_shared<ICollectionSchemeListTest>( emptyCP );
    IDecoderManifestPtr retrieveDM = std::make_shared<IDecoderManifestTest>( "DM" );
    testCollectionSchemeManager.setCollectionSchemeList( retrievePL );
    testCollectionSchemeManager.setDecoderManifest( retrieveDM );
    testCollectionSchemeManager.retrieve( DataType::COLLECTION_SCHEME_LIST );
    testCollectionSchemeManager.retrieve( DataType::DECODER_MANIFEST );

    std::vector<uint8_t> orgData = storePL->getData();
    std::vector<uint8_t> retData = retrievePL->getData();
    ASSERT_EQ( orgData, retData );
    orgData = storeDM->getData();
    retData = retrieveDM->getData();
    ASSERT_EQ( orgData, retData );
}

#ifdef FWE_FEATURE_STORE_AND_FORWARD
/** @brief
 * This test validates Store and Forward campaign persistency with the usage of the store/retrieve APIs.
 */
TEST( PersistencyTest, StoreAndForwardPersistency )
{
    auto persistenceRootDir = getTempDir();
    std::shared_ptr<CacheAndPersist> testPersistency =
        std::make_shared<CacheAndPersist>( persistenceRootDir.string(), 4096 );
    ASSERT_TRUE( testPersistency->init() );

    Schemas::CollectionSchemesMsg::CollectionSchemes protoCollectionSchemesMsg;
    auto *collectionSchemeTestMessage = protoCollectionSchemesMsg.add_collection_schemes();
    collectionSchemeTestMessage->set_campaign_sync_id( "arn:aws:iam::2.23606797749:user/Development/product_1235/*" );
    collectionSchemeTestMessage->set_decoder_manifest_sync_id( "model_manifest_13" );
    collectionSchemeTestMessage->set_start_time_ms_epoch( 162144816000 );
    collectionSchemeTestMessage->set_expiry_time_ms_epoch( 262144816000 );

    // Create an Event/Condition Based CollectionScheme
    Schemas::CollectionSchemesMsg::ConditionBasedCollectionScheme *message =
        collectionSchemeTestMessage->mutable_condition_based_collection_scheme();
    message->set_condition_minimum_interval_ms( 650 );
    message->set_condition_language_version( 20 );
    message->set_condition_trigger_mode(
        Schemas::CollectionSchemesMsg::ConditionBasedCollectionScheme_ConditionTriggerMode_TRIGGER_ALWAYS );

    // Create store and forward configuration
    auto *store_and_forward_configuration = collectionSchemeTestMessage->mutable_store_and_forward_configuration();
    auto *store_and_forward_entry = store_and_forward_configuration->add_partition_configuration();
    auto *storage_options = store_and_forward_entry->mutable_storage_options();
    auto *upload_options = store_and_forward_entry->mutable_upload_options();
    storage_options->set_maximum_size_in_bytes( 1000000 );
    storage_options->set_storage_location( "/storage" );
    storage_options->set_minimum_time_to_live_in_seconds( 1000000 );

    //  Build the AST Tree:
    //----------

    upload_options->mutable_condition_tree()->set_node_signal_id( 10 );
    message->mutable_condition_tree();

    //----------

    collectionSchemeTestMessage->set_after_duration_ms( 0 );
    collectionSchemeTestMessage->set_include_active_dtcs( true );
    collectionSchemeTestMessage->set_persist_all_collected_data( true );
    collectionSchemeTestMessage->set_compress_collected_data( true );
    collectionSchemeTestMessage->set_priority( 5 );

    // Add 3 Signals
    Schemas::CollectionSchemesMsg::SignalInformation *signal1 = collectionSchemeTestMessage->add_signal_information();
    signal1->set_signal_id( 19 );
    signal1->set_sample_buffer_size( 5 );
    signal1->set_minimum_sample_period_ms( 500 );
    signal1->set_fixed_window_period_ms( 600 );
    signal1->set_condition_only_signal( true );
    signal1->set_data_partition_id( 1 );

    Schemas::CollectionSchemesMsg::SignalInformation *signal2 = collectionSchemeTestMessage->add_signal_information();
    signal2->set_signal_id( 17 );
    signal2->set_sample_buffer_size( 10000 );
    signal2->set_minimum_sample_period_ms( 1000 );
    signal2->set_fixed_window_period_ms( 1000 );
    signal2->set_condition_only_signal( false );
    signal2->set_data_partition_id( 1 );

    Schemas::CollectionSchemesMsg::SignalInformation *signal3 = collectionSchemeTestMessage->add_signal_information();
    signal3->set_signal_id( 3 );
    signal3->set_sample_buffer_size( 1000 );
    signal3->set_minimum_sample_period_ms( 100 );
    signal3->set_fixed_window_period_ms( 100 );
    signal3->set_condition_only_signal( true );
    signal3->set_data_partition_id( 1 );

    std::string protoMessage;
    ASSERT_TRUE( protoCollectionSchemesMsg.SerializeToString( &protoMessage ) );
    size_t sizePL = protoMessage.length();
    std::string dataPL = protoMessage;

    CANInterfaceIDTranslator canIDTranslator;
    CollectionSchemeManagerWrapper testCollectionSchemeManager(
        nullptr, canIDTranslator, std::make_shared<CheckinSender>( nullptr ) );
    testCollectionSchemeManager.setCollectionSchemePersistency( testPersistency );
    std::vector<std::shared_ptr<ICollectionScheme>> emptyCP;
    auto storePL = std::make_shared<ICollectionSchemeListTest>( emptyCP );
    storePL->copyData( reinterpret_cast<const uint8_t *>( dataPL.c_str() ), sizePL );
    testCollectionSchemeManager.setCollectionSchemeList( storePL );
    testCollectionSchemeManager.store( DataType::COLLECTION_SCHEME_LIST );

    auto retrievePL = std::make_shared<ICollectionSchemeListTest>( emptyCP );
    testCollectionSchemeManager.setCollectionSchemeList( retrievePL );
    testCollectionSchemeManager.retrieve( DataType::COLLECTION_SCHEME_LIST );

    std::vector<uint8_t> orgData = storePL->getData();
    std::vector<uint8_t> retData = retrievePL->getData();
    ASSERT_EQ( orgData, retData );
}
#endif

} // namespace IoTFleetWise
} // namespace Aws
