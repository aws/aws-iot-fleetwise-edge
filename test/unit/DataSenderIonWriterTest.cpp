// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataSenderIonWriter.h"
#include "CollectionInspectionAPITypes.h"
#include "IDecoderDictionary.h"
#include "MessageTypes.h"
#include "RawDataBufferManagerSpy.h"
#include "RawDataManager.h"
#include "SignalTypes.h"
#include "StreambufBuilder.h"
#include "VehicleDataSourceTypes.h"
#include <boost/optional/optional.hpp>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <sys/resource.h>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::NiceMock;

class DataSenderIonWriterTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        dictionary = std::make_shared<ComplexDataDecoderDictionary>();
        dictionary->complexMessageDecoderMethod["ros2"]["SignalInfoFor1234Name:TypeEncoded"].mSignalId = 1234;

        triggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();
        triggeredCollectionSchemeData->metadata.decoderID = "TESTDECODERID";
        triggeredCollectionSchemeData->metadata.collectionSchemeID = "TESTCOLLECTIONSCHEME";
        triggeredCollectionSchemeData->triggerTime = 1000000;
        triggeredCollectionSchemeData->eventID = 579;

        bufferManager = std::make_shared<NiceMock<Testing::RawDataBufferManagerSpy>>(
            RawData::BufferManagerConfig::create().get() );

        signalConfig.typeId = 1234;

        bufferManager->updateConfig( { { signalConfig.typeId, signalConfig } } );

        ionWriter = std::make_unique<DataSenderIonWriter>( bufferManager, "DemoVehicle" );
        ionWriter->onChangeOfActiveDictionary( dictionary, VehicleDataSourceProtocol::COMPLEX_DATA );
        ionWriter->setupVehicleData( triggeredCollectionSchemeData );
    }

    void
    TearDown() override
    {
    }

    std::shared_ptr<ComplexDataDecoderDictionary> dictionary;
    std::shared_ptr<TriggeredCollectionSchemeData> triggeredCollectionSchemeData;
    std::shared_ptr<NiceMock<Testing::RawDataBufferManagerSpy>> bufferManager;
    RawData::SignalUpdateConfig signalConfig;
    std::unique_ptr<DataSenderIonWriter> ionWriter;
};

TEST_F( DataSenderIonWriterTest, StreamOutputIonAfterSeekTheSame )
{
    std::vector<uint8_t> tmpData;
    tmpData.resize( 100, 0xDE );
    auto handle = bufferManager->push( &tmpData[0], tmpData.size(), 3000000, signalConfig.typeId );

    for ( int i = 0; i < 3; i++ )
    {
        ionWriter->append( CollectedSignal(
            static_cast<SignalID>( signalConfig.typeId ), 3000000 + i, handle, SignalType::RAW_DATA_BUFFER_HANDLE ) );
    }
    auto estimatedSize = ionWriter->getEstimatedSizeInBytes();
    auto stream = ionWriter->getStreambufBuilder()->build();

    std::ostringstream stringStream;
    stringStream << &( *stream );
    std::string firstIterationString = stringStream.str();

    EXPECT_THAT( firstIterationString, HasSubstr( "collection_event_time" ) );
    EXPECT_THAT( firstIterationString, HasSubstr( "signal_name" ) );
    EXPECT_THAT( firstIterationString, HasSubstr( triggeredCollectionSchemeData->metadata.collectionSchemeID ) );
    EXPECT_THAT( firstIterationString, HasSubstr( "SignalInfoFor1234Name" ) );

    // for better analysis create file
    {
        std::ofstream ostm( "TmpTestStreamOutputIonAfterSeekTheSameIteration1.i10", std::ios::binary );
        ostm << firstIterationString;
    }

    // Seek to beginning
    stream->pubseekoff( std::streambuf::off_type( 0 ), std::ios_base::beg, std::ios_base::in );

    std::ostringstream stringStream2;
    stringStream2 << &( *stream );
    std::string secondIterationString = stringStream2.str();

    // for better analysis create file
    {
        std::ofstream ostm( "TmpTestStreamOutputIonAfterSeekTheSameIteration2.i10", std::ios::binary );
        ostm << secondIterationString;
    }

    ASSERT_EQ( firstIterationString, secondIterationString );

    ASSERT_GT( secondIterationString.size(), 300 );

    ASSERT_GT( ( secondIterationString.size() * 2 ), estimatedSize );

    ASSERT_LT( ( secondIterationString.size() / 2 ), estimatedSize );
}

TEST_F( DataSenderIonWriterTest, StreamOutputIonAfterAbsoluteSeek )
{
    std::vector<uint8_t> tmpData;
    tmpData.resize( 100, 0xDE );
    auto handle = bufferManager->push( &tmpData[0], tmpData.size(), 3000000, signalConfig.typeId );

    for ( int i = 0; i < 3; i++ )
    {
        ionWriter->append( CollectedSignal(
            static_cast<SignalID>( signalConfig.typeId ), 3000000 + i, handle, SignalType::RAW_DATA_BUFFER_HANDLE ) );
    }
    auto estimatedSize = ionWriter->getEstimatedSizeInBytes();
    auto stream = ionWriter->getStreambufBuilder()->build();

    std::ostringstream stringStream;
    stringStream << &( *stream );
    std::string firstIterationString = stringStream.str();
    auto totalSize = firstIterationString.size();

    EXPECT_THAT( firstIterationString, HasSubstr( "collection_event_time" ) );
    EXPECT_THAT( firstIterationString, HasSubstr( "signal_name" ) );
    EXPECT_THAT( firstIterationString, HasSubstr( triggeredCollectionSchemeData->metadata.collectionSchemeID ) );
    EXPECT_THAT( firstIterationString, HasSubstr( "SignalInfoFor1234Name" ) );

    // for better analysis create file
    {
        std::ofstream ostm( "TmpTestStreamOutputIonAfterAbsoluteSeekIteration1.i10", std::ios::binary );
        ostm << firstIterationString;
    }

    std::string secondIterationString;
    secondIterationString.resize( totalSize );

    // Seek to beginning and read the first half
    ASSERT_EQ( stream->pubseekoff( std::streambuf::off_type( 0 ), std::ios_base::beg, std::ios_base::in ), 0 );
    stream->sgetn( &secondIterationString[0], totalSize / 2 );

    // Seek to middle and read the second half
    ASSERT_EQ( stream->pubseekpos( totalSize / 2, std::ios_base::in ), totalSize / 2 );
    stream->sgetn( &secondIterationString[totalSize / 2], totalSize - ( totalSize / 2 ) );

    // for better analysis create file
    {
        std::ofstream ostm( "TmpTestStreamOutputIonAfterAbsoluteSeekIteration2.i10", std::ios::binary );
        ostm << secondIterationString;
    }

    ASSERT_EQ( firstIterationString, secondIterationString );

    ASSERT_GT( secondIterationString.size(), 300 );

    ASSERT_GT( ( secondIterationString.size() * 2 ), estimatedSize );

    ASSERT_LT( ( secondIterationString.size() / 2 ), estimatedSize );

    std::string thirdIterationString;
    thirdIterationString.resize( totalSize );

    // Seek to beginning and read the first half
    ASSERT_EQ( stream->pubseekoff( std::streambuf::off_type( 0 ), std::ios_base::beg, std::ios_base::in ), 0 );
    stream->sgetn( &thirdIterationString[0], totalSize / 2 );

    // Seek to middle and read the second half
    ASSERT_EQ( stream->pubseekpos( totalSize / 2, std::ios_base::in ), totalSize / 2 );
    stream->sgetn( &thirdIterationString[totalSize / 2], totalSize - ( totalSize / 2 ) );

    // Seek to middle again and re-read the second half. While jumping back is not an efficient way
    // to read our stream, we need to make sure that it generates the data again the same way as if
    // used seekoff to the beginning.
    ASSERT_EQ( stream->pubseekpos( totalSize / 2, std::ios_base::in ), totalSize / 2 );
    stream->sgetn( &thirdIterationString[totalSize / 2], totalSize - ( totalSize / 2 ) );

    // for better analysis create file
    {
        std::ofstream ostm( "TmpTestStreamOutputIonAfterAbsoluteSeekIteration3.i10", std::ios::binary );
        ostm << thirdIterationString;
    }

    ASSERT_EQ( firstIterationString, thirdIterationString );

    ASSERT_GT( thirdIterationString.size(), 300 );

    ASSERT_GT( ( thirdIterationString.size() * 2 ), estimatedSize );

    ASSERT_LT( ( thirdIterationString.size() / 2 ), estimatedSize );
}

/**
 * This test creates 1GB output file, but consumes less than 1GB RAM
 */

TEST_F( DataSenderIonWriterTest, StreamOutputHugeIon )
{
    struct rusage usage;
    getrusage( RUSAGE_SELF, &usage );
    auto startKB = usage.ru_maxrss;

    std::vector<uint8_t> tmpData;
    tmpData.resize( 1024 * 1024, 0xDE );
    auto handle = bufferManager->push( &tmpData[0], tmpData.size(), 16435345, signalConfig.typeId );

    for ( int i = 0; i < 1000; i++ )
    {
        ionWriter->append( CollectedSignal(
            static_cast<SignalID>( signalConfig.typeId ), 33444444, handle, SignalType::RAW_DATA_BUFFER_HANDLE ) );
    }
    auto stream = ionWriter->getStreambufBuilder()->build();
    {
        std::ofstream ostm( "TmpTestStreamOutputHugeIon.i10", std::ios::binary );
        ostm << &( *stream );
    }

    getrusage( RUSAGE_SELF, &usage );
    auto endKB = usage.ru_maxrss;
    auto usageIncreaseKB = endKB - startKB;
    printf( "RAM usage increase of %ld KB\n", usageIncreaseKB );
    ASSERT_GT( endKB, 0 );
    ASSERT_LT( usageIncreaseKB, 100 * 1024 ); // not more than 100MB RAM increase
}

TEST_F( DataSenderIonWriterTest, StreamTellgStream )
{
    std::vector<uint8_t> tmpData;
    tmpData.resize( 100, 0xDE );
    auto handle = bufferManager->push( &tmpData[0], tmpData.size(), 3000000, signalConfig.typeId );

    for ( int i = 0; i < 3; i++ )
    {
        ionWriter->append( CollectedSignal(
            static_cast<SignalID>( signalConfig.typeId ), 3000000 + i, handle, SignalType::RAW_DATA_BUFFER_HANDLE ) );
    }

    auto stream = ionWriter->getStreambufBuilder()->build();

    std::istream getSizeIstream( stream.get() );
    getSizeIstream.seekg( 0, getSizeIstream.end );
    auto size = getSizeIstream.tellg();

    // should not have any effect as after tellg should be already at the end
    getSizeIstream.seekg( std::streamoff( size ), getSizeIstream.beg );

    getSizeIstream.seekg( 0, getSizeIstream.beg );

    stream->pubseekoff( std::streambuf::off_type( 0 ), std::ios_base::beg, std::ios_base::in );

    std::ostringstream stringStream( std::stringstream::binary );
    stringStream << &( *stream );
    std::string firstIterationString = stringStream.str();

    EXPECT_EQ( size, firstIterationString.size() );

    // for better analysis create file
    {
        std::ofstream ostm( "TellGTest.i10", std::ios::binary );
        ostm << firstIterationString;
    }

    // Seek to beginning
    stream->pubseekoff( std::streambuf::off_type( 0 ), std::ios_base::beg, std::ios_base::in );

    std::ostringstream stringStream2( std::stringstream::binary );
    stringStream2 << &( *stream );
    std::string secondIterationString = stringStream2.str();

    ASSERT_EQ( firstIterationString, secondIterationString );

    ASSERT_GT( secondIterationString.size(), 300 );
}

TEST_F( DataSenderIonWriterTest, ReturnNullStreamWhenAllDataIsDeleted )
{
    std::vector<uint8_t> tmpData;
    tmpData.resize( 100, 0xDE );
    auto handle1 = bufferManager->push( &tmpData[0], tmpData.size(), 3000000, signalConfig.typeId );
    auto handle2 = bufferManager->push( &tmpData[0], tmpData.size(), 3000001, signalConfig.typeId );
    ASSERT_TRUE( bufferManager->increaseHandleUsageHint(
        signalConfig.typeId,
        handle1,
        RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_SELECTED_FOR_UPLOAD ) );
    ASSERT_TRUE( bufferManager->increaseHandleUsageHint(
        signalConfig.typeId,
        handle2,
        RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_SELECTED_FOR_UPLOAD ) );

    ionWriter->append( CollectedSignal(
        static_cast<SignalID>( signalConfig.typeId ), 3000000, handle1, SignalType::RAW_DATA_BUFFER_HANDLE ) );
    ionWriter->append( CollectedSignal(
        static_cast<SignalID>( signalConfig.typeId ), 3000001, handle2, SignalType::RAW_DATA_BUFFER_HANDLE ) );

    // Before we get the stream, make the buffer manager delete the data
    ASSERT_TRUE( bufferManager->decreaseHandleUsageHint(
        signalConfig.typeId, handle1, RawData::BufferHandleUsageStage::HANDED_OVER_TO_SENDER ) );
    ASSERT_TRUE( bufferManager->decreaseHandleUsageHint(
        signalConfig.typeId, handle2, RawData::BufferHandleUsageStage::HANDED_OVER_TO_SENDER ) );
    auto stream = ionWriter->getStreambufBuilder()->build();

    // Since all data associated to the stream was deleted, the stream should be null
    ASSERT_EQ( stream, nullptr );
}

TEST_F( DataSenderIonWriterTest, HandleUsageIsUpdatedWhenStreamIsCreated )
{
    std::vector<uint8_t> tmpData;
    tmpData.resize( 100, 0xDE );
    auto handle = bufferManager->push( &tmpData[0], tmpData.size(), 3000000, signalConfig.typeId );
    ASSERT_TRUE( bufferManager->increaseHandleUsageHint(
        signalConfig.typeId,
        handle,
        RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_SELECTED_FOR_UPLOAD ) );

    // From now on we want to strictly check the handle usage updates, so don't allow any call that
    // doesn't match exactly.
    EXPECT_CALL( *bufferManager, mockedIncreaseHandleUsageHint( _, _, _ ) ).Times( 0 );
    EXPECT_CALL( *bufferManager, mockedDecreaseHandleUsageHint( _, _, _ ) ).Times( 0 );

    // Add the data, which should make the previous usage stage to be cleared
    {
        InSequence seq;
        EXPECT_CALL( *bufferManager,
                     mockedIncreaseHandleUsageHint(
                         signalConfig.typeId, handle, RawData::BufferHandleUsageStage::HANDED_OVER_TO_SENDER ) )
            .Times( 1 );
        EXPECT_CALL( *bufferManager,
                     mockedDecreaseHandleUsageHint(
                         signalConfig.typeId,
                         handle,
                         RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_SELECTED_FOR_UPLOAD ) )
            .Times( 1 );
    }
    ionWriter->append( CollectedSignal(
        static_cast<SignalID>( signalConfig.typeId ), 3000000, handle, SignalType::RAW_DATA_BUFFER_HANDLE ) );

    // Now then the stream is really created the usage should be set as uploading, which should prevent data
    // to be deleted.
    {
        InSequence seq;
        EXPECT_CALL(
            *bufferManager,
            mockedIncreaseHandleUsageHint( signalConfig.typeId, handle, RawData::BufferHandleUsageStage::UPLOADING ) )
            .Times( 1 );
        EXPECT_CALL( *bufferManager,
                     mockedDecreaseHandleUsageHint(
                         signalConfig.typeId, handle, RawData::BufferHandleUsageStage::HANDED_OVER_TO_SENDER ) )
            .Times( 1 );
    }
    auto stream = ionWriter->getStreambufBuilder()->build();

    // On stream destruction the usage stage should be cleared
    EXPECT_CALL(
        *bufferManager,
        mockedDecreaseHandleUsageHint( signalConfig.typeId, handle, RawData::BufferHandleUsageStage::UPLOADING ) )
        .Times( 1 );
}

} // namespace IoTFleetWise
} // namespace Aws
