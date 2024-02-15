// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataSenderManagerWorkerThread.h"
#include "CANInterfaceIDTranslator.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "ConnectivityModuleMock.h"
#include "DataSenderManagerMock.h"
#include "SignalTypes.h"
#include "Testing.h"
#include "TimeTypes.h"
#include "WaitUntil.h"
#include <cstdint>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "ICollectionSchemeList.h"
#include <functional>
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
    void
    SetUp() override
    {
        mTriggerTime = ClockHandler::getClock()->systemTimeSinceEpochMs();

        mTriggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();
        mTriggeredCollectionSchemeData->metadata.decoderID = "TESTDECODERID";
        mTriggeredCollectionSchemeData->metadata.collectionSchemeID = "TESTCOLLECTIONSCHEME";
        mTriggeredCollectionSchemeData->triggerTime = mTriggerTime;
        mTriggeredCollectionSchemeData->eventID = 579;

        mConnectivityModule = std::make_shared<StrictMock<Testing::ConnectivityModuleMock>>();
        mDataSenderManager = std::make_shared<StrictMock<Testing::DataSenderManagerMock>>( canIDTranslator );
        mCollectedDataQueue = std::make_shared<CollectedDataReadyToPublish>( 10000 );
        mDataSenderManagerWorkerThread = std::make_unique<DataSenderManagerWorkerThread>(
            mConnectivityModule, mDataSenderManager, 100, mCollectedDataQueue );

        EXPECT_CALL( *mConnectivityModule, isAlive() ).WillRepeatedly( Return( true ) );
    }

    void
    TearDown() override
    {
        mDataSenderManagerWorkerThread->stop();
    }

protected:
    uint64_t mPersistencyUploadRetryIntervalMs{ 1000 };
    Timestamp mTriggerTime;
    std::shared_ptr<TriggeredCollectionSchemeData> mTriggeredCollectionSchemeData;

    std::shared_ptr<StrictMock<Testing::ConnectivityModuleMock>> mConnectivityModule;
    CANInterfaceIDTranslator canIDTranslator;
    std::shared_ptr<StrictMock<Testing::DataSenderManagerMock>> mDataSenderManager;
    std::shared_ptr<CollectedDataReadyToPublish> mCollectedDataQueue;
    std::unique_ptr<DataSenderManagerWorkerThread> mDataSenderManagerWorkerThread;
};

class DataSenderManagerWorkerThreadTestWithAllSignalTypes : public DataSenderManagerWorkerThreadTest,
                                                            public testing::WithParamInterface<SignalType>
{
};

INSTANTIATE_TEST_SUITE_P( MultipleSignals,
                          DataSenderManagerWorkerThreadTestWithAllSignalTypes,
                          allSignalTypes,
                          signalTypeToString );

TEST_F( DataSenderManagerWorkerThreadTest, ProcessNoData )
{
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    EXPECT_CALL( *mDataSenderManager, mockedProcessCollectedData( _, _ ) )
#else
    EXPECT_CALL( *mDataSenderManager, mockedProcessCollectedData( _ ) )
#endif
        .Times( 0 );
    mDataSenderManagerWorkerThread->start();

    mCollectedDataQueue->push( mTriggeredCollectionSchemeData );

    WAIT_ASSERT_GT( mDataSenderManager->mCheckAndSendRetrievedDataCalls, 0U );
}

TEST_P( DataSenderManagerWorkerThreadTestWithAllSignalTypes, ProcessSingleTrigger )
{
    SignalType signalType = GetParam();
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    EXPECT_CALL( *mDataSenderManager, mockedProcessCollectedData( _, _ ) )
#else
    EXPECT_CALL( *mDataSenderManager, mockedProcessCollectedData( _ ) )
#endif
        .Times( 1 );
    mDataSenderManagerWorkerThread->start();

    auto signal1 = CollectedSignal( 1234, mTriggerTime - 10, 40.5, signalType );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    mCollectedDataQueue->push( mTriggeredCollectionSchemeData );

    WAIT_ASSERT_EQ( mDataSenderManager->getProcessedData().size(), 1U );
    ASSERT_TRUE( mDataSenderManagerWorkerThread->stop() );
    ASSERT_EQ( mDataSenderManager->getProcessedData()[0]->signals.size(), 1 );

    auto processedSignal = mDataSenderManager->getProcessedData()[0]->signals[0];
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

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    EXPECT_CALL( *mDataSenderManager, mockedProcessCollectedData( _, _ ) )
#else
    EXPECT_CALL( *mDataSenderManager, mockedProcessCollectedData( _ ) )
#endif
        .Times( 2 );

    mDataSenderManagerWorkerThread->start();

    auto signal1 = CollectedSignal( 1234, mTriggerTime - 10, 40.5, SignalType::DOUBLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    auto signal2 = CollectedSignal( 5678, mTriggerTime, 99.5, SignalType::DOUBLE );
    triggeredCollectionSchemeData2->signals.push_back( signal2 );

    mCollectedDataQueue->push( mTriggeredCollectionSchemeData );
    mCollectedDataQueue->push( triggeredCollectionSchemeData2 );

    WAIT_ASSERT_EQ( mDataSenderManager->getProcessedData().size(), 2U );
    ASSERT_TRUE( mDataSenderManagerWorkerThread->stop() );

    ASSERT_EQ( mDataSenderManager->getProcessedData()[0]->signals.size(), 1 );
    auto processedSignal = mDataSenderManager->getProcessedData()[0]->signals[0];
    ASSERT_EQ( processedSignal.signalID, 1234 );
    ASSERT_EQ( processedSignal.receiveTime, mTriggerTime - 10 );
    ASSERT_EQ( processedSignal.value.value.doubleVal, 40.5 );

    ASSERT_EQ( mDataSenderManager->getProcessedData()[1]->signals.size(), 1 );
    processedSignal = mDataSenderManager->getProcessedData()[1]->signals[0];
    ASSERT_EQ( processedSignal.signalID, 5678 );
    ASSERT_EQ( processedSignal.receiveTime, mTriggerTime );
    ASSERT_EQ( processedSignal.value.value.doubleVal, 99.5 );
}

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
TEST_F( DataSenderManagerWorkerThreadTest, ProcessSingleTriggerWithRawData )
{
    auto uploadedCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();
    uploadedCollectionSchemeData->metadata.decoderID = mTriggeredCollectionSchemeData->metadata.decoderID;
    uploadedCollectionSchemeData->metadata.collectionSchemeID =
        mTriggeredCollectionSchemeData->metadata.collectionSchemeID;
    uploadedCollectionSchemeData->triggerTime = mTriggeredCollectionSchemeData->triggerTime;
    uploadedCollectionSchemeData->eventID = mTriggeredCollectionSchemeData->eventID;
    uploadedCollectionSchemeData->uploadedS3Objects.push_back(
        UploadedS3Object{ "uploaded/object/key1", UploadedS3ObjectDataFormat::Cdr } );

    // There should be two calls to process the data. The first will be the raw data to be uploaded
    // to S3 and the second will be the message containing the S3 upload metadata (sent via MQTT).
    Sequence seq;
    EXPECT_CALL( *mDataSenderManager, mockedProcessCollectedData( _, _ ) )
        .InSequence( seq )
        .WillOnce( InvokeArgument<1>( uploadedCollectionSchemeData ) );
    EXPECT_CALL( *mDataSenderManager, mockedProcessCollectedData( _, _ ) ).Times( 1 ).InSequence( seq );

    mDataSenderManagerWorkerThread->start();

    auto signal1 = CollectedSignal( 1234, mTriggerTime, 888999, SignalType::RAW_DATA_BUFFER_HANDLE );
    mTriggeredCollectionSchemeData->signals.push_back( signal1 );

    mCollectedDataQueue->push( mTriggeredCollectionSchemeData );

    WAIT_ASSERT_EQ( mDataSenderManager->getProcessedData().size(), 2U );
    ASSERT_TRUE( mDataSenderManagerWorkerThread->stop() );

    ASSERT_EQ( mDataSenderManager->getProcessedData()[0]->signals.size(), 1 );
    ASSERT_EQ( mDataSenderManager->getProcessedData()[0]->uploadedS3Objects.size(), 0 );
    auto processedSignal = mDataSenderManager->getProcessedData()[0]->signals[0];
    ASSERT_EQ( processedSignal.signalID, 1234 );
    ASSERT_EQ( processedSignal.receiveTime, mTriggerTime );
    ASSERT_EQ( processedSignal.value.value.uint32Val, 888999 );

    ASSERT_EQ( mDataSenderManager->getProcessedData()[1]->signals.size(), 0 );
    ASSERT_EQ( mDataSenderManager->getProcessedData()[1]->uploadedS3Objects.size(), 1 );
    auto uploadedS3Object = mDataSenderManager->getProcessedData()[1]->uploadedS3Objects[0];
    ASSERT_EQ( uploadedS3Object.key, "uploaded/object/key1" );
    ASSERT_EQ( uploadedS3Object.dataFormat, UploadedS3ObjectDataFormat::Cdr );
}

TEST_F( DataSenderManagerWorkerThreadTest, UpdateActiveCollectionSchemes )
{
    auto activeCollectionSchemes = std::make_shared<ActiveCollectionSchemes>();

    mDataSenderManagerWorkerThread->start();
    mDataSenderManagerWorkerThread->onChangeCollectionSchemeList( activeCollectionSchemes );

    WAIT_ASSERT_TRUE( mDataSenderManager->mActiveCollectionSchemes != nullptr );
    ASSERT_EQ( mDataSenderManager->mActiveCollectionSchemes.get(), activeCollectionSchemes.get() );
}
#endif

} // namespace IoTFleetWise
} // namespace Aws
