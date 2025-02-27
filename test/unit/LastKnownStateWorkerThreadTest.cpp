// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/LastKnownStateWorkerThread.h"
#include "Testing.h"
#include "WaitUntil.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/CommandTypes.h"
#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/ICommandDispatcher.h"
#include "aws/iotfleetwise/LastKnownStateInspector.h"
#include "aws/iotfleetwise/LastKnownStateTypes.h"
#include "aws/iotfleetwise/QueueTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "state_templates.pb.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::InvokeArgument;
using ::testing::Return;
using ::testing::Sequence;

using signalTypes =
    ::testing::Types<uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t, float, double>;

template <typename T>
class LastKnownStateWorkerThreadTest : public ::testing::Test
{
public:
    void
    SetUp() override
    {
        mCommandResponses = std::make_shared<DataSenderQueue>( 100, "Command Responses" );
        mLastKnownStateInspector = std::make_unique<LastKnownStateInspector>( mCommandResponses, nullptr );
        mSignalBufferPtr = std::make_shared<SignalBuffer>( 100, "Signal Buffer" );
        mCollectedSignals = std::make_shared<DataSenderQueue>( 2, "LKS Signals" );
    }
    void
    TearDown() override
    {
        mLastKnownStateWorkerThread->stop();
    }

    bool
    popCommandResponse( std::shared_ptr<const CommandResponse> &commandResponse )
    {
        std::shared_ptr<const DataToSend> senderData;
        auto succeeded = mCommandResponses->pop( senderData );
        commandResponse = std::dynamic_pointer_cast<const CommandResponse>( senderData );
        return succeeded;
    }

    bool
    popCollectedData( std::shared_ptr<const LastKnownStateCollectedData> &collectedData )
    {
        std::shared_ptr<const DataToSend> senderData;
        auto succeeded = mCollectedSignals->pop( senderData );
        collectedData = std::dynamic_pointer_cast<const LastKnownStateCollectedData>( senderData );
        return succeeded;
    }

protected:
    std::unique_ptr<LastKnownStateInspector> mLastKnownStateInspector;
    std::unique_ptr<LastKnownStateWorkerThread> mLastKnownStateWorkerThread;
    std::shared_ptr<DataSenderQueue> mCommandResponses;
    SignalBufferPtr mSignalBufferPtr;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    std::shared_ptr<DataSenderQueue> mCollectedSignals;
};

class LastKnownStateWorkerDoubleTest : public LastKnownStateWorkerThreadTest<double>
{
};

TYPED_TEST_SUITE( LastKnownStateWorkerThreadTest, signalTypes );

TYPED_TEST( LastKnownStateWorkerThreadTest, SuccessfulProcessing )
{
    this->mLastKnownStateWorkerThread = std::make_unique<LastKnownStateWorkerThread>(
        this->mSignalBufferPtr, this->mCollectedSignals, std::move( this->mLastKnownStateInspector ), 1000 );
    ASSERT_TRUE( this->mLastKnownStateWorkerThread->start() );

    WAIT_ASSERT_TRUE( this->mLastKnownStateWorkerThread->isAlive() );

    Schemas::LastKnownState::StateTemplates protoLastKnownState;
    auto stateTemplateInfo = std::make_shared<StateTemplateInformation>(
        StateTemplateInformation{ "lks1",
                                  "decoder1",
                                  { LastKnownStateSignalInformation{ 1, getSignalType<TypeParam>() } },
                                  LastKnownStateUpdateStrategy::PERIODIC,
                                  2000 } );

    this->mLastKnownStateWorkerThread->onStateTemplatesChanged(
        std::make_shared<StateTemplateList>( StateTemplateList{ stateTemplateInfo } ) );

    Timestamp timestamp = this->mClock->systemTimeSinceEpochMs();
    CollectedSignalsGroup collectedSignalsGroup;
    collectedSignalsGroup.push_back( CollectedSignal( 1, timestamp, 10, getSignalType<TypeParam>() ) );
    this->mSignalBufferPtr->push( CollectedDataFrame( collectedSignalsGroup ) );

    // Now send the Activate command, which should trigger a collection
    LastKnownStateCommandRequest activateCommand;
    activateCommand.commandID = "command1";
    activateCommand.stateTemplateID = "lks1";
    activateCommand.operation = LastKnownStateOperation::ACTIVATE;
    this->mLastKnownStateWorkerThread->onNewCommandReceived( activateCommand );

    WAIT_ASSERT_FALSE( this->mCollectedSignals->isEmpty() );
}

TEST_F( LastKnownStateWorkerDoubleTest, FailOnStartNoSignalBuffer )
{
    mLastKnownStateWorkerThread = std::make_unique<LastKnownStateWorkerThread>(
        nullptr, mCollectedSignals, std::move( mLastKnownStateInspector ), 0 );
    ASSERT_FALSE( mLastKnownStateWorkerThread->start() );
}

TEST_F( LastKnownStateWorkerDoubleTest, FailOnStartNoCollectedSignalsQueue )
{
    mLastKnownStateWorkerThread = std::make_unique<LastKnownStateWorkerThread>(
        mSignalBufferPtr, nullptr, std::move( mLastKnownStateInspector ), 0 );
    ASSERT_FALSE( mLastKnownStateWorkerThread->start() );
}

TEST_F( LastKnownStateWorkerDoubleTest, ActivateAndDeactivateStateTemplate )
{
    mLastKnownStateWorkerThread = std::make_unique<LastKnownStateWorkerThread>(
        mSignalBufferPtr,
        mCollectedSignals,
        std::make_unique<LastKnownStateInspector>( mCommandResponses, nullptr ),
        50 );
    ASSERT_TRUE( mLastKnownStateWorkerThread->start() );
    WAIT_ASSERT_TRUE( mLastKnownStateWorkerThread->isAlive() );

    auto stateTemplateInfo = std::make_shared<StateTemplateInformation>( StateTemplateInformation{
        "lks1", "decoder1", { LastKnownStateSignalInformation{ 1 } }, LastKnownStateUpdateStrategy::PERIODIC, 100 } );

    mLastKnownStateWorkerThread->onStateTemplatesChanged(
        std::make_shared<StateTemplateList>( StateTemplateList{ stateTemplateInfo } ) );

    mSignalBufferPtr->push( CollectedDataFrame(
        CollectedSignalsGroup{ CollectedSignal( 1, mClock->systemTimeSinceEpochMs(), 10, SignalType::DOUBLE ) } ) );

    // It should not collect any signals until an Activate command is received
    DELAY_ASSERT_TRUE( mCollectedSignals->isEmpty() );

    std::shared_ptr<const CommandResponse> commandResponse;

    // Now send the Activate command, which should trigger a collection
    LastKnownStateCommandRequest activateCommand;
    activateCommand.commandID = "command1";
    activateCommand.stateTemplateID = "lks1";
    activateCommand.operation = LastKnownStateOperation::ACTIVATE;
    activateCommand.receivedTime = mClock->timeSinceEpoch();
    mLastKnownStateWorkerThread->onNewCommandReceived( activateCommand );

    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command1" );
    ASSERT_EQ( commandResponse->status, CommandStatus::SUCCEEDED );

    std::shared_ptr<const LastKnownStateCollectedData> collectedData;

    WAIT_ASSERT_TRUE( popCollectedData( collectedData ) );
    mSignalBufferPtr->push( CollectedDataFrame(
        CollectedSignalsGroup{ CollectedSignal( 1, mClock->systemTimeSinceEpochMs(), 10, SignalType::DOUBLE ) } ) );
    WAIT_ASSERT_TRUE( popCollectedData( collectedData ) );

    // Now send the Deactivate command, which should stop the collection
    LastKnownStateCommandRequest deactivateCommand;
    deactivateCommand.commandID = "command2";
    deactivateCommand.stateTemplateID = "lks1";
    deactivateCommand.operation = LastKnownStateOperation::DEACTIVATE;
    mLastKnownStateWorkerThread->onNewCommandReceived( deactivateCommand );

    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command2" );
    ASSERT_EQ( commandResponse->status, CommandStatus::SUCCEEDED );

    // Send a FetchSnapshot command, which should send a snapshot even if the state template is deactivated
    LastKnownStateCommandRequest fetchSnapshotCommand;
    fetchSnapshotCommand.commandID = "command3";
    fetchSnapshotCommand.stateTemplateID = "lks1";
    fetchSnapshotCommand.operation = LastKnownStateOperation::FETCH_SNAPSHOT;
    mLastKnownStateWorkerThread->onNewCommandReceived( fetchSnapshotCommand );

    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command3" );
    ASSERT_EQ( commandResponse->status, CommandStatus::SUCCEEDED );

    WAIT_ASSERT_TRUE( popCollectedData( collectedData ) );

    // Now that the deactivate command was processed, this signal should not be collected
    mSignalBufferPtr->push( CollectedDataFrame(
        CollectedSignalsGroup{ CollectedSignal( 1, mClock->systemTimeSinceEpochMs(), 10, SignalType::DOUBLE ) } ) );
    DELAY_ASSERT_TRUE( mCollectedSignals->isEmpty() );
}

TEST_F( LastKnownStateWorkerDoubleTest, ActivateWithAutoDeactivate )
{
    mLastKnownStateWorkerThread = std::make_unique<LastKnownStateWorkerThread>(
        mSignalBufferPtr, mCollectedSignals, std::move( mLastKnownStateInspector ), 50 );
    ASSERT_TRUE( mLastKnownStateWorkerThread->start() );
    WAIT_ASSERT_TRUE( mLastKnownStateWorkerThread->isAlive() );

    auto stateTemplateInfo = std::make_shared<StateTemplateInformation>( StateTemplateInformation{
        "lks1", "decoder1", { LastKnownStateSignalInformation{ 1 } }, LastKnownStateUpdateStrategy::PERIODIC, 800 } );

    mLastKnownStateWorkerThread->onStateTemplatesChanged(
        std::make_shared<StateTemplateList>( StateTemplateList{ stateTemplateInfo } ) );

    mSignalBufferPtr->push( CollectedDataFrame(
        CollectedSignalsGroup{ CollectedSignal( 1, mClock->systemTimeSinceEpochMs(), 10, SignalType::DOUBLE ) } ) );

    std::shared_ptr<const CommandResponse> commandResponse;

    // Now send the Activate command, which should trigger a collection
    LastKnownStateCommandRequest activateCommand;
    activateCommand.commandID = "command1";
    activateCommand.stateTemplateID = "lks1";
    activateCommand.operation = LastKnownStateOperation::ACTIVATE;
    activateCommand.receivedTime = mClock->timeSinceEpoch();
    // Let data to be collected three times (snapshot + 2 periods) and on the fourth time it should be deactivated.
    activateCommand.deactivateAfterSeconds = 2;
    mLastKnownStateWorkerThread->onNewCommandReceived( activateCommand );

    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command1" );
    ASSERT_EQ( commandResponse->status, CommandStatus::SUCCEEDED );

    std::shared_ptr<const LastKnownStateCollectedData> collectedData;

    // First should be a snapshot, then the next two should be the periodic updates, then after that
    // auto deactivate should be triggered.
    WAIT_ASSERT_TRUE( popCollectedData( collectedData ) );

    mSignalBufferPtr->push( CollectedDataFrame(
        CollectedSignalsGroup{ CollectedSignal( 1, mClock->systemTimeSinceEpochMs(), 10, SignalType::DOUBLE ) } ) );
    WAIT_ASSERT_TRUE( popCollectedData( collectedData ) );

    mSignalBufferPtr->push( CollectedDataFrame(
        CollectedSignalsGroup{ CollectedSignal( 1, mClock->systemTimeSinceEpochMs(), 10, SignalType::DOUBLE ) } ) );
    WAIT_ASSERT_TRUE( popCollectedData( collectedData ) );

    // Now the auto deactivate time should have passed, so this signal should not be collected
    mSignalBufferPtr->push( CollectedDataFrame(
        CollectedSignalsGroup{ CollectedSignal( 1, mClock->systemTimeSinceEpochMs(), 10, SignalType::DOUBLE ) } ) );
    DELAY_ASSERT_TRUE( mCollectedSignals->isEmpty() );
}

TEST_F( LastKnownStateWorkerDoubleTest, SendErrorResponseWhenActivatingMissingStateTemplate )
{
    mLastKnownStateWorkerThread = std::make_unique<LastKnownStateWorkerThread>(
        mSignalBufferPtr, mCollectedSignals, std::move( mLastKnownStateInspector ), 0 );
    ASSERT_TRUE( mLastKnownStateWorkerThread->start() );
    WAIT_ASSERT_TRUE( mLastKnownStateWorkerThread->isAlive() );

    auto stateTemplateInfo = std::make_shared<StateTemplateInformation>( StateTemplateInformation{
        "lks1", "decoder1", { LastKnownStateSignalInformation{ 1 } }, LastKnownStateUpdateStrategy::PERIODIC, 2000 } );

    mLastKnownStateWorkerThread->onStateTemplatesChanged(
        std::make_shared<StateTemplateList>( StateTemplateList{ stateTemplateInfo } ) );

    std::shared_ptr<const CommandResponse> commandResponse;

    // Now send the Activate command, which should trigger a collection
    LastKnownStateCommandRequest activateCommand;
    activateCommand.commandID = "command1";
    activateCommand.stateTemplateID = "invalid_lks";
    activateCommand.operation = LastKnownStateOperation::ACTIVATE;
    activateCommand.receivedTime = mClock->timeSinceEpoch();
    mLastKnownStateWorkerThread->onNewCommandReceived( activateCommand );

    WAIT_ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command1" );
    ASSERT_EQ( commandResponse->status, CommandStatus::EXECUTION_FAILED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_STATE_TEMPLATE_OUT_OF_SYNC );
    DELAY_ASSERT_TRUE( mCollectedSignals->isEmpty() );
}

TEST_F( LastKnownStateWorkerDoubleTest, DataReadyWithoutStateTemplate )
{
    mLastKnownStateWorkerThread = std::make_unique<LastKnownStateWorkerThread>(
        mSignalBufferPtr, mCollectedSignals, std::move( mLastKnownStateInspector ), 0 );
    mSignalBufferPtr->subscribeToNewDataAvailable(
        std::bind( &LastKnownStateWorkerThread::onNewDataAvailable, mLastKnownStateWorkerThread.get() ) );
    ASSERT_TRUE( mLastKnownStateWorkerThread->start() );

    Timestamp timestamp = mClock->systemTimeSinceEpochMs();

    mSignalBufferPtr->push( CollectedDataFrame( { CollectedSignal( 1234, timestamp, 0.1, SignalType::DOUBLE ) } ) );

    // The input signal buffer should be consumed even when there is no state template because otherwise it will become
    // full and new data will be discarded.
    WAIT_ASSERT_TRUE( mSignalBufferPtr->isEmpty() );
    ASSERT_TRUE( mCollectedSignals->isEmpty() );

    // Make sure that even after the first iteration, the worker continues consuming data that is arriving
    mSignalBufferPtr->push( CollectedDataFrame( { CollectedSignal( 5678, timestamp, 0.1, SignalType::DOUBLE ) } ) );

    WAIT_ASSERT_TRUE( mSignalBufferPtr->isEmpty() );
    ASSERT_TRUE( mCollectedSignals->isEmpty() );
}

TEST_F( LastKnownStateWorkerDoubleTest, EmptyStateTemplate )
{
    mLastKnownStateWorkerThread = std::make_unique<LastKnownStateWorkerThread>(
        mSignalBufferPtr, mCollectedSignals, std::move( mLastKnownStateInspector ), 0 );
    ASSERT_TRUE( mLastKnownStateWorkerThread->start() );

    WAIT_ASSERT_TRUE( mLastKnownStateWorkerThread->isAlive() );

    mLastKnownStateWorkerThread->onStateTemplatesChanged( std::make_shared<StateTemplateList>( StateTemplateList{} ) );

    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    ASSERT_TRUE( mCollectedSignals->isEmpty() );
}

TEST_F( LastKnownStateWorkerDoubleTest, DtcAndCanRawFrame )
{
    mLastKnownStateWorkerThread = std::make_unique<LastKnownStateWorkerThread>(
        mSignalBufferPtr, mCollectedSignals, std::move( mLastKnownStateInspector ), 1000 );
    ASSERT_TRUE( mLastKnownStateWorkerThread->start() );

    WAIT_ASSERT_TRUE( mLastKnownStateWorkerThread->isAlive() );

    auto stateTemplateInfo = std::make_shared<StateTemplateInformation>( StateTemplateInformation{
        "lks1", "decoder1", { LastKnownStateSignalInformation{ 1 } }, LastKnownStateUpdateStrategy::PERIODIC, 2000 } );

    mLastKnownStateWorkerThread->onStateTemplatesChanged(
        std::make_shared<StateTemplateList>( StateTemplateList{ stateTemplateInfo } ) );

    CollectedSignalsGroup collectedSignalsGroup;
    mSignalBufferPtr->push( CollectedDataFrame( collectedSignalsGroup ) );

    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
    ASSERT_TRUE( mCollectedSignals->isEmpty() );
}

} // namespace IoTFleetWise
} // namespace Aws
