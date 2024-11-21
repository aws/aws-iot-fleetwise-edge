// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LastKnownStateInspector.h"
#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "CommandTypes.h"
#include "DataSenderTypes.h"
#include "ICommandDispatcher.h"
#include "LastKnownStateTypes.h"
#include "QueueTypes.h"
#include "SignalTypes.h"
#include "Testing.h"
#include "TimeTypes.h"
#include <algorithm> // IWYU pragma: keep
#include <cstdint>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

using signalTypes =
    ::testing::Types<uint8_t, int8_t, uint16_t, int16_t, uint32_t, int32_t, uint64_t, int64_t, float, double, bool>;

template <typename T>
class LastKnownStateInspectorTest : public ::testing::Test
{
protected:
    LastKnownStateInspectorTest()
        : mCommandResponses( std::make_shared<DataSenderQueue>( 100, "Command Responses" ) )
    {
    }

    void
    SetUp() override
    {
        mStateTemplatePeriodic1000ms = std::make_shared<const StateTemplateInformation>(
            StateTemplateInformation{ "stateTemplate1",
                                      "decoder",
                                      { LastKnownStateSignalInformation{ SIG_D, SignalType::DOUBLE } },
                                      LastKnownStateUpdateStrategy::PERIODIC,
                                      1000 } );
        mStateTemplatePeriodic400ms = std::make_shared<const StateTemplateInformation>(
            StateTemplateInformation{ "stateTemplate2",
                                      "decoder",
                                      { LastKnownStateSignalInformation{ SIG_E, SignalType::DOUBLE } },
                                      LastKnownStateUpdateStrategy::PERIODIC,
                                      400 } );
        mStateTemplateOnChange = std::make_shared<const StateTemplateInformation>(
            StateTemplateInformation{ "stateTemplate3",
                                      "decoder",
                                      { LastKnownStateSignalInformation{ SIG_A, SignalType::DOUBLE },
                                        LastKnownStateSignalInformation{ SIG_B, SignalType::INT64 } },
                                      LastKnownStateUpdateStrategy::ON_CHANGE } );
    }

    void
    TearDown() override
    {
    }

    bool
    popCommandResponse( std::shared_ptr<const CommandResponse> &commandResponse )
    {
        std::shared_ptr<const DataToSend> senderData;
        auto succeeded = mCommandResponses->pop( senderData );
        commandResponse = std::dynamic_pointer_cast<const CommandResponse>( senderData );
        return succeeded;
    }

    /**
     * Collects the next data sorting all elements and all signals in a predictable way
     */
    std::shared_ptr<LastKnownStateCollectedData>
    collectNextDataToSendSorted( LastKnownStateInspector &inspector, const TimePoint &currentTime )
    {
        auto dataToSend = inspector.collectNextDataToSend( currentTime );
        if ( dataToSend == nullptr )
        {
            return nullptr;
        }

        // Make a copy of everything to get rid of the const qualifier
        std::shared_ptr<LastKnownStateCollectedData> dataToSendSorted =
            std::make_shared<LastKnownStateCollectedData>( *dataToSend );

        std::sort( dataToSendSorted->stateTemplateCollectedSignals.begin(),
                   dataToSendSorted->stateTemplateCollectedSignals.end(),
                   []( StateTemplateCollectedSignals &a, StateTemplateCollectedSignals &b ) {
                       return a.stateTemplateSyncId < b.stateTemplateSyncId;
                   } );

        for ( auto &collectedData : dataToSendSorted->stateTemplateCollectedSignals )
        {
            std::sort( collectedData.signals.begin(),
                       collectedData.signals.end(),
                       []( CollectedSignal &a, CollectedSignal &b ) {
                           return a.signalID < b.signalID;
                       } );
        }

        return dataToSendSorted;
    }

    const SignalID SIG_A = 1;
    const SignalID SIG_B = 2;
    const SignalID SIG_D = 4;
    const SignalID SIG_E = 5;
    const SignalID SIG_F = 6;
    const SignalID SIG_G = 7;
    std::shared_ptr<const StateTemplateInformation> mStateTemplatePeriodic1000ms;
    std::shared_ptr<const StateTemplateInformation> mStateTemplatePeriodic400ms;
    std::shared_ptr<const StateTemplateInformation> mStateTemplateOnChange;
    std::shared_ptr<DataSenderQueue> mCommandResponses;
    std::map<SignalID, CollectedSignal> mOutputSignalMap;
};

TYPED_TEST_SUITE( LastKnownStateInspectorTest, signalTypes );

TYPED_TEST( LastKnownStateInspectorTest, inspectTwoSameSignalValues )
{
    const SignalID SIG_C = 3;
    std::vector<LastKnownStateSignalInformation> signalsToInspect;
    LastKnownStateInspector inspector( this->mCommandResponses, nullptr );
    signalsToInspect.emplace_back( LastKnownStateSignalInformation{ SIG_C, getSignalType<TypeParam>() } );
    auto stateTemplate = std::make_shared<const StateTemplateInformation>( StateTemplateInformation{
        "stateTemplate1", "decoder", signalsToInspect, LastKnownStateUpdateStrategy::ON_CHANGE } );

    inspector.onStateTemplatesChanged( std::make_shared<StateTemplateList>( StateTemplateList{ stateTemplate } ) );
    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command1", stateTemplate->id, LastKnownStateOperation::ACTIVATE } );

    inspector.inspectNewSignal<TypeParam>( SIG_C, TimePoint{ 100, 1000 }, 1.0 );
    auto dataToSend = this->collectNextDataToSendSorted( inspector, TimePoint{ 110, 1100 } );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->triggerTime, 110 );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    auto inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_C );
    ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 100 );
    ASSERT_EQ( inspectionOutput.signals[0].getType(), getSignalType<TypeParam>() );
    ASSERT_EQ( inspectionOutput.stateTemplateSyncId, "stateTemplate1" );
    ASSERT_NO_FATAL_FAILURE(
        assertSignalValue( inspectionOutput.signals[0].getValue(), 1, getSignalType<TypeParam>() ) );
    inspector.inspectNewSignal<TypeParam>( SIG_C, TimePoint(), 1.0 );
    ASSERT_EQ( this->collectNextDataToSendSorted( inspector, TimePoint() ), nullptr );
}

TYPED_TEST( LastKnownStateInspectorTest, inspectTwoDifferentSignalValues )
{
    const SignalID SIG_C = 3;
    std::vector<LastKnownStateSignalInformation> signalsToInspect;
    LastKnownStateInspector inspector( this->mCommandResponses, nullptr );
    signalsToInspect.emplace_back( LastKnownStateSignalInformation{ SIG_C, getSignalType<TypeParam>() } );
    auto stateTemplate = std::make_shared<const StateTemplateInformation>( StateTemplateInformation{
        "stateTemplate1", "decoder", signalsToInspect, LastKnownStateUpdateStrategy::ON_CHANGE } );

    inspector.onStateTemplatesChanged( std::make_shared<StateTemplateList>( StateTemplateList{ stateTemplate } ) );

    inspector.inspectNewSignal<TypeParam>( SIG_C, TimePoint(), 2.0 );
    auto dataToSend = this->collectNextDataToSendSorted( inspector, TimePoint() );
    // Before the state template is activate, nothing should be collected
    ASSERT_EQ( dataToSend, nullptr );

    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command1", stateTemplate->id, LastKnownStateOperation::ACTIVATE } );

    inspector.inspectNewSignal<TypeParam>( SIG_C, TimePoint(), 1.0 );
    dataToSend = this->collectNextDataToSendSorted( inspector, TimePoint() );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    auto inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_C );
    ASSERT_EQ( inspectionOutput.signals[0].getType(), getSignalType<TypeParam>() );
    ASSERT_NO_FATAL_FAILURE(
        assertSignalValue( inspectionOutput.signals[0].getValue(), 1, getSignalType<TypeParam>() ) );

    inspector.inspectNewSignal<TypeParam>( SIG_C, TimePoint(), 0.0 );
    dataToSend = this->collectNextDataToSendSorted( inspector, TimePoint() );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_C );
    ASSERT_EQ( inspectionOutput.signals[0].getType(), getSignalType<TypeParam>() );
    ASSERT_NO_FATAL_FAILURE(
        assertSignalValue( inspectionOutput.signals[0].getValue(), 0, getSignalType<TypeParam>() ) );
}

class LastKnownStateInspectorDoubleTest : public LastKnownStateInspectorTest<double>
{
protected:
    LastKnownStateInspectorDoubleTest()
        : inspector( mCommandResponses, nullptr )
    {
    }

    LastKnownStateInspector inspector;
};

TEST_F( LastKnownStateInspectorDoubleTest, inspectFirstTimeReceivedSignal )
{
    inspector.onStateTemplatesChanged(
        std::make_shared<StateTemplateList>( StateTemplateList{ mStateTemplateOnChange } ) );
    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command1", mStateTemplateOnChange->id, LastKnownStateOperation::ACTIVATE } );
    inspector.inspectNewSignal<double>( SIG_A, TimePoint(), 1.0 );
    auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint() );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    auto inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_A );
    ASSERT_EQ( inspectionOutput.signals[0].getType(), SignalType::DOUBLE );
    ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 1.0 );
}

TEST_F( LastKnownStateInspectorDoubleTest, inspectTwoSignalsWithSameValue )
{
    inspector.onStateTemplatesChanged(
        std::make_shared<StateTemplateList>( StateTemplateList{ mStateTemplateOnChange } ) );
    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command1", mStateTemplateOnChange->id, LastKnownStateOperation::ACTIVATE } );
    inspector.inspectNewSignal<double>( SIG_A, TimePoint(), 1.0 );
    auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint() );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    auto inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_A );
    ASSERT_EQ( inspectionOutput.signals[0].getType(), SignalType::DOUBLE );
    ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 1.0 );

    inspector.inspectNewSignal<double>( SIG_A, TimePoint(), 1.0 );
    ASSERT_EQ( collectNextDataToSendSorted( inspector, TimePoint() ), nullptr );

    // Although the value is different than previous value but it's still within the comparison threshold
    inspector.inspectNewSignal<double>( SIG_A, TimePoint(), 1.0009 );
    ASSERT_EQ( collectNextDataToSendSorted( inspector, TimePoint() ), nullptr );
}

TEST_F( LastKnownStateInspectorDoubleTest, inspectSignalWithDifferentValue )
{
    auto stateTemplate4 = std::make_shared<const StateTemplateInformation>(
        StateTemplateInformation{ "stateTemplate4",
                                  "decoder",
                                  { LastKnownStateSignalInformation{ SIG_F, SignalType::DOUBLE } },
                                  LastKnownStateUpdateStrategy::ON_CHANGE } );
    inspector.onStateTemplatesChanged(
        std::make_shared<StateTemplateList>( StateTemplateList{ mStateTemplateOnChange, stateTemplate4 } ) );

    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command1", mStateTemplateOnChange->id, LastKnownStateOperation::ACTIVATE } );
    inspector.inspectNewSignal<double>( SIG_A, TimePoint(), 1.0 );
    inspector.inspectNewSignal<double>( SIG_F, TimePoint(), 10.0 );
    auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint() );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    auto inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_A );
    ASSERT_EQ( inspectionOutput.signals[0].getType(), SignalType::DOUBLE );
    ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 1.0 );

    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command2", stateTemplate4->id, LastKnownStateOperation::ACTIVATE } );

    inspector.inspectNewSignal<double>( SIG_A, TimePoint(), 1.002 );
    inspector.inspectNewSignal<double>( SIG_F, TimePoint(), 11.0 );
    dataToSend = collectNextDataToSendSorted( inspector, TimePoint() );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 2 );

    inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_A );
    ASSERT_EQ( inspectionOutput.signals[0].getType(), SignalType::DOUBLE );
    ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 1.002 );

    inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 1 );

    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_F );
    ASSERT_EQ( inspectionOutput.signals[0].getType(), SignalType::DOUBLE );
    ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 11.0 );

    // Now deactivate only one template and ensure its signal is not collected
    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command3", mStateTemplateOnChange->id, LastKnownStateOperation::DEACTIVATE } );

    inspector.inspectNewSignal<double>( SIG_A, TimePoint(), 2.0 );
    inspector.inspectNewSignal<double>( SIG_F, TimePoint(), 12.0 );
    dataToSend = collectNextDataToSendSorted( inspector, TimePoint() );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );

    inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_F );
    ASSERT_EQ( inspectionOutput.signals[0].getType(), SignalType::DOUBLE );
    ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 12.0 );

    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command4", stateTemplate4->id, LastKnownStateOperation::DEACTIVATE } );

    inspector.inspectNewSignal<double>( SIG_A, TimePoint(), 3.0 );
    inspector.inspectNewSignal<double>( SIG_F, TimePoint(), 13.0 );
    dataToSend = collectNextDataToSendSorted( inspector, TimePoint() );
    ASSERT_EQ( dataToSend, nullptr );
}

TEST_F( LastKnownStateInspectorDoubleTest, withNoLksInspectionMatrix )
{
    inspector.inspectNewSignal<double>( SIG_B, TimePoint(), 0xAA55AA55 );
    ASSERT_EQ( collectNextDataToSendSorted( inspector, TimePoint() ), nullptr );
}

TEST_F( LastKnownStateInspectorDoubleTest, activateStateTemplateMultipleTimesWithAutoDeactivate )
{
    inspector.onStateTemplatesChanged(
        std::make_shared<StateTemplateList>( StateTemplateList{ mStateTemplatePeriodic1000ms } ) );

    inspector.onNewCommandReceived( LastKnownStateCommandRequest{
        "command1",
        mStateTemplatePeriodic1000ms->id,
        LastKnownStateOperation::ACTIVATE,
        5,                 // deactivateAfterSeconds
        TimePoint{ 0, 0 }, // receivedTime
    } );

    std::shared_ptr<const CommandResponse> commandResponse;

    ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command1" );
    ASSERT_EQ( commandResponse->status, CommandStatus::SUCCEEDED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_UNSPECIFIED );
    ASSERT_EQ( commandResponse->reasonDescription, "" );

    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 500, 500 }, 1.0 );

    {
        auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 1000, 1000 } );
        ASSERT_NE( dataToSend, nullptr );
        ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    }

    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 1100, 1100 }, 2.0 );
    // State template should have been deactivated already
    ASSERT_EQ( collectNextDataToSendSorted( inspector, TimePoint{ 5001, 5001 } ), nullptr );

    // Re-activate the state template
    inspector.onNewCommandReceived( LastKnownStateCommandRequest{
        "command2",
        mStateTemplatePeriodic1000ms->id,
        LastKnownStateOperation::ACTIVATE,
        5,                       // deactivateAfterSeconds
        TimePoint{ 5000, 5000 }, // receivedTime
    } );

    ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command2" );
    ASSERT_EQ( commandResponse->status, CommandStatus::SUCCEEDED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_UNSPECIFIED );
    ASSERT_EQ( commandResponse->reasonDescription, "" );

    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 8000, 8000 }, 1.0 );

    {
        auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 10000, 10000 } );
        ASSERT_NE( dataToSend, nullptr );
        ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    }

    // Send another Activate command for a state template that is already activated. This should
    // reset the deactivateAfterSeconds config.
    inspector.onNewCommandReceived( LastKnownStateCommandRequest{
        "command3",
        mStateTemplatePeriodic1000ms->id,
        LastKnownStateOperation::ACTIVATE,
        3,                       // deactivateAfterSeconds
        TimePoint{ 9000, 9000 }, // state template activation should be extended until 12000
    } );

    ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command3" );
    ASSERT_EQ( commandResponse->status, CommandStatus::SUCCEEDED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_STATE_TEMPLATE_ALREADY_ACTIVATED );
    ASSERT_EQ( commandResponse->reasonDescription, REASON_DESCRIPTION_STATE_TEMPLATE_ALREADY_ACTIVATED );

    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 9000, 9000 }, 1.0 );

    {
        auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 10001, 10001 } );
        ASSERT_NE( dataToSend, nullptr );
        ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    }

    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 11000, 11000 }, 1.0 );

    {
        auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 12000, 12000 } );
        ASSERT_NE( dataToSend, nullptr );
        ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    }

    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 11500, 11500 }, 1.0 );
    // State template should have been deactivated already
    ASSERT_EQ( collectNextDataToSendSorted( inspector, TimePoint{ 12001, 12001 } ), nullptr );
}

TEST_F( LastKnownStateInspectorDoubleTest, activateStateTemplateMultipleTimesWithoutAutoDeactivate )
{
    inspector.onStateTemplatesChanged(
        std::make_shared<StateTemplateList>( StateTemplateList{ mStateTemplatePeriodic1000ms } ) );

    inspector.onNewCommandReceived( LastKnownStateCommandRequest{
        "command1",
        mStateTemplatePeriodic1000ms->id,
        LastKnownStateOperation::ACTIVATE,
        5,                 // deactivateAfterSeconds
        TimePoint{ 0, 0 }, // receivedTime
    } );

    std::shared_ptr<const CommandResponse> commandResponse;

    ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command1" );
    ASSERT_EQ( commandResponse->status, CommandStatus::SUCCEEDED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_UNSPECIFIED );
    ASSERT_EQ( commandResponse->reasonDescription, "" );

    // Send another command, but without deactivateAfterSeconds.
    // This should clear the deactivateAfterSeconds set by previous command.
    inspector.onNewCommandReceived( LastKnownStateCommandRequest{
        "command2",
        mStateTemplatePeriodic1000ms->id,
        LastKnownStateOperation::ACTIVATE,
        0,                       // deactivateAfterSeconds
        TimePoint{ 4000, 4000 }, // receivedTime
    } );

    ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command2" );
    ASSERT_EQ( commandResponse->status, CommandStatus::SUCCEEDED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_STATE_TEMPLATE_ALREADY_ACTIVATED );
    ASSERT_EQ( commandResponse->reasonDescription, REASON_DESCRIPTION_STATE_TEMPLATE_ALREADY_ACTIVATED );

    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 4500, 4500 }, 1.0 );

    {
        // Auto-deactivate shouldn't have kicked in.
        auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 5001, 5001 } );
        ASSERT_NE( dataToSend, nullptr );
        ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    }

    // Now set deactivateAfterSeconds again, which should replace the missing deactivateAfterSeconds
    // from previous command.
    inspector.onNewCommandReceived( LastKnownStateCommandRequest{
        "command3",
        mStateTemplatePeriodic1000ms->id,
        LastKnownStateOperation::ACTIVATE,
        3,                       // deactivateAfterSeconds
        TimePoint{ 6000, 6000 }, // state template activation should be extended until 9000
    } );

    ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command3" );
    ASSERT_EQ( commandResponse->status, CommandStatus::SUCCEEDED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_STATE_TEMPLATE_ALREADY_ACTIVATED );
    ASSERT_EQ( commandResponse->reasonDescription, REASON_DESCRIPTION_STATE_TEMPLATE_ALREADY_ACTIVATED );

    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 7000, 7000 }, 1.0 );

    {
        auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 9000, 9000 } );
        ASSERT_NE( dataToSend, nullptr );
        ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    }

    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 8500, 8500 }, 2.0 );
    // State template should have been deactivated already
    ASSERT_EQ( collectNextDataToSendSorted( inspector, TimePoint{ 9001, 9001 } ), nullptr );
}

TEST_F( LastKnownStateInspectorDoubleTest, keepActivationStatusOnStateTemplatesUpdate )
{
    inspector.onStateTemplatesChanged(
        std::make_shared<StateTemplateList>( StateTemplateList{ mStateTemplatePeriodic1000ms } ) );

    inspector.onNewCommandReceived( LastKnownStateCommandRequest{
        "command1",
        mStateTemplatePeriodic1000ms->id,
        LastKnownStateOperation::ACTIVATE,
        5,                 // deactivateAfterSeconds
        TimePoint{ 0, 0 }, // receivedTime
    } );

    std::shared_ptr<const CommandResponse> commandResponse;

    ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command1" );
    ASSERT_EQ( commandResponse->status, CommandStatus::SUCCEEDED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_UNSPECIFIED );
    ASSERT_EQ( commandResponse->reasonDescription, "" );

    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 500, 500 }, 1.0 );

    {
        auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 800, 800 } );
        ASSERT_NE( dataToSend, nullptr );
        ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    }

    inspector.onStateTemplatesChanged( std::make_shared<StateTemplateList>(
        StateTemplateList{ mStateTemplatePeriodic1000ms, mStateTemplatePeriodic400ms } ) );

    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 4500, 4500 }, 1.0 );

    {
        // The initial template should be still activated. The inspector shouldn't clear all
        // existing state when the list of state templates is updated.
        auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 5000, 5000 } );
        ASSERT_NE( dataToSend, nullptr );
        ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    }

    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 4900, 4900 }, 2.0 );
    // State template should have been already deactivated due to deactivateAfterSeconds config
    ASSERT_EQ( collectNextDataToSendSorted( inspector, TimePoint{ 5001, 5001 } ), nullptr );
}

TEST_F( LastKnownStateInspectorDoubleTest, deactivateAlreadyDeactivatedStateTemplate )
{
    inspector.onStateTemplatesChanged(
        std::make_shared<StateTemplateList>( StateTemplateList{ mStateTemplatePeriodic1000ms } ) );

    inspector.onNewCommandReceived( LastKnownStateCommandRequest{
        "command1", mStateTemplatePeriodic1000ms->id, LastKnownStateOperation::DEACTIVATE } );

    std::shared_ptr<const CommandResponse> commandResponse;

    ASSERT_TRUE( popCommandResponse( commandResponse ) );
    ASSERT_EQ( commandResponse->id, "command1" );
    ASSERT_EQ( commandResponse->status, CommandStatus::SUCCEEDED );
    ASSERT_EQ( commandResponse->reasonCode, REASON_CODE_STATE_TEMPLATE_ALREADY_DEACTIVATED );
    ASSERT_EQ( commandResponse->reasonDescription, REASON_DESCRIPTION_STATE_TEMPLATE_ALREADY_DEACTIVATED );

    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 1000, 1000 }, 1.0 );
    ASSERT_EQ( collectNextDataToSendSorted( inspector, TimePoint{ 2000, 2000 } ), nullptr );
}

TEST_F( LastKnownStateInspectorDoubleTest, persistActivatedStateTemplate )
{
    auto clock = ClockHandler::getClock();
    auto initialTime = clock->timeSinceEpoch();
    auto persistency = createCacheAndPersist();
    {
        LastKnownStateInspector inspector( mCommandResponses, persistency );
        inspector.onStateTemplatesChanged(
            std::make_shared<StateTemplateList>( StateTemplateList{ mStateTemplatePeriodic1000ms } ) );

        inspector.onNewCommandReceived( LastKnownStateCommandRequest{
            "command1",
            mStateTemplatePeriodic1000ms->id,
            LastKnownStateOperation::ACTIVATE,
            5,           // deactivateAfterSeconds
            initialTime, // receivedTime
        } );

        std::shared_ptr<const CommandResponse> commandResponse;
        ASSERT_TRUE( popCommandResponse( commandResponse ) );
        ASSERT_EQ( commandResponse->id, "command1" );

        inspector.inspectNewSignal<double>( SIG_D, initialTime + 500, 1.0 );

        {
            auto dataToSend = collectNextDataToSendSorted( inspector, initialTime + 1000 );
            ASSERT_NE( dataToSend, nullptr );
            ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
        }
    }

    {
        // Create a new inspector, which should load the persisted metadata from the previous instance
        // and thus set the state template as activated.
        LastKnownStateInspector inspector( mCommandResponses, persistency );
        inspector.onStateTemplatesChanged(
            std::make_shared<StateTemplateList>( StateTemplateList{ mStateTemplatePeriodic1000ms } ) );

        inspector.inspectNewSignal<double>( SIG_D, initialTime + 1100, 2.0 );

        {
            auto dataToSend = collectNextDataToSendSorted( inspector, initialTime + 1200 );
            ASSERT_NE( dataToSend, nullptr );
            ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
        }

        inspector.inspectNewSignal<double>( SIG_D, initialTime + 2000, 3.0 );
        // State template should have been deactivated already
        ASSERT_EQ( collectNextDataToSendSorted( inspector, initialTime + 6000 ), nullptr );
    }

    {
        // Create a new inspector, but since the last time the state template was deactivate, this
        // new instance should not activate the state template.
        LastKnownStateInspector inspector( mCommandResponses, persistency );
        inspector.onStateTemplatesChanged(
            std::make_shared<StateTemplateList>( StateTemplateList{ mStateTemplatePeriodic1000ms } ) );

        inspector.inspectNewSignal<double>( SIG_D, initialTime + 6500, 4.0 );

        ASSERT_EQ( collectNextDataToSendSorted( inspector, initialTime + 7000 ), nullptr );
    }
}

/**
 * Here's the test scenarios:
 * signal D (period: 1s)       |       |
 * signal E (period: 0.4s)       | |   |
 * Inspection                        |    |      |      |
 */
TEST_F( LastKnownStateInspectorDoubleTest, inspectSignalPeriodically )
{
    auto stateTemplate4 = std::make_shared<const StateTemplateInformation>(
        StateTemplateInformation{ "stateTemplate4",
                                  "decoder",
                                  { LastKnownStateSignalInformation{ SIG_F, SignalType::DOUBLE },
                                    LastKnownStateSignalInformation{ SIG_G, SignalType::DOUBLE } },
                                  LastKnownStateUpdateStrategy::PERIODIC,
                                  500 } );
    inspector.onStateTemplatesChanged( std::make_shared<StateTemplateList>(
        StateTemplateList{ mStateTemplatePeriodic1000ms, mStateTemplatePeriodic400ms, stateTemplate4 } ) );

    // Activate only two templates
    inspector.onNewCommandReceived( LastKnownStateCommandRequest{
        "command1", mStateTemplatePeriodic1000ms->id, LastKnownStateOperation::ACTIVATE } );
    inspector.onNewCommandReceived( LastKnownStateCommandRequest{
        "command2", mStateTemplatePeriodic400ms->id, LastKnownStateOperation::ACTIVATE } );
    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 500, 500 }, 1.0 );
    inspector.inspectNewSignal<double>( SIG_E, TimePoint{ 800, 800 }, 11.0 );
    inspector.inspectNewSignal<double>( SIG_E, TimePoint{ 900, 900 }, 12.0 );

    {
        auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 1000, 1000 } );
        ASSERT_NE( dataToSend, nullptr );
        ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 2 );

        {
            auto inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

            ASSERT_EQ( inspectionOutput.stateTemplateSyncId, "stateTemplate1" );
            ASSERT_EQ( inspectionOutput.signals.size(), 1 );
            ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_D );
            ASSERT_EQ( inspectionOutput.signals[0].getType(), SignalType::DOUBLE );
            ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 1.0 );
            ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 500 );
        }

        {
            auto inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 1 );

            ASSERT_EQ( inspectionOutput.stateTemplateSyncId, "stateTemplate2" );
            ASSERT_EQ( inspectionOutput.signals.size(), 1 );
            ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_E );
            ASSERT_EQ( inspectionOutput.signals[0].getType(), SignalType::DOUBLE );
            ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 12.0 );
            ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 900 );
        }
    }

    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 1100, 1100 }, 2.0 );
    inspector.inspectNewSignal<double>( SIG_E, TimePoint{ 1100, 1100 }, 13.0 );
    // Last trigger time for SIG_E is 1000, update period is set as 400, hence we should not trigger update this time
    ASSERT_EQ( collectNextDataToSendSorted( inspector, TimePoint{ 1300, 1300 } ), nullptr );

    // Activate the other template
    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command3", stateTemplate4->id, LastKnownStateOperation::ACTIVATE } );
    inspector.inspectNewSignal<double>( SIG_F, TimePoint{ 1000, 1000 }, 100.0 );
    inspector.inspectNewSignal<double>( SIG_G, TimePoint{ 1100, 1100 }, 110.0 );

    {
        // Here we shall expect the update for signal E
        auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 1400, 1400 } );
        ASSERT_NE( dataToSend, nullptr );
        ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 2 );

        {
            auto inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

            ASSERT_EQ( inspectionOutput.stateTemplateSyncId, "stateTemplate2" );
            ASSERT_EQ( inspectionOutput.signals.size(), 1 );
            ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_E );
            ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 1100 );
            ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 13.0 );
        }

        {
            // For the second template, we should expect a snapshot with both signals
            auto inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 1 );

            ASSERT_EQ( inspectionOutput.stateTemplateSyncId, "stateTemplate4" );
            ASSERT_EQ( inspectionOutput.signals.size(), 2 );
            ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_F );
            ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 1000 );
            ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 100.0 );
            ASSERT_EQ( inspectionOutput.signals[1].signalID, SIG_G );
            ASSERT_EQ( inspectionOutput.signals[1].receiveTime, 1100 );
            ASSERT_EQ( inspectionOutput.signals[1].getValue().value.doubleVal, 110.0 );
        }
    }

    inspector.inspectNewSignal<double>( SIG_F, TimePoint{ 1420, 1420 }, 101.0 );
    inspector.inspectNewSignal<double>( SIG_G, TimePoint{ 1450, 1450 }, 111.0 );

    {
        auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 1900, 1900 } );
        ASSERT_NE( dataToSend, nullptr );
        ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );

        auto inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

        ASSERT_EQ( inspectionOutput.stateTemplateSyncId, "stateTemplate4" );
        ASSERT_EQ( inspectionOutput.signals.size(), 2 );
        ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_F );
        ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 1420 );
        ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 101.0 );
        ASSERT_EQ( inspectionOutput.signals[1].signalID, SIG_G );
        ASSERT_EQ( inspectionOutput.signals[1].receiveTime, 1450 );
        ASSERT_EQ( inspectionOutput.signals[1].getValue().value.doubleVal, 111.0 );
    }

    {
        auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 2000, 2000 } );
        ASSERT_NE( dataToSend, nullptr );
        ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );

        auto inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

        ASSERT_EQ( inspectionOutput.stateTemplateSyncId, "stateTemplate1" );
        ASSERT_EQ( inspectionOutput.signals.size(), 1 );
        ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_D );
        ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 1100 );
        ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 2.0 );
    }

    // Now ensure that after deactivating a template, no signal from that template is collected
    inspector.onNewCommandReceived( LastKnownStateCommandRequest{
        "command3", mStateTemplatePeriodic1000ms->id, LastKnownStateOperation::DEACTIVATE } );
    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 2600, 2600 }, 3.0 );
    inspector.inspectNewSignal<double>( SIG_F, TimePoint{ 2650, 2650 }, 102.0 );

    {
        // Only the signal from the second template should be collected now
        auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 4000, 4000 } );
        ASSERT_NE( dataToSend, nullptr );
        ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );

        auto inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

        ASSERT_EQ( inspectionOutput.stateTemplateSyncId, "stateTemplate4" );
        ASSERT_EQ( inspectionOutput.signals.size(), 1 );
        ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_F );
        ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 2650 );
        ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 102.0 );
    }

    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command3", stateTemplate4->id, LastKnownStateOperation::DEACTIVATE } );
    inspector.inspectNewSignal<double>( SIG_D, TimePoint{ 4100, 4100 }, 3.0 );
    inspector.inspectNewSignal<double>( SIG_F, TimePoint{ 4100, 4100 }, 102.0 );

    // Nothing else should be collected now
    {
        auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 6000, 6000 } );
        ASSERT_EQ( dataToSend, nullptr );
    }
}

TEST_F( LastKnownStateInspectorDoubleTest, inspectSignalWithUpdatedInspectionLogic )
{
    // We first set SIG_A as on change policy
    inspector.onStateTemplatesChanged(
        std::make_shared<StateTemplateList>( StateTemplateList{ mStateTemplateOnChange } ) );
    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command1", mStateTemplateOnChange->id, LastKnownStateOperation::ACTIVATE } );
    inspector.inspectNewSignal<double>( SIG_A, TimePoint{ 500, 500 }, 0.1 );
    auto dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 550, 550 } );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    auto inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_A );
    ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 500 );
    ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 0.1 );

    inspector.inspectNewSignal<double>( SIG_A, TimePoint{ 600, 600 }, 0.2 );
    dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 650, 650 } );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_A );
    ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 600 );
    ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 0.2 );

    inspector.inspectNewSignal<double>( SIG_A, TimePoint{ 700, 700 }, 0.3 );
    auto stateTemplate4 = std::make_shared<const StateTemplateInformation>(
        StateTemplateInformation{ "stateTemplate4",
                                  "decoder",
                                  { LastKnownStateSignalInformation{ SIG_A, SignalType::DOUBLE } },
                                  LastKnownStateUpdateStrategy::PERIODIC,
                                  1000 } );
    // Here we change update policy to one-second period
    inspector.onStateTemplatesChanged( std::make_shared<StateTemplateList>( StateTemplateList{ stateTemplate4 } ) );
    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command2", stateTemplate4->id, LastKnownStateOperation::ACTIVATE } );
    // The first collection should be a snapshot with all signals for the newly activate state template.
    dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 750, 750 } );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_A );
    ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 0.3 );
    ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 700 );

    // This sample shall be collected as it's the first sample
    inspector.inspectNewSignal<double>( SIG_A, TimePoint{ 800, 800 }, 0.3 );

    ASSERT_EQ( collectNextDataToSendSorted( inspector, TimePoint{ 1700, 1700 } ), nullptr );
    // Collect at 1000 after the time it tried to send the snapshot
    dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 1750, 1750 } );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_A );

    inspector.inspectNewSignal<double>( SIG_A, TimePoint{ 1100, 1100 }, 0.3 );
    ASSERT_EQ( collectNextDataToSendSorted( inspector, TimePoint{ 2700, 2700 } ), nullptr );

    dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 2750, 2750 } );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_A );
    ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 0.3 );
    ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 1100 );

    // Here we change period to two-second
    auto stateTemplate5 = std::make_shared<const StateTemplateInformation>(
        StateTemplateInformation{ "stateTemplate5",
                                  "decoder",
                                  { LastKnownStateSignalInformation{ SIG_A, SignalType::DOUBLE } },
                                  LastKnownStateUpdateStrategy::PERIODIC,
                                  2000 } );
    // Here we change update policy to one-second period
    inspector.onStateTemplatesChanged( std::make_shared<StateTemplateList>( StateTemplateList{ stateTemplate5 } ) );
    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command3", stateTemplate5->id, LastKnownStateOperation::ACTIVATE } );

    inspector.inspectNewSignal<double>( SIG_A, TimePoint{ 2850, 2850 }, 0.3 );
    // The first collected data should be a snapshot. Then, the period should start counting from
    // the time the snapshot was generated.
    dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 2950, 2950 } );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_A );
    ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 0.3 );
    ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 2850 );

    inspector.inspectNewSignal<double>( SIG_A, TimePoint{ 3050, 3050 }, 0.3 );
    // Because we changed period to 2-second
    ASSERT_EQ( collectNextDataToSendSorted( inspector, TimePoint{ 3950, 3950 } ), nullptr );

    dataToSend = collectNextDataToSendSorted( inspector, TimePoint{ 4950, 4950 } );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals[0].signalID, SIG_A );
    ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 0.3 );
    ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 3050 );
}

/**
 * Here's the test scenarios:
 * signal A (on change)        |       |
 * signal B (on change)                |
 * signal F (period: 1s)       |       |
 * signal G (period: 0.4s)       | |   |
 * Inspection                        |    |      |      |
 */
TYPED_TEST( LastKnownStateInspectorTest, inspectSignalsPeriodicallyAndOnChange )
{
    const SignalID SIG_A = 1;
    const SignalID SIG_B = 2;
    const SignalID SIG_F = 6;
    const SignalID SIG_G = 7;
    LastKnownStateInspector inspector( this->mCommandResponses, nullptr );
    auto stateTemplate4 = std::make_shared<const StateTemplateInformation>(
        StateTemplateInformation{ "stateTemplate4",
                                  "decoder",
                                  { LastKnownStateSignalInformation{ SIG_A, SignalType::DOUBLE },
                                    LastKnownStateSignalInformation{ SIG_B, SignalType::INT64 } },
                                  LastKnownStateUpdateStrategy::ON_CHANGE } );
    auto stateTemplate5 = std::make_shared<const StateTemplateInformation>(
        StateTemplateInformation{ "stateTemplate5",
                                  "decoder",
                                  { LastKnownStateSignalInformation{ SIG_F, getSignalType<TypeParam>() } },
                                  LastKnownStateUpdateStrategy::PERIODIC,
                                  1000 } );
    auto stateTemplate6 = std::make_shared<const StateTemplateInformation>(
        StateTemplateInformation{ "stateTemplate6",
                                  "decoder",
                                  { LastKnownStateSignalInformation{ SIG_G, getSignalType<TypeParam>() } },
                                  LastKnownStateUpdateStrategy::PERIODIC,
                                  400 } );

    inspector.onStateTemplatesChanged(
        std::make_shared<StateTemplateList>( StateTemplateList{ stateTemplate4, stateTemplate5, stateTemplate6 } ) );

    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command1", stateTemplate4->id, LastKnownStateOperation::ACTIVATE } );
    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command2", stateTemplate5->id, LastKnownStateOperation::ACTIVATE } );
    inspector.onNewCommandReceived(
        LastKnownStateCommandRequest{ "command3", stateTemplate6->id, LastKnownStateOperation::ACTIVATE } );

    // First is a snapshot triggered by each activate command
    inspector.inspectNewSignal<double>( SIG_A, TimePoint{ 50, 50 }, 0.05 );
    inspector.inspectNewSignal<TypeParam>( SIG_F, TimePoint{ 50, 50 }, 0 );
    inspector.inspectNewSignal<TypeParam>( SIG_G, TimePoint{ 80, 80 }, 1 );
    inspector.inspectNewSignal<TypeParam>( SIG_G, TimePoint{ 90, 90 }, 2 );
    auto dataToSend = this->collectNextDataToSendSorted( inspector, TimePoint{ 0, 0 } );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 3 );

    inspector.inspectNewSignal<double>( SIG_A, TimePoint{ 500, 500 }, 0.1 );
    inspector.inspectNewSignal<TypeParam>( SIG_F, TimePoint{ 500, 500 }, 1 );
    inspector.inspectNewSignal<TypeParam>( SIG_G, TimePoint{ 800, 800 }, 11 );
    inspector.inspectNewSignal<TypeParam>( SIG_G, TimePoint{ 900, 900 }, 12 );
    dataToSend = this->collectNextDataToSendSorted( inspector, TimePoint{ 1000, 1000 } );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 3 );

    auto inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.stateTemplateSyncId, "stateTemplate4" );
    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, this->SIG_A );
    ASSERT_EQ( inspectionOutput.signals[0].getType(), SignalType::DOUBLE );
    ASSERT_EQ( inspectionOutput.signals[0].getValue().value.doubleVal, 0.1 );
    ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 500 );

    inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 1 );

    ASSERT_EQ( inspectionOutput.stateTemplateSyncId, "stateTemplate5" );
    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, this->SIG_F );
    ASSERT_EQ( inspectionOutput.signals[0].getType(), getSignalType<TypeParam>() );
    ASSERT_NO_FATAL_FAILURE(
        assertSignalValue( inspectionOutput.signals[0].getValue(), 1, getSignalType<TypeParam>() ) );
    ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 500 );

    inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 2 );

    ASSERT_EQ( inspectionOutput.stateTemplateSyncId, "stateTemplate6" );
    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, this->SIG_G );
    ASSERT_EQ( inspectionOutput.signals[0].getType(), getSignalType<TypeParam>() );
    ASSERT_NO_FATAL_FAILURE(
        assertSignalValue( inspectionOutput.signals[0].getValue(), 12, getSignalType<TypeParam>() ) );
    ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 900 );

    inspector.inspectNewSignal<TypeParam>( this->SIG_F, TimePoint{ 1100, 1100 }, 2.0 );
    inspector.inspectNewSignal<TypeParam>( this->SIG_G, TimePoint{ 1100, 1100 }, 13 );
    inspector.inspectNewSignal<TypeParam>( this->SIG_G, TimePoint{ 1100, 1100 }, 13 );
    inspector.inspectNewSignal<double>( this->SIG_A, TimePoint{ 1100, 1100 }, 0.1 );
    inspector.inspectNewSignal<int64_t>( this->SIG_B, TimePoint{ 1100, 1100 }, 2 );
    dataToSend = this->collectNextDataToSendSorted( inspector, TimePoint{ 1200, 1200 } );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );
    // We should not receive update for signal D and E because next period has not complete
    // We will receive update from signal B

    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, this->SIG_B );
    ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 1100 );
    ASSERT_EQ( inspectionOutput.signals[0].getValue().value.int64Val, 2 );

    // Here we shall expect the update for signal E
    dataToSend = this->collectNextDataToSendSorted( inspector, TimePoint{ 1400, 1400 } );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, this->SIG_G );
    ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 1100 );
    ASSERT_NO_FATAL_FAILURE(
        assertSignalValue( inspectionOutput.signals[0].getValue(), 13, getSignalType<TypeParam>() ) );

    dataToSend = this->collectNextDataToSendSorted( inspector, TimePoint{ 2000, 2000 } );
    ASSERT_NE( dataToSend, nullptr );
    ASSERT_EQ( dataToSend->stateTemplateCollectedSignals.size(), 1 );
    inspectionOutput = dataToSend->stateTemplateCollectedSignals.at( 0 );

    ASSERT_EQ( inspectionOutput.signals.size(), 1 );
    ASSERT_EQ( inspectionOutput.signals[0].signalID, this->SIG_F );
    ASSERT_EQ( inspectionOutput.signals[0].receiveTime, 1100 );
    ASSERT_NO_FATAL_FAILURE(
        assertSignalValue( inspectionOutput.signals[0].getValue(), 2, getSignalType<TypeParam>() ) );
}

} // namespace IoTFleetWise
} // namespace Aws
