// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/DataSenderManagerWorkerThread.h"
#include "ConnectivityModuleMock.h"
#include "DataSenderManagerMock.h"
#include "Testing.h"
#include "WaitUntil.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/DataSenderManager.h"
#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/QueueTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <cstdint>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#ifdef FWE_FEATURE_REMOTE_COMMANDS
#include "aws/iotfleetwise/CommandTypes.h"
#include "aws/iotfleetwise/ICommandDispatcher.h"
#endif

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::InvokeArgument;
using ::testing::Return;
using ::testing::Sequence;
using ::testing::StrictMock;

class DataSenderManagerWorkerThreadTest : public ::testing::Test
{
public:
    DataSenderManagerWorkerThreadTest()
        : mCollectedDataQueue( std::make_shared<DataSenderQueue>( 10000, "Collected Data" ) )
#ifdef FWE_FEATURE_REMOTE_COMMANDS
        , mCommandResponses( std::make_shared<DataSenderQueue>( 10000, "Command Responses" ) )
#endif
        , mDataSenderManagerWorkerThread( mConnectivityModule,
                                          [&]() {
                                              auto dataSenderManager =
                                                  std::make_unique<StrictMock<Testing::DataSenderManagerMock>>();
                                              mDataSenderManager = dataSenderManager.get();
                                              return dataSenderManager;
                                          }(),
                                          100,
                                          {
#ifdef FWE_FEATURE_REMOTE_COMMANDS
                                              mCommandResponses,
#endif
                                              mCollectedDataQueue } )
    {
    }
    void
    SetUp() override
    {
        mTriggerTime = ClockHandler::getClock()->systemTimeSinceEpochMs();

        mTriggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();
        mTriggeredCollectionSchemeData->metadata.decoderID = "TESTDECODERID";
        mTriggeredCollectionSchemeData->metadata.collectionSchemeID = "TESTCOLLECTIONSCHEME";
        mTriggeredCollectionSchemeData->triggerTime = mTriggerTime;
        mTriggeredCollectionSchemeData->eventID = 579;

        EXPECT_CALL( mConnectivityModule, isAlive() ).WillRepeatedly( Return( true ) );
    }

    void
    TearDown() override
    {
        mDataSenderManagerWorkerThread.stop();
    }

protected:
    uint64_t mPersistencyUploadRetryIntervalMs{ 1000 };
    Timestamp mTriggerTime;
    std::shared_ptr<TriggeredCollectionSchemeData> mTriggeredCollectionSchemeData;

    StrictMock<Testing::ConnectivityModuleMock> mConnectivityModule;
    StrictMock<Testing::DataSenderManagerMock> *mDataSenderManager{};
    std::shared_ptr<DataSenderQueue> mCollectedDataQueue;
#ifdef FWE_FEATURE_REMOTE_COMMANDS
    std::shared_ptr<DataSenderQueue> mCommandResponses;
#endif
    DataSenderManagerWorkerThread mDataSenderManagerWorkerThread;
};

class DataSenderManagerWorkerThreadTestWithAllSignalTypes : public DataSenderManagerWorkerThreadTest,
                                                            public testing::WithParamInterface<SignalType>
{
};

INSTANTIATE_TEST_SUITE_P( MultipleSignals,
                          DataSenderManagerWorkerThreadTestWithAllSignalTypes,
                          allSignalTypes,
                          signalTypeParamInfoToString );

TEST_F( DataSenderManagerWorkerThreadTest, ProcessNoData )
{
    EXPECT_CALL( *mDataSenderManager, mockedProcessData( _ ) ).Times( 1 );
    mDataSenderManagerWorkerThread.start();

    mCollectedDataQueue->push( mTriggeredCollectionSchemeData );

    WAIT_ASSERT_GT( mDataSenderManager->mCheckAndSendRetrievedDataCalls, 0U );
}

TEST_P( DataSenderManagerWorkerThreadTestWithAllSignalTypes, ProcessSingleTrigger )
{
    SignalType signalType = GetParam();
    EXPECT_CALL( *mDataSenderManager, mockedProcessData( _ ) ).Times( 1 );
    mDataSenderManagerWorkerThread.start();

    auto signal1 = CollectedSignal( 1234, mTriggerTime - 10, 40.5, signalType );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    mCollectedDataQueue->push( mTriggeredCollectionSchemeData );

    WAIT_ASSERT_EQ( mDataSenderManager->getProcessedData().size(), 1U );
    auto processedData = mDataSenderManager->getProcessedData<TriggeredCollectionSchemeData>();
    ASSERT_TRUE( mDataSenderManagerWorkerThread.stop() );
    ASSERT_EQ( processedData[0]->signals.size(), 1 );

    auto processedSignal = processedData[0]->signals[0];
    ASSERT_EQ( processedSignal.signalID, 1234 );
    ASSERT_EQ( processedSignal.receiveTime, mTriggerTime - 10 );
    ASSERT_NO_FATAL_FAILURE( assertSignalValue( processedSignal.value, 40.5, signalType ) );
}

TEST_F( DataSenderManagerWorkerThreadTest, ProcessMultipleTriggers )
{
    auto triggeredCollectionSchemeData2 = std::make_shared<TriggeredCollectionSchemeData>();
    triggeredCollectionSchemeData2->metadata.decoderID = "TESTDECODERID2";
    triggeredCollectionSchemeData2->metadata.collectionSchemeID = "TESTCOLLECTIONSCHEME2";
    triggeredCollectionSchemeData2->triggerTime = mTriggerTime;
    triggeredCollectionSchemeData2->eventID = 590;

    EXPECT_CALL( *mDataSenderManager, mockedProcessData( _ ) ).Times( 2 );

    mDataSenderManagerWorkerThread.start();

    auto signal1 = CollectedSignal( 1234, mTriggerTime - 10, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    auto signal2 = CollectedSignal( 5678, mTriggerTime, 99.5, SignalType::DOUBLE );
    triggeredCollectionSchemeData2->signals.push_back( signal2 );

    mCollectedDataQueue->push( mTriggeredCollectionSchemeData );
    mCollectedDataQueue->push( triggeredCollectionSchemeData2 );

    WAIT_ASSERT_EQ( mDataSenderManager->getProcessedData().size(), 2U );
    ASSERT_TRUE( mDataSenderManagerWorkerThread.stop() );
    auto processedData = mDataSenderManager->getProcessedData<TriggeredCollectionSchemeData>();

    ASSERT_EQ( processedData[0]->signals.size(), 1 );
    auto processedSignal = processedData[0]->signals[0];
    ASSERT_EQ( processedSignal.signalID, 1234 );
    ASSERT_EQ( processedSignal.receiveTime, mTriggerTime - 10 );
    ASSERT_EQ( processedSignal.value.value.doubleVal, 40.5 );

    ASSERT_EQ( processedData[1]->signals.size(), 1 );
    processedSignal = processedData[1]->signals[0];
    ASSERT_EQ( processedSignal.signalID, 5678 );
    ASSERT_EQ( processedSignal.receiveTime, mTriggerTime );
    ASSERT_EQ( processedSignal.value.value.doubleVal, 99.5 );
}

#ifdef FWE_FEATURE_REMOTE_COMMANDS
TEST_F( DataSenderManagerWorkerThreadTest, ProcessSingleCommandResponse )
{
    EXPECT_CALL( *mDataSenderManager, mockedProcessData( _ ) ).Times( 1 );
    mDataSenderManagerWorkerThread.start();

    auto commandResponse1 = std::make_shared<CommandResponse>(
        "command123", CommandStatus::EXECUTION_FAILED, REASON_CODE_DECODER_MANIFEST_OUT_OF_SYNC, "status456" );
    mCommandResponses->push( commandResponse1 );

    WAIT_ASSERT_EQ( mDataSenderManager->getProcessedData().size(), 1U );
    ASSERT_TRUE( mDataSenderManagerWorkerThread.stop() );

    auto processedCommandResponses = mDataSenderManager->getProcessedData<CommandResponse>();
    ASSERT_EQ( processedCommandResponses[0]->id, "command123" );
    ASSERT_EQ( processedCommandResponses[0]->status, CommandStatus::EXECUTION_FAILED );
    ASSERT_EQ( processedCommandResponses[0]->reasonCode, REASON_CODE_DECODER_MANIFEST_OUT_OF_SYNC );
    ASSERT_EQ( processedCommandResponses[0]->reasonDescription, "status456" );
}

TEST_F( DataSenderManagerWorkerThreadTest, ProcessMultipleCommandResponses )
{
    EXPECT_CALL( *mDataSenderManager, mockedProcessData( _ ) ).Times( 3 );
    mDataSenderManagerWorkerThread.start();

    auto commandResponse1 =
        std::make_shared<CommandResponse>( "command1", CommandStatus::SUCCEEDED, 0x1234, "status1" );
    mCommandResponses->push( commandResponse1 );
    auto commandResponse2 =
        std::make_shared<CommandResponse>( "command2", CommandStatus::EXECUTION_FAILED, 0x5678, "status2" );
    mCommandResponses->push( commandResponse2 );
    auto commandResponse3 =
        std::make_shared<CommandResponse>( "command3", CommandStatus::EXECUTION_TIMEOUT, 0x9ABC, "status3" );
    mCommandResponses->push( commandResponse3 );

    WAIT_ASSERT_EQ( mDataSenderManager->getProcessedData().size(), 3U );
    ASSERT_TRUE( mDataSenderManagerWorkerThread.stop() );

    auto processedCommandResponses = mDataSenderManager->getProcessedData<CommandResponse>();
    ASSERT_EQ( processedCommandResponses[0]->id, "command1" );
    ASSERT_EQ( processedCommandResponses[0]->status, CommandStatus::SUCCEEDED );
    ASSERT_EQ( processedCommandResponses[0]->reasonCode, 0x1234 );
    ASSERT_EQ( processedCommandResponses[0]->reasonDescription, "status1" );
    ASSERT_EQ( processedCommandResponses[1]->id, "command2" );
    ASSERT_EQ( processedCommandResponses[1]->status, CommandStatus::EXECUTION_FAILED );
    ASSERT_EQ( processedCommandResponses[1]->reasonCode, 0x5678 );
    ASSERT_EQ( processedCommandResponses[1]->reasonDescription, "status2" );
    ASSERT_EQ( processedCommandResponses[2]->id, "command3" );
    ASSERT_EQ( processedCommandResponses[2]->status, CommandStatus::EXECUTION_TIMEOUT );
    ASSERT_EQ( processedCommandResponses[2]->reasonCode, 0x9ABC );
    ASSERT_EQ( processedCommandResponses[2]->reasonDescription, "status3" );
}
#endif

} // namespace IoTFleetWise
} // namespace Aws
