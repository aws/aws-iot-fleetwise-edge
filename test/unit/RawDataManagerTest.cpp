// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RawDataManager.h"
#include "SignalTypes.h"
#include "Testing.h"
#include "TimeTypes.h"
#include <algorithm>
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <iterator>
#include <numeric>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

constexpr size_t maxOverallMemory = 1_GiB;

class RawDataManagerTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        signalUpdateConfig1.typeId = 101;
        signalUpdateConfig1.interfaceId = "interface1";
        signalUpdateConfig1.messageId = "ImageTopic:sensor_msgs/msg/Image";
        signalOverrides1.interfaceId = signalUpdateConfig1.interfaceId;
        signalOverrides1.messageId = signalUpdateConfig1.messageId;
        signalOverrides1.maxNumOfSamples = 20;
        signalOverrides1.maxBytesPerSample = 5_MiB;
        signalOverrides1.reservedBytes = 5_MiB;
        signalOverrides1.maxBytes = 100_MiB;

        signalUpdateConfig2.typeId = 201;
        signalUpdateConfig2.interfaceId = "interface1";
        signalUpdateConfig2.messageId = "PointFieldTopic:sensor_msgs/msg/PointField";
        signalOverrides2.interfaceId = signalUpdateConfig2.interfaceId;
        signalOverrides2.messageId = signalUpdateConfig2.messageId;
        signalOverrides2.maxNumOfSamples = 20;
        signalOverrides2.maxBytesPerSample = 5_MiB;
        signalOverrides2.reservedBytes = 1_MiB;
        signalOverrides2.maxBytes = 100_MiB;

        signalUpdateConfig3.typeId = 301;
        signalUpdateConfig3.interfaceId = "interface2";
        signalUpdateConfig3.messageId = "ImageTopic:sensor_msgs/msg/Image";
        signalOverrides3.interfaceId = signalUpdateConfig3.interfaceId;
        signalOverrides3.messageId = signalUpdateConfig3.messageId;
        signalOverrides3.maxNumOfSamples = 20;
        signalOverrides3.maxBytesPerSample = 1_MiB;
        signalOverrides3.reservedBytes = 3_MiB;
        signalOverrides3.maxBytes = 100_MiB;

        overridesPerSignal = { signalOverrides1, signalOverrides2, signalOverrides3 };
        bufferManagerConfig = RawData::BufferManagerConfig::create( maxOverallMemory,
                                                                    boost::none,
                                                                    boost::make_optional( (size_t)20 ),
                                                                    boost::none,
                                                                    boost::none,
                                                                    overridesPerSignal );
        ASSERT_TRUE( bufferManagerConfig.has_value() );

        updatedSignals = { { signalUpdateConfig1.typeId, signalUpdateConfig1 },
                           { signalUpdateConfig2.typeId, signalUpdateConfig2 },
                           { signalUpdateConfig3.typeId, signalUpdateConfig3 } };
    }

    void
    TearDown() override
    {
    }

    RawData::SignalUpdateConfig signalUpdateConfig1;
    RawData::SignalUpdateConfig signalUpdateConfig2;
    RawData::SignalUpdateConfig signalUpdateConfig3;
    RawData::SignalBufferOverrides signalOverrides1;
    RawData::SignalBufferOverrides signalOverrides2;
    RawData::SignalBufferOverrides signalOverrides3;
    std::vector<RawData::SignalBufferOverrides> overridesPerSignal;
    boost::optional<RawData::BufferManagerConfig> bufferManagerConfig;
    std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> updatedSignals;
};

TEST_F( RawDataManagerTest, updateConfig )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );
}

TEST_F( RawDataManagerTest, updateConfigFailsWithMemoryLowerThanMinimumRequiredBySignals )
{
    // Remove the max bytes limit since they can't be larger than the overall max bytes limit
    overridesPerSignal[0].maxBytes = boost::none;
    overridesPerSignal[1].maxBytes = boost::none;
    overridesPerSignal[2].maxBytes = boost::none;
    size_t requiredMemory = signalOverrides1.reservedBytes.get() + signalOverrides2.reservedBytes.get() +
                            signalOverrides3.reservedBytes.get();
    bufferManagerConfig = RawData::BufferManagerConfig::create( requiredMemory - 1,
                                                                boost::none,
                                                                boost::make_optional( (size_t)20 ),
                                                                boost::none,
                                                                boost::none,
                                                                overridesPerSignal );
    ASSERT_TRUE( bufferManagerConfig.has_value() );

    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::OUTOFMEMORY );
}

TEST_F( RawDataManagerTest, updateConfigSucceedsWithMemoryEqualToMinimumRequiredBySignals )
{
    // Remove the max bytes limit since they can't be larger than the overall max bytes limit
    overridesPerSignal[0].maxBytes = boost::none;
    overridesPerSignal[1].maxBytes = boost::none;
    overridesPerSignal[2].maxBytes = boost::none;
    size_t requiredMemory = signalOverrides1.reservedBytes.get() + signalOverrides2.reservedBytes.get() +
                            signalOverrides3.reservedBytes.get();
    bufferManagerConfig = RawData::BufferManagerConfig::create(
        requiredMemory, boost::none, boost::make_optional( (size_t)20 ), boost::none, boost::none, overridesPerSignal );
    ASSERT_TRUE( bufferManagerConfig.has_value() );

    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );
}

TEST_F( RawDataManagerTest, increaseHandleUsageWithInvalidHandle )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );

    ASSERT_FALSE(
        rawDataBufferManager.increaseHandleUsageHint( 1, 2, RawData::BufferHandleUsageStage::HANDED_OVER_TO_SENDER ) );
}

TEST_F( RawDataManagerTest, increaseHandleUsageWithInvalidStage )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;

    std::vector<RawData::BufferHandle> handles;
    RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    handles.push_back( rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 ) );

    ASSERT_FALSE( rawDataBufferManager.increaseHandleUsageHint(
        typeId1, handles[0], RawData::BufferHandleUsageStage::STAGE_SIZE ) );
}

TEST_F( RawDataManagerTest, decreaseHandleUsageWithInvalidHandle )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );

    ASSERT_FALSE(
        rawDataBufferManager.decreaseHandleUsageHint( 1, 2, RawData::BufferHandleUsageStage::HANDED_OVER_TO_SENDER ) );
}

TEST_F( RawDataManagerTest, decreaseHandleUsageWithInvalidStage )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;

    std::vector<RawData::BufferHandle> handles;
    RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    handles.push_back( rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 ) );

    ASSERT_FALSE( rawDataBufferManager.decreaseHandleUsageHint(
        typeId1, handles[0], RawData::BufferHandleUsageStage::STAGE_SIZE ) );
}

TEST_F( RawDataManagerTest, unusedTypesFreedWhenConfigIsUpdated )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;

    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 3 );

    // Add some data and leave it as unused
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 500 );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp, typeId1 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
    }

    // Update the config without the first signal
    ASSERT_EQ( updatedSignals.erase( signalUpdateConfig1.typeId ), 1 );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 2 );

    // Now this should be rejected because the signal was removed from the buffer manager
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 1 );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp, typeId1 );
        ASSERT_EQ( handle, RawData::INVALID_BUFFER_HANDLE );
    }
}

TEST_F( RawDataManagerTest, usedTypesAreFreedAfterTheLastFrameIsReturned )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;
    std::vector<RawData::LoanedFrame> loanedFrames;

    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 3 );

    // Add some data and hold it so it can't be deleted
    RawData::RawDataType rawData = std::vector<uint8_t>( 500 );
    auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp, typeId1 );
    ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
    loanedFrames.push_back( rawDataBufferManager.borrowFrame( typeId1, handle ) );

    // Update the config without the first signal. This shouldn't delete the buffer though as there
    // is still data being used.
    ASSERT_EQ( updatedSignals.erase( signalUpdateConfig1.typeId ), 1 );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 3 );

    // Adding new data should be rejected
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 1 );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp, typeId1 );
        ASSERT_EQ( handle, RawData::INVALID_BUFFER_HANDLE );
    }
    // But the existing data should still be available
    ASSERT_FALSE( rawDataBufferManager.borrowFrame( typeId1, handle ).isNull() );

    // When the last frame is returned, the buffer should be deleted
    loanedFrames.pop_back();
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 2 );
}

TEST_F( RawDataManagerTest, bufferMarkedAsDeletingShouldBeKeptWhenTheSignalIsAddedAgain )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;
    std::vector<RawData::LoanedFrame> loanedFrames;

    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 3 );

    // Add some data and hold it so it can't be deleted
    RawData::RawDataType rawData = std::vector<uint8_t>( 500 );
    auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp, typeId1 );
    ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
    loanedFrames.push_back( rawDataBufferManager.borrowFrame( typeId1, handle ) );

    // Update the config without the first signal. This shouldn't delete the buffer though as there
    // is still data being used.
    ASSERT_EQ( updatedSignals.erase( signalUpdateConfig1.typeId ), 1 );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 3 );

    // Now add the first signal back
    updatedSignals[signalUpdateConfig1.typeId] = signalUpdateConfig1;
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 3 );

    // When the last frame is returned, the buffer should not be deleted since we are collecting this signal again
    loanedFrames.pop_back();
    ASSERT_EQ( rawDataBufferManager.getActiveBuffers(), 3 );
}

TEST_F( RawDataManagerTest, dataStorage )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;
    RawData::RawDataType rawDataTest1 = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

    // Push the data for signalID which is not requested to be collected
    uint32_t typeIdInvalid = 555;
    auto inHandle = rawDataBufferManager.push( &rawDataTest1.front(), rawDataTest1.size(), timestamp, typeIdInvalid );
    ASSERT_EQ( inHandle, RawData::INVALID_BUFFER_HANDLE );

    auto handle = rawDataBufferManager.push( &rawDataTest1.front(), rawDataTest1.size(), timestamp, typeId1 );
    ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );

    // Push the data for which exceed memory per sample limit
    RawData::RawDataType rawDataTest3( 10000001 );
    uint32_t typeId3 = 301;                                               // MaxSampleSize is 1 MB
    std::iota( std::begin( rawDataTest3 ), std::end( rawDataTest3 ), 0 ); // 1.001 MB
    ASSERT_EQ( rawDataBufferManager.push( &rawDataTest3.front(), 10000001, timestamp, typeId3 ),
               RawData::INVALID_BUFFER_HANDLE );
}

TEST_F( RawDataManagerTest, overwriteOlderUnusedData )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;

    std::vector<RawData::BufferHandle> handles;
    RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    handles.push_back( rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 ) );

    auto firstRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, handles[0] );
    ASSERT_FALSE( firstRawDataFrame.isNull() );

    // Now add enough samples so that they start overwriting the previous ones
    for ( size_t i = 0; i < signalOverrides1.maxNumOfSamples.get(); i++ )
    {
        RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        handles.push_back( handle );
    }

    // First data should still be available since it was in use.
    ASSERT_FALSE( firstRawDataFrame.isNull() );
    // Getting another reference to the same data should also work
    auto firstRawDataFrame2 = rawDataBufferManager.borrowFrame( typeId1, handles[0] );
    ASSERT_FALSE( firstRawDataFrame2.isNull() );

    // The second data is the one that should have been overwritten
    auto overwrittenRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, handles[1] );
    ASSERT_TRUE( overwrittenRawDataFrame.isNull() );

    auto thirdRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, handles[2] );
    ASSERT_FALSE( thirdRawDataFrame.isNull() );

    auto lastRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, handles[handles.size() - 1] );
    ASSERT_FALSE( lastRawDataFrame.isNull() );
}

TEST_F( RawDataManagerTest, deleteDataWhenHintsAreReset )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;

    std::vector<RawData::BufferHandle> firstBatchHandles;

    // Fill the buffer with data
    for ( size_t i = 0; i < signalOverrides1.maxNumOfSamples.get(); i++ )
    {
        RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        firstBatchHandles.push_back( handle );
        ASSERT_TRUE( rawDataBufferManager.increaseHandleUsageHint(
            typeId1, handle, RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_HISTORY_BUFFER ) );
    }

    // For the first handle we also increase the hint for another stage
    ASSERT_TRUE( rawDataBufferManager.increaseHandleUsageHint(
        typeId1, firstBatchHandles[0], RawData::BufferHandleUsageStage::HANDED_OVER_TO_SENDER ) );

    // Resetting the hints for this stage should delete all data that was associated to it, except the first one
    // that is also used in another stage.
    rawDataBufferManager.resetUsageHintsForStage(
        RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_HISTORY_BUFFER );

    auto firstRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, firstBatchHandles[0] );
    ASSERT_FALSE( firstRawDataFrame.isNull() );

    firstBatchHandles.erase( firstBatchHandles.begin() );
    for ( auto handle : firstBatchHandles )
    {
        auto rawDataFrame = rawDataBufferManager.borrowFrame( typeId1, handle );
        ASSERT_TRUE( rawDataFrame.isNull() );
    }
}

TEST_F( RawDataManagerTest, overwriteOlderUnusedDataWhoseHandleIsInUse )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;

    std::vector<RawData::BufferHandle> handles;

    // Fill the buffer with data and hold all handles
    for ( size_t i = 0; i < signalOverrides1.maxNumOfSamples.get(); i++ )
    {
        RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        ASSERT_TRUE( rawDataBufferManager.increaseHandleUsageHint(
            typeId1, handle, RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_HISTORY_BUFFER ) );
        handles.push_back( handle );
    }

    // Now add a new data to the full buffer
    RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    auto lastHandle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
    ASSERT_NE( lastHandle, RawData::INVALID_BUFFER_HANDLE );

    // It should have overwritten the first data. Even though its handle was in use, its data was not.
    auto firstRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, handles[0] );
    ASSERT_TRUE( firstRawDataFrame.isNull() );
}

TEST_F( RawDataManagerTest, overwriteOlderUnusedDataWhoseHandleIsNotInUploadingStage )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;

    std::vector<RawData::BufferHandle> handles;
    RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    handles.push_back( rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 ) );
    // Indicate that the second handle is being uploaded. This should prevent the data from being
    // released the same way as borrowing the raw data frame.
    ASSERT_TRUE( rawDataBufferManager.increaseHandleUsageHint(
        typeId1, handles[0], RawData::BufferHandleUsageStage::UPLOADING ) );

    // Fill the buffer with data and hold all handles
    auto numberOfFreeSamplesAvailable = signalOverrides1.maxNumOfSamples.get() - handles.size();
    for ( size_t i = 0; i < numberOfFreeSamplesAvailable; i++ )
    {
        RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        ASSERT_TRUE( rawDataBufferManager.increaseHandleUsageHint(
            typeId1, handle, RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_HISTORY_BUFFER ) );
        handles.push_back( handle );
    }

    // Now add a new data to the full buffer
    auto lastHandle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
    ASSERT_NE( lastHandle, RawData::INVALID_BUFFER_HANDLE );

    // It should have not overwritten the first data because its handle is in the UPLOADING stage.
    auto firstRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, handles[0] );
    ASSERT_FALSE( firstRawDataFrame.isNull() );
    // It should have overwritten the second data. Even though its handle was in use, its data was not.
    auto secondRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, handles[1] );
    ASSERT_TRUE( secondRawDataFrame.isNull() );
}

TEST_F( RawDataManagerTest, overwriteFirstOlderUnusedDataThenDataWhoseHandleIsInUse )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;

    std::vector<RawData::BufferHandle> firstBatchHandles;

    // Fill the buffer with data
    for ( size_t i = 0; i < signalOverrides1.maxNumOfSamples.get(); i++ )
    {
        RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        firstBatchHandles.push_back( handle );
    }

    // Hold only the handles with an even index
    for ( size_t i = 0; i < signalOverrides1.maxNumOfSamples.get(); i += 2 )
    {
        ASSERT_TRUE( rawDataBufferManager.increaseHandleUsageHint(
            typeId1,
            firstBatchHandles[i],
            RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_HISTORY_BUFFER ) );
    }

    // Add enough data so that only the unused data with unused handles get overwritten
    std::vector<RawData::BufferHandle> secondBatchHandles;
    for ( size_t i = 1; i < signalOverrides1.maxNumOfSamples.get(); i += 2 )
    {
        RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        secondBatchHandles.push_back( handle );
    }

    // All data with an odd index should haven been overwritten since neither its handle nor its data
    // were in use.
    for ( size_t i = 1; i < signalOverrides1.maxNumOfSamples.get(); i += 2 )
    {
        auto rawDataFrame = rawDataBufferManager.borrowFrame( typeId1, firstBatchHandles[i] );
        ASSERT_TRUE( rawDataFrame.isNull() );
    }

    // All data with an even index should still be available, since their handle was in use and the
    // manager should try to keep them if possible.
    for ( size_t i = 0; i < signalOverrides1.maxNumOfSamples.get(); i += 2 )
    {
        auto rawDataFrame = rawDataBufferManager.borrowFrame( typeId1, firstBatchHandles[i] );
        ASSERT_FALSE( rawDataFrame.isNull() );
    }

    // Hold all the handles of the newly added data. This should make all current handles be in use.
    for ( auto handle : secondBatchHandles )
    {
        ASSERT_TRUE( rawDataBufferManager.increaseHandleUsageHint(
            typeId1, handle, RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_HISTORY_BUFFER ) );
    }

    // Now the buffer should be full, all handles are in use, but no data is in use. The next data we
    // add should then overwrite the oldest data even if its handle is in use.
    RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    auto lastHandle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
    ASSERT_NE( lastHandle, RawData::INVALID_BUFFER_HANDLE );

    auto oldestRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, firstBatchHandles[0] );
    ASSERT_TRUE( oldestRawDataFrame.isNull() );
}

TEST_F( RawDataManagerTest, overwriteUnusedDataAfterTheHandleIsNotUsedAnymore )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;

    std::vector<RawData::BufferHandle> firstBatchHandles;

    // Fill the buffer with data
    for ( size_t i = 0; i < signalOverrides1.maxNumOfSamples.get(); i++ )
    {
        RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        firstBatchHandles.push_back( handle );
    }

    // Hold the first handle
    ASSERT_TRUE( rawDataBufferManager.increaseHandleUsageHint(
        typeId1, firstBatchHandles[0], RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_HISTORY_BUFFER ) );

    // The next data should not overwrite the first one. It should overwrite the second one, whose both
    // data and handle are unused.
    RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    auto lastHandle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
    ASSERT_NE( lastHandle, RawData::INVALID_BUFFER_HANDLE );

    {
        auto firstRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, firstBatchHandles[0] );
        ASSERT_FALSE( firstRawDataFrame.isNull() );
        auto secondRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, firstBatchHandles[1] );
        ASSERT_TRUE( secondRawDataFrame.isNull() );
    }

    // Now if we unset the handle usage, the manager should overwrite the first data because now neither
    // the data nor the handle are in use
    ASSERT_TRUE( rawDataBufferManager.decreaseHandleUsageHint(
        typeId1, firstBatchHandles[0], RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_HISTORY_BUFFER ) );
    rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    lastHandle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
    ASSERT_NE( lastHandle, RawData::INVALID_BUFFER_HANDLE );

    auto firstRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, firstBatchHandles[0] );
    ASSERT_TRUE( firstRawDataFrame.isNull() );
}

TEST_F( RawDataManagerTest, overwriteUnusedDataFromTheSameType )
{
    signalOverrides1.reservedBytes = 50_MiB;
    signalOverrides1.maxNumOfSamples = 100000;
    signalOverrides1.maxBytesPerSample = 5_MiB;
    signalOverrides1.maxBytes = boost::none;

    signalOverrides2.reservedBytes = 5_MiB;
    signalOverrides2.maxNumOfSamples = 100000;
    signalOverrides2.maxBytesPerSample = 5_MiB;
    signalOverrides2.maxBytes = boost::none;

    overridesPerSignal = { signalOverrides1, signalOverrides2 };

    bufferManagerConfig = RawData::BufferManagerConfig::create(
        1000_MiB, boost::none, boost::none, boost::none, boost::none, overridesPerSignal );
    ASSERT_TRUE( bufferManagerConfig.has_value() );
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;
    auto typeId2 = signalUpdateConfig2.typeId;

    std::vector<RawData::BufferHandle> firstBatchHandlesType1;
    std::vector<RawData::BufferHandle> firstBatchHandlesType2;

    // Fill the buffer with data just enough to hit the min bytes per signal
    for ( unsigned int i = 0; i < 10; i++ )
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 5_MiB );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        firstBatchHandlesType1.push_back( handle );
    }
    for ( unsigned int i = 0; i < 5; i++ )
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 1_MiB );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId2 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        firstBatchHandlesType2.push_back( handle );
    }

    std::vector<RawData::BufferHandle> secondBatchHandlesType1;
    std::vector<RawData::BufferHandle> secondBatchHandlesType2;

    // Fill the rest of the buffer with data
    for ( unsigned int i = 0; i < 180; i++ )
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 5_MiB );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        secondBatchHandlesType1.push_back( handle );
    }
    for ( unsigned int i = 0; i < 45; i++ )
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 1_MiB );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId2 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        secondBatchHandlesType2.push_back( handle );
    }

    // Now that the buffer is full, the oldest handle of the signal we are adding should be overwritten,
    // but it should never delete the buffer from the other signals.
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 5_MiB );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
    }
    auto oldestHandle = firstBatchHandlesType1[0];
    firstBatchHandlesType1.erase( firstBatchHandlesType1.begin() );
    {
        auto rawDataFrame = rawDataBufferManager.borrowFrame( typeId1, oldestHandle );
        ASSERT_TRUE( rawDataFrame.isNull() );
    }

    for ( auto handle : firstBatchHandlesType1 )
    {
        auto rawDataFrame = rawDataBufferManager.borrowFrame( typeId1, handle );
        ASSERT_FALSE( rawDataFrame.isNull() );
    }
    for ( auto handle : firstBatchHandlesType2 )
    {
        auto rawDataFrame = rawDataBufferManager.borrowFrame( typeId2, handle );
        ASSERT_FALSE( rawDataFrame.isNull() );
    }
    for ( auto handle : secondBatchHandlesType1 )
    {
        auto rawDataFrame = rawDataBufferManager.borrowFrame( typeId1, handle );
        ASSERT_FALSE( rawDataFrame.isNull() );
    }
    for ( auto handle : secondBatchHandlesType2 )
    {
        auto rawDataFrame = rawDataBufferManager.borrowFrame( typeId2, handle );
        ASSERT_FALSE( rawDataFrame.isNull() );
    }
}

TEST_F( RawDataManagerTest, dontOverwriteUnusedDataFromTheOtheTypes )
{
    signalOverrides1.reservedBytes = 50_MiB;
    signalOverrides1.maxNumOfSamples = 100000;
    signalOverrides1.maxBytesPerSample = 5_MiB;
    signalOverrides1.maxBytes = boost::none;

    signalOverrides2.reservedBytes = 5_MiB;
    signalOverrides2.maxNumOfSamples = 100000;
    signalOverrides2.maxBytesPerSample = 5_MiB;
    signalOverrides2.maxBytes = boost::none;

    overridesPerSignal = { signalOverrides1, signalOverrides2 };

    bufferManagerConfig = RawData::BufferManagerConfig::create(
        1000_MiB, boost::none, boost::none, boost::none, boost::none, overridesPerSignal );
    ASSERT_TRUE( bufferManagerConfig.has_value() );
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;
    auto typeId2 = signalUpdateConfig2.typeId;

    std::vector<RawData::LoanedFrame> loanedFramesType1;
    std::vector<RawData::LoanedFrame> loanedFramesType2;

    // Fill the buffer with data just enough to hit the min bytes per signal
    for ( unsigned int i = 0; i < 10; i++ )
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 5_MiB );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        loanedFramesType1.emplace_back( rawDataBufferManager.borrowFrame( typeId1, handle ) );
        ASSERT_FALSE( loanedFramesType1.back().isNull() );
    }
    for ( unsigned int i = 0; i < 5; i++ )
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 1_MiB );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId2 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        loanedFramesType2.emplace_back( rawDataBufferManager.borrowFrame( typeId2, handle ) );
        ASSERT_FALSE( loanedFramesType2.back().isNull() );
    }

    // Fill the rest of the buffer with data of one typeId1
    for ( unsigned int i = 0; i < 189; i++ )
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 5_MiB );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        loanedFramesType1.emplace_back( rawDataBufferManager.borrowFrame( typeId1, handle ) );
        ASSERT_FALSE( loanedFramesType1.back().isNull() );
    }

    // Now that the buffer is full with typeId1 and typeId2 is already using all its reserved bytes
    // any attempt to add data of typeId2 should fail.
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 1_MiB );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId2 );
        ASSERT_EQ( handle, RawData::INVALID_BUFFER_HANDLE );
    }
}

TEST_F( RawDataManagerTest, reserveMemoryDontAllowASingleTypeToFillTheBuffer )
{
    signalOverrides1.reservedBytes = 50_MiB;
    signalOverrides1.maxNumOfSamples = 100000;
    signalOverrides1.maxBytesPerSample = 5_MiB;
    signalOverrides1.maxBytes = boost::none;

    signalOverrides2.reservedBytes = 5_MiB;
    signalOverrides2.maxNumOfSamples = 100000;
    signalOverrides2.maxBytesPerSample = 5_MiB;
    signalOverrides2.maxBytes = boost::none;

    overridesPerSignal = { signalOverrides1, signalOverrides2 };

    bufferManagerConfig = RawData::BufferManagerConfig::create(
        1000_MiB, boost::none, boost::none, boost::none, boost::none, overridesPerSignal );
    ASSERT_TRUE( bufferManagerConfig.has_value() );
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;
    auto typeId2 = signalUpdateConfig2.typeId;

    std::vector<RawData::BufferHandle> firstBatchHandlesType1;
    std::vector<RawData::BufferHandle> firstBatchHandlesType2;

    std::vector<RawData::LoanedFrame> loanedFramesType1;
    std::vector<RawData::LoanedFrame> loanedFramesType2;

    // Fill the buffer with data just enough to leave the reservedBytes for the other signal
    for ( unsigned int i = 0; i < 199; i++ )
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 5_MiB );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        firstBatchHandlesType1.push_back( handle );
        // Hold all data so that nothing can be deleted
        loanedFramesType1.push_back( rawDataBufferManager.borrowFrame( typeId1, handle ) );
    }
    // Adding more data from the same signal should fail
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 10 );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
        ASSERT_EQ( handle, RawData::INVALID_BUFFER_HANDLE );
    }
    // But adding data from another signal should succeed because we have a reservedBytes for it
    for ( unsigned int i = 0; i < 5; i++ )
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 1_MiB );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId2 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        firstBatchHandlesType2.push_back( handle );
        // Hold all data so that nothing can be deleted
        loanedFramesType2.push_back( rawDataBufferManager.borrowFrame( typeId2, handle ) );
    }
    // Now the buffer is completely full, so any attempt to add more data for the second signal should
    // fail too.
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 10 );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId2 );
        ASSERT_EQ( handle, RawData::INVALID_BUFFER_HANDLE );
    }

    // Now release one frame of first signal, which should allow us to add more data for the second
    // signal
    loanedFramesType1.pop_back();
    {
        RawData::RawDataType rawData = std::vector<uint8_t>( 10 );
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId2 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
    }
}

TEST_F( RawDataManagerTest, failToAddNewDataWhenNoSpaceAvailable )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;

    std::vector<RawData::BufferHandle> handles;
    // Fill all the sample slots
    for ( size_t i = 0; i < signalOverrides1.maxNumOfSamples.get(); i++ )
    {
        RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
        ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );
        handles.push_back( handle );
    }

    std::vector<RawData::LoanedFrame> rawDataFrames;
    // Now hold a reference to the data so that all samples are in use
    for ( auto handle : handles )
    {
        auto loanedRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, handle );
        ASSERT_FALSE( loanedRawDataFrame.isNull() );
        rawDataFrames.push_back( std::move( loanedRawDataFrame ) );
    }

    // Since all samples are in use, none of them should be deleted and trying to add more data should fail
    RawData::RawDataType rawData = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    auto handle = rawDataBufferManager.push( &rawData.front(), rawData.size(), timestamp++, typeId1 );
    ASSERT_EQ( handle, RawData::INVALID_BUFFER_HANDLE );
}

TEST_F( RawDataManagerTest, dataRetrieval )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;
    RawData::RawDataType rawDataTest1 = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

    auto handle = rawDataBufferManager.push( &rawDataTest1.front(), rawDataTest1.size(), timestamp, typeId1 );
    ASSERT_NE( handle, RawData::INVALID_BUFFER_HANDLE );

    auto stats = rawDataBufferManager.getStatistics();
    ASSERT_EQ( stats.overallNumOfSamplesReceived, 1 );
    ASSERT_EQ( stats.numOfSamplesCurrentlyInMemory, 1 );
    ASSERT_EQ( stats.numOfSamplesAccessedBySender, 0 );

    auto cmpRawData = []( const RawData::RawDataType &orgData, const RawData::LoanedFrame &rawDataRec ) {
        if ( orgData.size() != rawDataRec.getSize() )
        {
            return false;
        }
        auto rawDataRecPtr = rawDataRec.getData();
        for ( size_t i = 0; i < orgData.size(); ++i )
        {
            if ( orgData[i] != *rawDataRecPtr++ )
            {
                return false;
            }
        }
        return true;
    };

    auto rawDataFrame = rawDataBufferManager.borrowFrame( typeId1, handle );
    ASSERT_FALSE( rawDataFrame.isNull() );
    ASSERT_EQ( rawDataFrame.getSize(), rawDataTest1.size() );
    ASSERT_TRUE( cmpRawData( rawDataTest1, rawDataFrame ) );

    stats = rawDataBufferManager.getStatistics();
    ASSERT_EQ( stats.overallNumOfSamplesReceived, 1 );
    ASSERT_EQ( stats.numOfSamplesCurrentlyInMemory, 1 );
    ASSERT_EQ( stats.numOfSamplesAccessedBySender, 1 );

    auto msgTypeStats = rawDataBufferManager.getStatistics( typeId1 );
    ASSERT_EQ( msgTypeStats.overallNumOfSamplesReceived, 1 );
    ASSERT_EQ( msgTypeStats.numOfSamplesCurrentlyInMemory, 1 );
    ASSERT_EQ( msgTypeStats.numOfSamplesAccessedBySender, 1 );

    RawData::RawDataType rawDataTest3( 100000 );
    uint32_t typeId3 = 301;
    std::iota( std::begin( rawDataTest3 ), std::end( rawDataTest3 ), 0 );
    auto handle3 = rawDataBufferManager.push( &rawDataTest3.front(), rawDataTest3.size(), timestamp, typeId3 );
    ASSERT_NE( handle3, RawData::INVALID_BUFFER_HANDLE );

    stats = rawDataBufferManager.getStatistics();
    ASSERT_EQ( stats.overallNumOfSamplesReceived, 2 );
    ASSERT_EQ( stats.numOfSamplesCurrentlyInMemory, 2 );
    ASSERT_EQ( stats.numOfSamplesAccessedBySender, 1 );

    // TypeID not included for data collection
    uint32_t InvalidTypeId = 902;
    auto rawDataFrameInvalid = rawDataBufferManager.borrowFrame( InvalidTypeId, handle3 );
    ASSERT_TRUE( rawDataFrameInvalid.isNull() );

    stats = rawDataBufferManager.getStatistics();
    ASSERT_EQ( stats.overallNumOfSamplesReceived, 2 );
    ASSERT_EQ( stats.numOfSamplesCurrentlyInMemory, 2 );
    ASSERT_EQ( stats.numOfSamplesAccessedBySender, 1 );

    auto rawDataFrame2 = rawDataBufferManager.borrowFrame( typeId3, handle3 );
    ASSERT_FALSE( rawDataFrame2.isNull() );
    ASSERT_EQ( rawDataFrame2.getSize(), rawDataTest3.size() );
    ASSERT_TRUE( cmpRawData( rawDataTest3, rawDataFrame2 ) );

    stats = rawDataBufferManager.getStatistics();
    ASSERT_EQ( stats.overallNumOfSamplesReceived, 2 );
    ASSERT_EQ( stats.numOfSamplesCurrentlyInMemory, 2 );
    ASSERT_EQ( stats.numOfSamplesAccessedBySender, 2 );

    msgTypeStats = rawDataBufferManager.getStatistics( typeId3 );
    ASSERT_EQ( msgTypeStats.overallNumOfSamplesReceived, 1 );
    ASSERT_EQ( msgTypeStats.numOfSamplesCurrentlyInMemory, 1 );
    ASSERT_EQ( msgTypeStats.numOfSamplesAccessedBySender, 1 );
}

TEST_F( RawDataManagerTest, dataDeletion )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;
    RawData::RawDataType rawDataTest1 = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

    auto handle = rawDataBufferManager.push( &rawDataTest1.front(), rawDataTest1.size(), timestamp, typeId1 );
    const auto sizeData1 = rawDataBufferManager.getUsedMemory();

    ASSERT_EQ( sizeData1, rawDataTest1.size() );

    RawData::RawDataType rawDataTest2( 100000 );
    uint32_t typeId2 = 201;
    std::iota( std::begin( rawDataTest2 ), std::end( rawDataTest2 ), 0 );
    auto handle2 = rawDataBufferManager.push( &rawDataTest2.front(), rawDataTest2.size(), timestamp, typeId2 );
    const auto totalSize = rawDataBufferManager.getUsedMemory();

    const auto sizeData2 = totalSize - sizeData1;

    auto stats = rawDataBufferManager.getStatistics();
    ASSERT_EQ( stats.overallNumOfSamplesReceived, 2 );
    ASSERT_EQ( stats.numOfSamplesCurrentlyInMemory, 2 );

    {
        // The manager should guarantee the data is valid while this object is alive
        auto loanedRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, handle );

        stats = rawDataBufferManager.getStatistics();
        ASSERT_EQ( stats.overallNumOfSamplesReceived, 2 );
        ASSERT_EQ( stats.numOfSamplesCurrentlyInMemory, 2 );

        {
            auto loanedRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, handle );

            stats = rawDataBufferManager.getStatistics();
            ASSERT_EQ( stats.overallNumOfSamplesReceived, 2 );
            ASSERT_EQ( stats.numOfSamplesCurrentlyInMemory, 2 );
        }

        stats = rawDataBufferManager.getStatistics();
        ASSERT_EQ( stats.overallNumOfSamplesReceived, 2 );
        ASSERT_EQ( stats.numOfSamplesCurrentlyInMemory, 2 );
    }

    // Only after both loanedRawDataFrame are out of scope, memory should be made available again
    ASSERT_EQ( rawDataBufferManager.getUsedMemory(), sizeData2 );
    stats = rawDataBufferManager.getStatistics();
    ASSERT_EQ( stats.overallNumOfSamplesReceived, 2 );
    ASSERT_EQ( stats.numOfSamplesCurrentlyInMemory, 1 );

    {
        auto loanedRawDataFrame = rawDataBufferManager.borrowFrame( typeId2, handle2 );

        stats = rawDataBufferManager.getStatistics();
        ASSERT_EQ( stats.overallNumOfSamplesReceived, 2 );
        ASSERT_EQ( stats.numOfSamplesCurrentlyInMemory, 1 );
    }

    // After the last loanedRawDataFrame is out of scope, no memory should be in use anymore
    ASSERT_EQ( rawDataBufferManager.getUsedMemory(), 0 );
    stats = rawDataBufferManager.getStatistics();
    ASSERT_EQ( stats.overallNumOfSamplesReceived, 2 );
    ASSERT_EQ( stats.numOfSamplesCurrentlyInMemory, 0 );
}

TEST_F( RawDataManagerTest, accessRawDataFromMultipleThreads )
{
    RawData::BufferManager rawDataBufferManager( bufferManagerConfig.get() );
    ASSERT_EQ( rawDataBufferManager.updateConfig( updatedSignals ), RawData::BufferErrorCode::SUCCESSFUL );

    Timestamp timestamp = 160000000;
    auto typeId1 = signalUpdateConfig1.typeId;

    auto stats = rawDataBufferManager.getStatistics();
    ASSERT_EQ( stats.overallNumOfSamplesReceived, 0 );
    ASSERT_EQ( stats.numOfSamplesCurrentlyInMemory, 0 );

    auto consumeData = [&]() {
        RawData::RawDataType rawDataTest1 = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        auto handle = rawDataBufferManager.push( &rawDataTest1.front(), rawDataTest1.size(), timestamp++, typeId1 );
        if ( handle != RawData::INVALID_BUFFER_HANDLE )
        {
            auto loanedRawDataFrame = rawDataBufferManager.borrowFrame( typeId1, handle );
        }
    };

    std::vector<std::thread> threads;
    for ( int i = 0; i < 1000; i++ )
    {
        threads.emplace_back( consumeData );
    }

    for ( std::thread &t : threads )
    {
        t.join();
    }

    stats = rawDataBufferManager.getStatistics();
    ASSERT_EQ( stats.overallNumOfSamplesReceived, 1000 );
    ASSERT_EQ( stats.numOfSamplesCurrentlyInMemory, 0 );
    ASSERT_EQ( stats.numOfSamplesAccessedBySender, 0 );
    ASSERT_EQ( rawDataBufferManager.getUsedMemory(), 0 );
}

TEST( RawDataManagerConfigTest, defaultConfig )
{
    auto bufferManagerConfig = RawData::BufferManagerConfig::create();
    ASSERT_TRUE( bufferManagerConfig.has_value() );

    auto signalConfig = bufferManagerConfig.get().getSignalConfig( 101, "interface1", "ImageTopic" );
    ASSERT_EQ( signalConfig.typeId, 101 );
    ASSERT_EQ( signalConfig.storageStrategy, RawData::StorageStrategy::COPY_ON_INGEST_SYNC );
    ASSERT_EQ( signalConfig.reservedBytes, 0 );
    ASSERT_EQ( signalConfig.maxNumOfSamples, SIZE_MAX );
    ASSERT_EQ( signalConfig.maxBytesPerSample, 1_GiB );
    ASSERT_EQ( signalConfig.maxOverallBytes, 1_GiB );
}

TEST( RawDataManagerConfigTest, overrideSignalConfigMaxBytes )
{
    auto maxBytes = boost::make_optional<size_t>( 10000 );
    auto reservedBytesPerSignal = boost::make_optional<size_t>( 50 );
    auto maxNumOfSamplesPerSignal = boost::make_optional<size_t>( 20 );
    auto maxBytesPerSample = boost::make_optional<size_t>( 1000 );
    auto maxBytesPerSignal = boost::make_optional<size_t>( 5000 );

    RawData::SignalBufferOverrides signalOverrides1;
    signalOverrides1.interfaceId = "interface1";
    signalOverrides1.messageId = "ImageTopic";
    signalOverrides1.maxBytes = 500;
    std::vector<RawData::SignalBufferOverrides> overridesPerSignal = { signalOverrides1 };

    auto bufferManagerConfig = RawData::BufferManagerConfig::create( maxBytes,
                                                                     reservedBytesPerSignal,
                                                                     maxNumOfSamplesPerSignal,
                                                                     maxBytesPerSample,
                                                                     maxBytesPerSignal,
                                                                     overridesPerSignal );
    ASSERT_TRUE( bufferManagerConfig.has_value() );

    auto signalConfig = bufferManagerConfig.get().getSignalConfig( 101, "interface1", "ImageTopic" );
    ASSERT_EQ( signalConfig.typeId, 101 );
    ASSERT_EQ( signalConfig.storageStrategy, RawData::StorageStrategy::COPY_ON_INGEST_SYNC );
    ASSERT_EQ( signalConfig.reservedBytes, 50 );
    ASSERT_EQ( signalConfig.maxNumOfSamples, 20 );
    ASSERT_EQ( signalConfig.maxBytesPerSample, 500 );
    ASSERT_EQ( signalConfig.maxOverallBytes, 500 );

    // Now with a signal without any overrides, the general config should be used
    signalConfig = bufferManagerConfig.get().getSignalConfig( 201, "interface1", "ImageTopic2" );
    ASSERT_EQ( signalConfig.typeId, 201 );
    ASSERT_EQ( signalConfig.storageStrategy, RawData::StorageStrategy::COPY_ON_INGEST_SYNC );
    ASSERT_EQ( signalConfig.reservedBytes, 50 );
    ASSERT_EQ( signalConfig.maxNumOfSamples, 20 );
    ASSERT_EQ( signalConfig.maxBytesPerSample, 1000 );
    ASSERT_EQ( signalConfig.maxOverallBytes, 5000 );
}

TEST( RawDataManagerConfigTest, overrideSignalConfigAllParameters )
{
    auto maxBytes = boost::make_optional<size_t>( 10000 );
    auto reservedBytesPerSignal = boost::make_optional<size_t>( 50 );
    auto maxNumOfSamplesPerSignal = boost::make_optional<size_t>( 20 );
    auto maxBytesPerSample = boost::make_optional<size_t>( 1000 );
    auto maxBytesPerSignal = boost::make_optional<size_t>( 5000 );

    RawData::SignalBufferOverrides signalOverrides1;
    signalOverrides1.interfaceId = "interface1";
    signalOverrides1.messageId = "ImageTopic";
    signalOverrides1.reservedBytes = 101;
    signalOverrides1.maxNumOfSamples = 123;
    signalOverrides1.maxBytesPerSample = 56;
    signalOverrides1.maxBytes = 500;
    std::vector<RawData::SignalBufferOverrides> overridesPerSignal = { signalOverrides1 };

    auto bufferManagerConfig = RawData::BufferManagerConfig::create( maxBytes,
                                                                     reservedBytesPerSignal,
                                                                     maxNumOfSamplesPerSignal,
                                                                     maxBytesPerSample,
                                                                     maxBytesPerSignal,
                                                                     overridesPerSignal );
    ASSERT_TRUE( bufferManagerConfig.has_value() );

    auto signalConfig = bufferManagerConfig.get().getSignalConfig( 101, "interface1", "ImageTopic" );
    ASSERT_EQ( signalConfig.typeId, 101 );
    ASSERT_EQ( signalConfig.storageStrategy, RawData::StorageStrategy::COPY_ON_INGEST_SYNC );
    ASSERT_EQ( signalConfig.reservedBytes, 101 );
    ASSERT_EQ( signalConfig.maxNumOfSamples, 123 );
    ASSERT_EQ( signalConfig.maxBytesPerSample, 56 );
    ASSERT_EQ( signalConfig.maxOverallBytes, 500 );

    // Now with a signal without any overrides, the general config should be used
    signalConfig = bufferManagerConfig.get().getSignalConfig( 201, "interface1", "ImageTopic2" );
    ASSERT_EQ( signalConfig.typeId, 201 );
    ASSERT_EQ( signalConfig.storageStrategy, RawData::StorageStrategy::COPY_ON_INGEST_SYNC );
    ASSERT_EQ( signalConfig.reservedBytes, 50 );
    ASSERT_EQ( signalConfig.maxNumOfSamples, 20 );
    ASSERT_EQ( signalConfig.maxBytesPerSample, 1000 );
    ASSERT_EQ( signalConfig.maxOverallBytes, 5000 );
}

TEST( RawDataManagerConfigTest, omitMaxBytesPerSample )
{
    auto maxBytes = boost::make_optional<size_t>( 10000 );
    auto reservedBytesPerSignal = boost::make_optional<size_t>( 50 );
    auto maxNumOfSamplesPerSignal = boost::make_optional<size_t>( 20 );
    auto maxBytesPerSignal = boost::make_optional<size_t>( 5000 );

    std::vector<RawData::SignalBufferOverrides> overridesPerSignal;
    auto bufferManagerConfig = RawData::BufferManagerConfig::create( maxBytes,
                                                                     reservedBytesPerSignal,
                                                                     maxNumOfSamplesPerSignal,
                                                                     boost::none,
                                                                     maxBytesPerSignal,
                                                                     overridesPerSignal );
    ASSERT_TRUE( bufferManagerConfig.has_value() );

    auto signalConfig = bufferManagerConfig.get().getSignalConfig( 101, "interface1", "ImageTopic" );
    ASSERT_EQ( signalConfig.typeId, 101 );
    ASSERT_EQ( signalConfig.storageStrategy, RawData::StorageStrategy::COPY_ON_INGEST_SYNC );
    ASSERT_EQ( signalConfig.reservedBytes, 50 );
    ASSERT_EQ( signalConfig.maxNumOfSamples, 20 );
    // When omitted, max bytes per sample should always be set to max bytes for the signal
    ASSERT_EQ( signalConfig.maxBytesPerSample, 5000 );
    ASSERT_EQ( signalConfig.maxOverallBytes, 5000 );
}

class RawDataManagerConfigValidationTest : public ::testing::Test
{
protected:
    bool
    createConfig()
    {
        auto bufferManagerConfig = RawData::BufferManagerConfig::create( mMaxBytes,
                                                                         mReservedBytesPerSignal,
                                                                         mMaxNumOfSamplesPerSignal,
                                                                         mMaxBytesPerSample,
                                                                         mMaxBytesPerSignal,
                                                                         mOverridesPerSignal );
        return bufferManagerConfig.has_value();
    }

    boost::optional<size_t> mMaxBytes;
    boost::optional<size_t> mReservedBytesPerSignal;
    boost::optional<size_t> mMaxNumOfSamplesPerSignal;
    boost::optional<size_t> mMaxBytesPerSample;
    boost::optional<size_t> mMaxBytesPerSignal;
    std::vector<RawData::SignalBufferOverrides> mOverridesPerSignal;
};

TEST_F( RawDataManagerConfigValidationTest, maxBytesIsZero )
{
    mMaxBytes = 0;
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, reservedBytesPerSignalIsLargerThanMaxBytes )
{
    mMaxBytes = 999;
    mReservedBytesPerSignal = 1000;
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, reservedBytesPerSignalIsLargerThanMaxBytesPerSignal )
{
    mMaxBytesPerSignal = 999;
    mReservedBytesPerSignal = 1000;
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, maxNumberOfSamplesIsZero )
{
    mMaxNumOfSamplesPerSignal = 0;
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, maxBytesPerSampleIsZero )
{
    mMaxBytesPerSample = 0;
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, maxBytesPerSampleIsLargerThanMaxBytes )
{
    mMaxBytes = 999;
    mMaxBytesPerSample = 1000;
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, maxBytesPerSampleIsLargerThanMaxBytesPerSignal )
{
    mMaxBytesPerSignal = 999;
    mMaxBytesPerSample = 1000;
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, maxBytesPerSignalIsZero )
{
    mMaxBytesPerSignal = 0;
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, maxBytesPerSignalIsLargerThanMaxBytes )
{
    mMaxBytes = 999;
    mMaxBytesPerSignal = 1000;
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, multipleOverridesForTheSameSignalType )
{
    RawData::SignalBufferOverrides signalOverrides1;
    signalOverrides1.interfaceId = "interface1";
    signalOverrides1.messageId = "topic1";
    RawData::SignalBufferOverrides signalOverrides2;
    signalOverrides2.interfaceId = "interface1";
    signalOverrides2.messageId = "topic1";
    mOverridesPerSignal = { signalOverrides1, signalOverrides2 };
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, overriddenMaxBytesIsZero )
{
    RawData::SignalBufferOverrides signalOverrides1;
    signalOverrides1.maxBytes = 0;
    mOverridesPerSignal = { signalOverrides1 };
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, overriddenMaxBytesIsLargerThanOverallMaxBytes )
{
    mMaxBytes = 999;
    RawData::SignalBufferOverrides signalOverrides1;
    signalOverrides1.maxBytes = 1000;
    mOverridesPerSignal = { signalOverrides1 };
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, overriddenReservedBytesIsLargerThanOverallMaxBytes )
{
    mMaxBytes = 999;
    RawData::SignalBufferOverrides signalOverrides1;
    signalOverrides1.reservedBytes = 1000;
    mOverridesPerSignal = { signalOverrides1 };
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, overriddenReservedBytesIsLargerThanOverridenMaxBytes )
{
    RawData::SignalBufferOverrides signalOverrides1;
    signalOverrides1.maxBytes = 999;
    signalOverrides1.reservedBytes = 1000;
    mOverridesPerSignal = { signalOverrides1 };
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, overriddenMaxNumberOfSamplesIsZero )
{
    RawData::SignalBufferOverrides signalOverrides1;
    signalOverrides1.maxNumOfSamples = 0;
    mOverridesPerSignal = { signalOverrides1 };
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, overriddenMaxBytesPerSampleIsZero )
{
    RawData::SignalBufferOverrides signalOverrides1;
    signalOverrides1.maxBytesPerSample = 0;
    mOverridesPerSignal = { signalOverrides1 };
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, overriddenMaxBytesPerSampleIsLargerThanOverallMaxBytes )
{
    mMaxBytes = 999;
    RawData::SignalBufferOverrides signalOverrides1;
    signalOverrides1.maxBytesPerSample = 1000;
    mOverridesPerSignal = { signalOverrides1 };
    ASSERT_FALSE( createConfig() );
}

TEST_F( RawDataManagerConfigValidationTest, overriddenMaxBytesPerSampleIsLargerThanOverridenMaxBytes )
{
    RawData::SignalBufferOverrides signalOverrides1;
    signalOverrides1.maxBytes = 999;
    signalOverrides1.maxBytesPerSample = 1000;
    mOverridesPerSignal = { signalOverrides1 };
    ASSERT_FALSE( createConfig() );
}

} // namespace IoTFleetWise
} // namespace Aws
