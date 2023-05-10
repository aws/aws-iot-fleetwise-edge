// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "OBDOverCANModule.h"
#include "OBDDataTypes.h"
#include "Testing.h"
#include "WaitUntil.h"
#include "businterfaces/ISOTPOverCANReceiver.h"
#include "datatypes/OBDDataTypesUnitTestOnly.h"
#include <gtest/gtest.h>
#include <linux/can.h>
#include <linux/can/isotp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <thread>

using namespace Aws::IoTFleetWise::DataInspection;
using namespace Aws::IoTFleetWise::TestingSupport;

namespace
{
using SignalDoubleValue = double;

// ECU ID mocked for unit test
enum class ECU_ID_MOCK
{
    ENGINE_ECU_TX = 0x7E0,
    ENGINE_ECU_RX = 0x7E8,
    ENGINE_ECU_TX_EXTENDED = 0x18DA58F1,
    ENGINE_ECU_RX_EXTENDED = 0x18DAF158,
    TRANSMISSION_ECU_TX = 0x7E1,
    TRANSMISSION_ECU_TX_EXTENDED = 0x18DA59F1,
    TRANSMISSION_ECU_RX = 0x7E9,
    TRANSMISSION_ECU_RX_EXTENDED = 0x18DAF159
};

bool
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

// Initialize the decoder dictionary for this test.
// Note In actual product, decoder dictionary comes from decoder manifest. For this unit test,
// The decoder dictionary is initialized based on a local table mode1PIDs in OBDDataDecoder Module.
void
initInspectionMatrix( OBDOverCANModule &module )
{
    // Prepare the Inspection Engine and the inspection matrix

    auto matrix = std::make_shared<InspectionMatrix>();
    InspectionMatrixSignalCollectionInfo matrixCollectInfo;
    ConditionWithCollectedData condition;
    matrixCollectInfo.signalID = 1234;
    matrixCollectInfo.sampleBufferSize = 50;
    matrixCollectInfo.minimumSampleIntervalMs = 10;
    matrixCollectInfo.fixedWindowPeriod = 77777;
    matrixCollectInfo.isConditionOnlySignal = true;
    condition.signals.push_back( matrixCollectInfo );
    condition.afterDuration = 3;
    condition.minimumPublishIntervalMs = 0;
    condition.probabilityToSend = 1.0;
    condition.includeActiveDtcs = true;
    condition.triggerOnlyOnRisingEdge = false;
    // Node
    ExpressionNode node;
    node.nodeType = ExpressionNodeType::BOOLEAN;
    node.booleanValue = true;
    condition.condition = &node;
    // add the condition to the matrix
    matrix->conditions.emplace_back( condition );
    module.onChangeInspectionMatrix( matrix );
}

// Initialize the decoder dictionary for this test.
// Note In actual product, decoder dictionary comes from decoder manifest. For this unit test,
// The decoder dictionary is initialized based on a local table mode1PIDs in OBDDataDecoder Module.
std::shared_ptr<CANDecoderDictionary>
initDecoderDictionary( SignalType signalType = SignalType::DOUBLE )
{
    auto decoderDictPtr = std::make_shared<CANDecoderDictionary>();
    decoderDictPtr->canMessageDecoderMethod.emplace( 0, std::unordered_map<CANRawFrameID, CANMessageDecoderMethod>() );
    for ( const auto &pidInfo : mode1PIDs )
    {
        CANMessageFormat format;
        auto pid = pidInfo.pid;
        format.mMessageID = pid;
        format.mSizeInBytes = static_cast<uint8_t>( pidInfo.retLen );
        format.mSignals = std::vector<CANSignalFormat>( pidInfo.formulas.size() );
        for ( uint32_t idx = 0; idx < pidInfo.formulas.size(); ++idx )
        {
            // In final product, signal ID comes from Cloud, edge doesn't generate signal ID
            // Below signal ID initialization got implemented only for Edge testing.
            // In this test, signal ID are defined as PID | (signal order) << 8
            format.mSignals[idx].mSignalID = pid | ( idx << 8 );
            format.mSignals[idx].mFirstBitPosition =
                static_cast<uint16_t>( pidInfo.formulas[idx].byteOffset * BYTE_SIZE + pidInfo.formulas[idx].bitShift );
            format.mSignals[idx].mSizeInBits = static_cast<uint16_t>(
                ( pidInfo.formulas[idx].numOfBytes - 1 ) * BYTE_SIZE + pidInfo.formulas[idx].bitMaskLen );
            format.mSignals[idx].mFactor = pidInfo.formulas[idx].scaling;
            format.mSignals[idx].mOffset = pidInfo.formulas[idx].offset;
            format.mSignals[idx].mSignalType = signalType;
            decoderDictPtr->signalIDsToCollect.insert( pid | ( idx << 8 ) );
        }
        decoderDictPtr->canMessageDecoderMethod[0].emplace( pid, CANMessageDecoderMethod() );
        decoderDictPtr->canMessageDecoderMethod[0][pid].format = format;
    }
    return decoderDictPtr;
}

} // namespace

struct ECUMock
{
    void
    init( ECUID broadcastRxId, ECU_ID_MOCK physicalRxId, ECU_ID_MOCK physicalTxId )
    {
        ISOTPOverCANReceiverOptions broadcastOptions;
        // Broadcast
        broadcastOptions.mSocketCanIFName = "vcan0";
        broadcastOptions.mSourceCANId = (unsigned)ECU_ID_MOCK::ENGINE_ECU_RX_EXTENDED;
        broadcastOptions.mIsExtendedId = (unsigned)broadcastRxId > CAN_SFF_MASK;
        broadcastOptions.mDestinationCANId = (unsigned)broadcastRxId;
        broadcastOptions.mP2TimeoutMs = P2_TIMEOUT_DEFAULT_MS;
        ASSERT_TRUE( mBroadcastReceiver.init( broadcastOptions ) );
        ASSERT_TRUE( mBroadcastReceiver.connect() );

        ISOTPOverCANSenderReceiverOptions ecuOptions;
        ecuOptions.mSocketCanIFName = "vcan0";
        ecuOptions.mIsExtendedId = (unsigned)physicalRxId > CAN_SFF_MASK;
        ecuOptions.mP2TimeoutMs = P2_TIMEOUT_DEFAULT_MS;
        ecuOptions.mSourceCANId = (unsigned)physicalTxId;
        ecuOptions.mDestinationCANId = (unsigned)physicalRxId;
        ASSERT_TRUE( mPhysicalSenderReceiver.init( ecuOptions ) );
        ASSERT_TRUE( mPhysicalSenderReceiver.connect() );
    }
    ISOTPOverCANReceiver mBroadcastReceiver;
    ISOTPOverCANSenderReceiver mPhysicalSenderReceiver;
    std::vector<uint8_t> mSupportedPIDResponse1;
    std::vector<uint8_t> mSupportedPIDResponse2;
    std::vector<uint8_t> mRequestPID1;
    std::vector<uint8_t> mPIDResponse1;
    std::vector<uint8_t> mRequestPID2;
    std::vector<uint8_t> mPIDResponse2;
    std::vector<uint8_t> mDTCResponse;
    std::atomic<bool> mShouldStop{ false };
    Thread mThread;
};

// This function will be run in a separate thread to mock ECU response to Edge Agent OBD requests
void
ecuResponse( void *ecuMock )
{
    auto ecuMockPtr = static_cast<ECUMock *>( ecuMock );
    while ( !ecuMockPtr->mShouldStop )
    {
        struct pollfd pfds[] = { { ecuMockPtr->mBroadcastReceiver.getSocket(), POLLIN, 0 },
                                 { ecuMockPtr->mPhysicalSenderReceiver.getSocket(), POLLIN, 0 } };
        int res = poll( pfds, 2U, 100 ); // 100 ms poll time
        if ( res <= 0 )
        {
            continue;
        }
        std::vector<uint8_t> rxPDUData;
        if ( pfds[0].revents != 0 )
        {
            ecuMockPtr->mBroadcastReceiver.receivePDU( rxPDUData );
        }
        else if ( pfds[1].revents != 0 )
        {
            ecuMockPtr->mPhysicalSenderReceiver.receivePDU( rxPDUData );
        }
        else
        {
            continue;
        }

        if ( rxPDUData == std::vector<uint8_t>{ 0x01, 0x00 } )
        {
            ecuMockPtr->mPhysicalSenderReceiver.sendPDU( ecuMockPtr->mSupportedPIDResponse1 );
        }
        else if ( rxPDUData == std::vector<uint8_t>{ 0x01, 0x00, 0x20, 0x40, 0x60, 0x80, 0xA0 } )
        {
            // Edge Agent is querying supported PIDs
            ecuMockPtr->mPhysicalSenderReceiver.sendPDU( ecuMockPtr->mSupportedPIDResponse1 );
        }
        else if ( rxPDUData == std::vector<uint8_t>{ 0x01, 0xC0, 0xE0 } )
        {
            // Edge Agent is querying supported PIDs
            ecuMockPtr->mPhysicalSenderReceiver.sendPDU( ecuMockPtr->mSupportedPIDResponse2 );
        }
        else if ( rxPDUData == ecuMockPtr->mRequestPID1 )
        {
            ecuMockPtr->mPhysicalSenderReceiver.sendPDU( ecuMockPtr->mPIDResponse1 );
        }
        else if ( rxPDUData == ecuMockPtr->mRequestPID2 )
        {
            ecuMockPtr->mPhysicalSenderReceiver.sendPDU( ecuMockPtr->mPIDResponse2 );
        }
        else if ( rxPDUData == std::vector<uint8_t>{ 0x03 } )
        {
            ecuMockPtr->mPhysicalSenderReceiver.sendPDU( ecuMockPtr->mDTCResponse );
        }
        else
        {
            // Do Nothing, this message is not recognized by ECU
        }
    }
}

class OBDOverCANModuleTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        if ( !socketAvailable() )
        {
            GTEST_SKIP() << "Skipping test fixture due to unavailability of socket";
        }
        signalBufferPtr = std::make_shared<SignalBuffer>( 256 );
        activeDTCBufferPtr = std::make_shared<ActiveDTCBuffer>( 256 );
    }
    void
    TearDown() override
    {
        for ( auto &ecuMock : ecus )
        {
            ecuMock.mShouldStop.store( true, std::memory_order_relaxed );
            ecuMock.mThread.release();
            ASSERT_TRUE( ecuMock.mBroadcastReceiver.disconnect() );
            ASSERT_TRUE( ecuMock.mPhysicalSenderReceiver.disconnect() );
        }
        ASSERT_TRUE( obdModule.disconnect() );
    }
    // ISOTPOverCANSenderReceiver engineECU;
    OBDOverCANModule obdModule;
    std::shared_ptr<SignalBuffer> signalBufferPtr;
    std::shared_ptr<ActiveDTCBuffer> activeDTCBufferPtr;
    std::vector<ECUMock> ecus;

    void
    assertSignalValue( const SignalValueWrapper &signalValueWrapper,
                       double expectedSignalValue,
                       SignalType expectedSignalType )
    {
        switch ( expectedSignalType )
        {
        case SignalType::UINT8:
            ASSERT_EQ( signalValueWrapper.value.uint8Val, static_cast<uint8_t>( expectedSignalValue ) );
            break;
        case SignalType::INT8:
            ASSERT_EQ( signalValueWrapper.value.int8Val, static_cast<int8_t>( expectedSignalValue ) );
            break;
        case SignalType::UINT16:
            ASSERT_EQ( signalValueWrapper.value.uint16Val, static_cast<uint16_t>( expectedSignalValue ) );
            break;
        case SignalType::INT16:
            ASSERT_EQ( signalValueWrapper.value.int16Val, static_cast<int16_t>( expectedSignalValue ) );
            break;
        case SignalType::UINT32:
            ASSERT_EQ( signalValueWrapper.value.uint32Val, static_cast<uint32_t>( expectedSignalValue ) );
            break;
        case SignalType::INT32:
            ASSERT_EQ( signalValueWrapper.value.int32Val, static_cast<int32_t>( expectedSignalValue ) );
            break;
        case SignalType::UINT64:
            ASSERT_EQ( signalValueWrapper.value.uint64Val, static_cast<uint64_t>( expectedSignalValue ) );
            break;
        case SignalType::INT64:
            ASSERT_EQ( signalValueWrapper.value.int64Val, static_cast<int64_t>( expectedSignalValue ) );
            break;
        case SignalType::FLOAT:
            ASSERT_FLOAT_EQ( signalValueWrapper.value.floatVal, static_cast<float>( expectedSignalValue ) );
            break;
        case SignalType::DOUBLE:
            ASSERT_DOUBLE_EQ( signalValueWrapper.value.doubleVal, static_cast<double>( expectedSignalValue ) );
            break;
        case SignalType::BOOLEAN:
            ASSERT_EQ( signalValueWrapper.value.boolVal, static_cast<bool>( expectedSignalValue ) );
            break;
        default:
            FAIL() << "Unsupported signal type";
        }
    }
};

TEST_F( OBDOverCANModuleTest, OBDOverCANModuleInitFailure )
{
    constexpr uint32_t obdPIDRequestInterval = 0; // 0 seconds
    constexpr uint32_t obdDTCRequestInterval = 0; // 0 seconds
    ASSERT_FALSE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, false ) );
}

TEST_F( OBDOverCANModuleTest, OBDOverCANModuleInitTestSuccess )
{
    constexpr uint32_t obdPIDRequestInterval = 1; // 1 second
    constexpr uint32_t obdDTCRequestInterval = 1; // 1 seconds
    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, false ) );
    ASSERT_TRUE( obdModule.connect() );
    ASSERT_TRUE( obdModule.disconnect() );
}

TEST_F( OBDOverCANModuleTest, OBDOverCANModuleInitTestFailure )
{
    constexpr uint32_t obdPIDRequestInterval = 0; // 2 seconds
    constexpr uint32_t obdDTCRequestInterval = 0; // 2 seconds
    ASSERT_FALSE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, false ) );
}

TEST_F( OBDOverCANModuleTest, OBDOverCANModuleAndDecoderManifestLifecycle )
{
    // If no decoder manifest is available, the module should be sleeping and not sending
    // any requests on the bus.
    // In this test, we create a receiver on the bus, we start the module and assert
    // that the module did not send any request while it did not receive a manifest.
    // Then later we activate a decoder manifest and we should see the module sending
    // requests on the bus
    std::vector<uint8_t> ecmRxPDUData;
    constexpr uint32_t obdPIDRequestInterval = 1; // 1 second
    constexpr uint32_t obdDTCRequestInterval = 2; // 2 seconds

    ISOTPOverCANSenderReceiver engineECU;
    ISOTPOverCANSenderReceiverOptions engineECUOptions;

    // Engine ECU
    engineECUOptions.mSocketCanIFName = "vcan0";
    engineECUOptions.mSourceCANId = toUType( ECU_ID_MOCK::ENGINE_ECU_RX );
    engineECUOptions.mDestinationCANId = toUType( ECU_ID_MOCK::ENGINE_ECU_TX );
    ASSERT_TRUE( engineECU.init( engineECUOptions ) );
    ASSERT_TRUE( engineECU.connect() );
    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, false ) );
    ASSERT_TRUE( obdModule.connect() );
    // No Requests should be seen on the bus as it hasn't received a valid decoder dictionary yet.
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    ASSERT_FALSE( engineECU.receivePDU( ecmRxPDUData ) );
    ASSERT_TRUE( engineECU.disconnect() );
    ASSERT_TRUE( obdModule.disconnect() );
}

class OBDOverCANModuleTestWithAllSignalTypes : public OBDOverCANModuleTest,
                                               public testing::WithParamInterface<SignalType>
{
};

INSTANTIATE_TEST_SUITE_P( MultipleSignals, OBDOverCANModuleTestWithAllSignalTypes, allSignalTypes, signalTypeToString );

TEST_P( OBDOverCANModuleTestWithAllSignalTypes, RequestPIDFromNotExtendedIDECUTest )
{
    SignalType signalType = GetParam();
    // Setup ECU Mock
    ecus = std::vector<ECUMock>( 2 );

    ecus[0].init( ECUID::BROADCAST_ID, ECU_ID_MOCK::ENGINE_ECU_TX, ECU_ID_MOCK::ENGINE_ECU_RX );
    ecus[0].mSupportedPIDResponse1 = { 0x41, 0x00, 0x18, 0x00, 0x00, 0x00 };
    ecus[0].mSupportedPIDResponse2 = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ecus[0].mRequestPID1 = { 0x01, 0x04, 0x05 };
    ecus[0].mPIDResponse1 = { 0x41, 0x04, 0x90, 0x05, 0x6E };
    ecus[0].mDTCResponse = { 0x43, 0x02, 0x01, 0x43, 0x41, 0x96 };
    ecus[0].mThread.create( ecuResponse, &ecus[0] );

    ecus[1].init( ECUID::BROADCAST_ID, ECU_ID_MOCK::TRANSMISSION_ECU_TX, ECU_ID_MOCK::TRANSMISSION_ECU_RX );
    ecus[1].mSupportedPIDResponse1 = { 0x41, 0x00, 0x00, 0x08, 0x00, 0x00 };
    ecus[1].mSupportedPIDResponse2 = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ecus[1].mRequestPID1 = { 0x01, 0x0D };
    ecus[1].mPIDResponse1 = { 0x41, 0x0D, 0x23 };
    ecus[1].mDTCResponse = { 0x43, 0x00 };
    ecus[1].mThread.create( ecuResponse, &ecus[1] );

    // Request PIDs every 2 seconds and no DTC request
    constexpr uint32_t obdPIDRequestInterval = 1; // 1 second
    constexpr uint32_t obdDTCRequestInterval = 0; // no DTC request
    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, false ) );
    ASSERT_TRUE( obdModule.connect() );
    // Create decoder dictionary
    auto decoderDictPtr = initDecoderDictionary( signalType );
    // publish decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );

    // Expected value for PID signals
    std::map<SignalID, SignalDoubleValue> expectedPIDSignalValue = {
        { toUType( EmissionPIDs::ENGINE_LOAD ), 56.470588235294116 },
        { toUType( EmissionPIDs::ENGINE_COOLANT_TEMPERATURE ), 70 },
        { toUType( EmissionPIDs::VEHICLE_SPEED ), 35 } };
    // Verify all PID Signals are correctly decoded
    CollectedSignal signal;
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    assertSignalValue( signal.value, expectedPIDSignalValue[signal.signalID], signalType );
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    assertSignalValue( signal.value, expectedPIDSignalValue[signal.signalID], signalType );
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    assertSignalValue( signal.value, expectedPIDSignalValue[signal.signalID], signalType );
}

// This test is to validate that OBDOverCANModule will only request PID that are specified in Decoder Dictionary
// In this test scenario, the decoder dictionary will request signals from PIDs 0x04, 0x14, 0x0D, 0x15, 0x16, 0x17
// Engine ECU supports PIDs 0x04,0x05,0x09,0x11,0x12,0x13,0x14
// Transmission ECU supports PIDs 0x0D, 0xC1,
// Hence Edge Agent will only request 0x04, 0x14 to Engine ECU and 0x0D, 0xC1 to Transmission ECU
TEST_F( OBDOverCANModuleTest, RequestPartialPIDFromNotExtendedIDECUTest )
{
    // Setup ECU Mock
    ecus = std::vector<ECUMock>( 2 );

    ecus[0].init( ECUID::BROADCAST_ID, ECU_ID_MOCK::ENGINE_ECU_TX, ECU_ID_MOCK::ENGINE_ECU_RX );
    ecus[0].mSupportedPIDResponse1 = { 0x41, 0x00, 0x18, 0x80, 0xF0, 0x00 };
    ecus[0].mSupportedPIDResponse2 = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ecus[0].mRequestPID1 = { 0x01, 0x04, 0x14 };
    ecus[0].mPIDResponse1 = { 0x41, 0x04, 0x99, 0x14, 0x10, 0x20 };
    ecus[0].mDTCResponse = { 0x43, 0x02, 0x01, 0x43, 0x41, 0x96 };
    ecus[0].mThread.create( ecuResponse, &ecus[0] );

    ecus[1].init( ECUID::BROADCAST_ID, ECU_ID_MOCK::TRANSMISSION_ECU_TX, ECU_ID_MOCK::TRANSMISSION_ECU_RX );
    ecus[1].mSupportedPIDResponse1 = { 0x41, 0x00, 0x00, 0x08, 0x00, 0x00 };
    ecus[1].mSupportedPIDResponse2 = { 0x41, 0xC0, 0x80, 0x10, 0x00, 0x00 };
    ecus[1].mRequestPID1 = { 0x01, 0x0D, 0xC1 };
    ecus[1].mPIDResponse1 = { 0x41, 0x0D, 0x23, 0xC1, 0xAA };
    ecus[1].mDTCResponse = { 0x43, 0x00 };
    ecus[1].mThread.create( ecuResponse, &ecus[1] );

    // Request PIDs every 2 seconds and no DTC request
    constexpr uint32_t obdPIDRequestInterval = 1; // 1 second
    constexpr uint32_t obdDTCRequestInterval = 0; // no DTC request
    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, false ) );
    ASSERT_TRUE( obdModule.connect() );
    // Create decoder dictionary
    auto decoderDictPtr = initDecoderDictionary();
    decoderDictPtr->signalIDsToCollect.clear();
    decoderDictPtr->signalIDsToCollect.insert( 0x04 );
    // Oxygen Sensor 1 contains two signals, we want to collect the second signal
    decoderDictPtr->signalIDsToCollect.insert( 0x14 | 1 << 8 );
    decoderDictPtr->signalIDsToCollect.insert( 0x0D );
    // The decoder dictionary request PID 0x15, 0x16, 0x17 but they will not be supported by ECU
    decoderDictPtr->signalIDsToCollect.insert( 0x15 );
    decoderDictPtr->signalIDsToCollect.insert( 0x16 );
    decoderDictPtr->signalIDsToCollect.insert( 0x17 );
    decoderDictPtr->signalIDsToCollect.insert( 0xC1 );
    // publish decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );

    // Expected value for PID signals
    std::map<SignalID, SignalDoubleValue> expectedPIDSignalValue = {
        { toUType( EmissionPIDs::ENGINE_LOAD ), 60 },
        { toUType( EmissionPIDs::OXYGEN_SENSOR1_1 ) | 0 << 8, (double)0x10 / 200 },
        { toUType( EmissionPIDs::OXYGEN_SENSOR1_1 ) | 1 << 8, (double)0x20 * 100 / 128 - 100 },
        { toUType( EmissionPIDs::VEHICLE_SPEED ), 35 },
        { 0xC1, 0xAA } };
    // Verify all PID Signals are correctly decoded
    CollectedSignal signal;
    for ( size_t idx = 0; idx < expectedPIDSignalValue.size(); ++idx )
    {
        WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
        ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );
    }
    ASSERT_TRUE( obdModule.getSignalBufferPtr()->empty() );
}

// This test is to validate that OBDOverCANModule can update the PID request list when receiving new decoder manifest
// In this test scenario, decoder dictionary will first request PID 0x04, 0x14 and 0x0D; then it will switch
// to 0x05, 0x14 and 0x0C.
TEST_F( OBDOverCANModuleTest, DecoderDictionaryUpdatePIDsToCollectTest )
{
    // Setup ECU Mock
    ecus = std::vector<ECUMock>( 2 );

    ecus[0].init(
        ECUID::BROADCAST_EXTENDED_ID, ECU_ID_MOCK::ENGINE_ECU_TX_EXTENDED, ECU_ID_MOCK::ENGINE_ECU_RX_EXTENDED );
    ecus[0].mSupportedPIDResponse1 = { 0x41, 0x00, 0x18, 0x80, 0xF0, 0x00 };
    ecus[0].mSupportedPIDResponse2 = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ecus[0].mRequestPID1 = { 0x01, 0x04, 0x14 };
    ecus[0].mPIDResponse1 = { 0x41, 0x04, 0x99, 0x14, 0x10, 0x20 };
    ecus[0].mRequestPID2 = { 0x01, 0x05, 0x14 };
    ecus[0].mPIDResponse2 = { 0x41, 0x05, 0x4C, 0x14, 0x10, 0x20 };
    ecus[0].mDTCResponse = { 0x43, 0x02, 0x01, 0x43, 0x41, 0x96 };
    ecus[0].mThread.create( ecuResponse, &ecus[0] );

    ecus[1].init( ECUID::BROADCAST_EXTENDED_ID,
                  ECU_ID_MOCK::TRANSMISSION_ECU_TX_EXTENDED,
                  ECU_ID_MOCK::TRANSMISSION_ECU_RX_EXTENDED );
    ecus[1].mSupportedPIDResponse1 = { 0x41, 0x00, 0x00, 0x18, 0x00, 0x00 };
    ecus[1].mSupportedPIDResponse2 = { 0x41, 0xC0, 0x80, 0x10, 0x00, 0x00 };
    ecus[1].mRequestPID1 = { 0x01, 0x0D, 0xC1 };
    ecus[1].mPIDResponse1 = { 0x41, 0x0D, 0x23, 0xC1, 0xAA };
    ecus[1].mRequestPID2 = { 0x01, 0x0C, 0xC1 };
    ecus[1].mPIDResponse2 = { 0x41, 0x0C, 0x0F, 0xA0, 0xC1, 0xAA };
    ecus[1].mDTCResponse = { 0x43, 0x00 };
    ecus[1].mThread.create( ecuResponse, &ecus[1] );

    // Request PIDs every 2 seconds and no DTC request
    constexpr uint32_t obdPIDRequestInterval = 1; // 1 second
    constexpr uint32_t obdDTCRequestInterval = 0; // no DTC request
    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, false ) );
    ASSERT_TRUE( obdModule.connect() );
    // Create decoder dictionary
    auto decoderDictPtr = initDecoderDictionary();
    decoderDictPtr->signalIDsToCollect.clear();
    decoderDictPtr->signalIDsToCollect.insert( 0x04 );
    // Oxygen Sensor 1 contains two signals, we want to collect the second signal
    decoderDictPtr->signalIDsToCollect.insert( 0x14 | 1 << 8 );
    decoderDictPtr->signalIDsToCollect.insert( 0x0D );
    // The decoder dictionary request PID 0x15, 0x16, 0x17 but they will not be supported by ECU
    decoderDictPtr->signalIDsToCollect.insert( 0x15 );
    decoderDictPtr->signalIDsToCollect.insert( 0x16 );
    decoderDictPtr->signalIDsToCollect.insert( 0x17 );
    decoderDictPtr->signalIDsToCollect.insert( 0xC1 );
    // publish decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );

    // Expected value for PID signals
    std::map<SignalID, SignalDoubleValue> expectedPIDSignalValue = {
        { toUType( EmissionPIDs::ENGINE_LOAD ), 60 },
        { toUType( EmissionPIDs::OXYGEN_SENSOR1_1 ) | 0 << 8, (double)0x10 / 200 },
        { toUType( EmissionPIDs::OXYGEN_SENSOR1_1 ) | 1 << 8, (double)0x20 * 100 / 128 - 100 },
        { toUType( EmissionPIDs::VEHICLE_SPEED ), 35 },
        { 0xC1, 0xAA } };
    // Verify all PID Signals are correctly decoded
    CollectedSignal signal;
    for ( size_t idx = 0; idx < expectedPIDSignalValue.size(); ++idx )
    {
        WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
        ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );
    }
    ASSERT_TRUE( obdModule.getSignalBufferPtr()->empty() );

    // Update Decoder Dictionary to collect PID 0x05
    decoderDictPtr->signalIDsToCollect.erase( 0x04 );
    decoderDictPtr->signalIDsToCollect.insert( 0x05 );
    decoderDictPtr->signalIDsToCollect.insert( 0x0C );
    decoderDictPtr->signalIDsToCollect.erase( 0x0D );
    decoderDictPtr->signalIDsToCollect.insert( 0x0E );
    // publish the new decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );

    expectedPIDSignalValue = { { toUType( EmissionPIDs::ENGINE_COOLANT_TEMPERATURE ), 36 },
                               { toUType( EmissionPIDs::OXYGEN_SENSOR1_1 ) | 0 << 8, (double)0x10 / 200 },
                               { toUType( EmissionPIDs::OXYGEN_SENSOR1_1 ) | 1 << 8, (double)0x20 * 100 / 128 - 100 },
                               { toUType( EmissionPIDs::ENGINE_SPEED ), 1000 },
                               { 0xC1, 0xAA } };
    // Verify all PID Signals are correctly decoded
    for ( size_t idx = 0; idx < expectedPIDSignalValue.size(); ++idx )
    {
        WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
        ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );
    }
    ASSERT_TRUE( obdModule.getSignalBufferPtr()->empty() );

    // Update Decoder Dictionary to collect no signals.
    // publish the new decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( nullptr, VehicleDataSourceProtocol::OBD );
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    // wait for one cycle and clear out the signal buffer
    while ( !obdModule.getSignalBufferPtr()->empty() )
    {
        obdModule.getSignalBufferPtr()->pop( signal );
    }
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    // We shall not receive any PIDs as decoder dictionary is empty
    ASSERT_TRUE( obdModule.getSignalBufferPtr()->empty() );
}

TEST_F( OBDOverCANModuleTest, RequestEmissionPIDAndDTCFromExtendedIDECUTest )
{
    // Setup ECU Mock
    ecus = std::vector<ECUMock>( 2 );

    ecus[0].init(
        ECUID::BROADCAST_EXTENDED_ID, ECU_ID_MOCK::ENGINE_ECU_TX_EXTENDED, ECU_ID_MOCK::ENGINE_ECU_RX_EXTENDED );
    ecus[0].mSupportedPIDResponse1 = { 0x41, 0x00, 0x18, 0x00, 0x00, 0x00 };
    ecus[0].mSupportedPIDResponse2 = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ecus[0].mRequestPID1 = { 0x01, 0x04, 0x05 };
    ecus[0].mPIDResponse1 = { 0x41, 0x04, 0x99, 0x05, 0x6E };
    ecus[0].mDTCResponse = { 0x43, 0x04, 0x01, 0x43, 0x41, 0x96, 0x81, 0x48, 0xC1, 0x48 };
    ecus[0].mThread.create( ecuResponse, &ecus[0] );

    ecus[1].init( ECUID::BROADCAST_EXTENDED_ID,
                  ECU_ID_MOCK::TRANSMISSION_ECU_TX_EXTENDED,
                  ECU_ID_MOCK::TRANSMISSION_ECU_RX_EXTENDED );
    ecus[1].mSupportedPIDResponse1 = { 0x41, 0x00, 0x00, 0x08, 0x00, 0x00 };
    ecus[1].mSupportedPIDResponse2 = { 0x41, 0xC0, 0x80, 0x00, 0x00, 0x00 };
    ecus[1].mRequestPID1 = { 0x01, 0x0D, 0xC1 };
    ecus[1].mPIDResponse1 = { 0x41, 0x0D, 0x23, 0xC1, 0xAA };
    ecus[1].mDTCResponse = { 0x43, 0x00 };
    ecus[1].mThread.create( ecuResponse, &ecus[1] );

    // Request PIDs every 2 seconds, and DTCs every 2 seconds
    constexpr uint32_t obdPIDRequestInterval = 1; // 1 second
    constexpr uint32_t obdDTCRequestInterval = 1; // Request DTC every 1 seconds

    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, false ) );
    ASSERT_TRUE( obdModule.connect() );
    // Create decoder dictionary
    auto decoderDictPtr = initDecoderDictionary();
    // publish decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );
    initInspectionMatrix( obdModule );

    // This is the expected value for PID signals.
    std::map<SignalID, SignalDoubleValue> expectedPIDSignalValue = {
        { toUType( EmissionPIDs::ENGINE_LOAD ), 60 },
        { toUType( EmissionPIDs::ENGINE_COOLANT_TEMPERATURE ), 70 },
        { toUType( EmissionPIDs::VEHICLE_SPEED ), 35 },
        { 0xC1, 0xAA } };
    // Verify produced PID signals are correctly decoded
    CollectedSignal signal;
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );

    // Verify produced DTC Buffer is correct. 4 codes for ECM , 0 codes for TCM
    DTCInfo dtcInfo;
    WAIT_ASSERT_TRUE( obdModule.getActiveDTCBufferPtr()->pop( dtcInfo ) );
    ASSERT_EQ( dtcInfo.mDTCCodes.size(), 4 );
    ASSERT_EQ( dtcInfo.mSID, SID::STORED_DTC );
    ASSERT_EQ( dtcInfo.mDTCCodes[0], "P0143" );
    ASSERT_EQ( dtcInfo.mDTCCodes[1], "C0196" );
    ASSERT_EQ( dtcInfo.mDTCCodes[2], "B0148" );
    ASSERT_EQ( dtcInfo.mDTCCodes[3], "U0148" );
}

TEST_F( OBDOverCANModuleTest, RequestPIDAndDTCFromNonExtendedIDECUTest )
{
    // Setup ECU Mock
    ecus = std::vector<ECUMock>( 2 );

    ecus[0].init( ECUID::BROADCAST_ID, ECU_ID_MOCK::ENGINE_ECU_TX, ECU_ID_MOCK::ENGINE_ECU_RX );
    ecus[0].mSupportedPIDResponse1 = { 0x41, 0x00, 0x18, 0x00, 0x00, 0x00 };
    ecus[0].mSupportedPIDResponse2 = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ecus[0].mRequestPID1 = { 0x01, 0x04, 0x05 };
    ecus[0].mPIDResponse1 = { 0x41, 0x04, 0x99, 0x05, 0x6E };
    ecus[0].mDTCResponse = { 0x43, 0x04, 0x01, 0x43, 0x41, 0x96, 0x81, 0x48, 0xC1, 0x48 };
    ecus[0].mThread.create( ecuResponse, &ecus[0] );

    ecus[1].init( ECUID::BROADCAST_ID, ECU_ID_MOCK::TRANSMISSION_ECU_TX, ECU_ID_MOCK::TRANSMISSION_ECU_RX );
    ecus[1].mSupportedPIDResponse1 = { 0x41, 0x00, 0x00, 0x08, 0x00, 0x00 };
    ecus[1].mSupportedPIDResponse2 = { 0x41, 0xC0, 0x80, 0x00, 0x00, 0x00 };
    ecus[1].mRequestPID1 = { 0x01, 0x0D, 0xC1 };
    ecus[1].mPIDResponse1 = { 0x41, 0x0D, 0x23, 0xC1, 0xAA };
    ecus[1].mDTCResponse = { 0x43, 0x00 };
    ecus[1].mThread.create( ecuResponse, &ecus[1] );

    // Request PIDs every 2 seconds, and DTCs every 2 seconds
    constexpr uint32_t obdPIDRequestInterval = 1; // 1 second
    constexpr uint32_t obdDTCRequestInterval = 1; // Request DTC every 1 seconds

    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, false ) );
    ASSERT_TRUE( obdModule.connect() );
    auto decoderDictPtr = initDecoderDictionary();
    // publish decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );
    initInspectionMatrix( obdModule );

    // This is the expected value for PID signals.
    std::map<SignalID, SignalDoubleValue> expectedPIDSignalValue = {
        { toUType( EmissionPIDs::ENGINE_LOAD ), 60 },
        { toUType( EmissionPIDs::ENGINE_COOLANT_TEMPERATURE ), 70 },
        { toUType( EmissionPIDs::VEHICLE_SPEED ), 35 },
        { 0xC1, 0xAA } };
    // Verify produced PID signals are correctly decoded
    CollectedSignal signal;
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );

    // Verify produced DTC Buffer is correct. 4 codes for ECM , 0 codes for TCM
    DTCInfo dtcInfo;
    WAIT_ASSERT_TRUE( obdModule.getActiveDTCBufferPtr()->pop( dtcInfo ) );
    ASSERT_EQ( dtcInfo.mDTCCodes.size(), 4 );
    ASSERT_EQ( dtcInfo.mSID, SID::STORED_DTC );
    ASSERT_EQ( dtcInfo.mDTCCodes[0], "P0143" );
    ASSERT_EQ( dtcInfo.mDTCCodes[1], "C0196" );
    ASSERT_EQ( dtcInfo.mDTCCodes[2], "B0148" );
    ASSERT_EQ( dtcInfo.mDTCCodes[3], "U0148" );
}

TEST_F( OBDOverCANModuleTest, BroadcastRequestsStandardIDs )
{
    // Setup ECU Mock
    ecus = std::vector<ECUMock>( 2 );

    ecus[0].init( ECUID::BROADCAST_ID, ECU_ID_MOCK::ENGINE_ECU_TX, ECU_ID_MOCK::ENGINE_ECU_RX );
    ecus[0].mSupportedPIDResponse1 = { 0x41, 0x00, 0x18, 0x00, 0x00, 0x00 };
    ecus[0].mSupportedPIDResponse2 = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ecus[0].mRequestPID1 = { 0x01, 0x04, 0x05 };
    ecus[0].mPIDResponse1 = { 0x41, 0x04, 0x99, 0x05, 0x6E };
    ecus[0].mDTCResponse = { 0x43, 0x02, 0x01, 0x43, 0x41, 0x96 };
    ecus[0].mThread.create( ecuResponse, &ecus[0] );

    ecus[1].init( ECUID::BROADCAST_ID, ECU_ID_MOCK::TRANSMISSION_ECU_TX, ECU_ID_MOCK::TRANSMISSION_ECU_RX );
    ecus[1].mSupportedPIDResponse1 = { 0x41, 0x00, 0x00, 0x08, 0x00, 0x00 };
    ecus[1].mSupportedPIDResponse2 = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ecus[1].mRequestPID1 = { 0x01, 0x0D };
    ecus[1].mPIDResponse1 = { 0x41, 0x0D, 0x23 };
    ecus[1].mDTCResponse = { 0x43, 0x00 };
    ecus[1].mThread.create( ecuResponse, &ecus[1] );

    // Request PIDs every 2 seconds and no DTC request
    constexpr uint32_t obdPIDRequestInterval = 1; // 1 second
    constexpr uint32_t obdDTCRequestInterval = 0; // no DTC request
    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, true ) );
    ASSERT_TRUE( obdModule.connect() );
    // Create decoder dictionary
    auto decoderDictPtr = initDecoderDictionary();
    // publish decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );

    // Expected value for PID signals
    std::map<SignalID, SignalDoubleValue> expectedPIDSignalValue = {
        { toUType( EmissionPIDs::ENGINE_LOAD ), 60 },
        { toUType( EmissionPIDs::ENGINE_COOLANT_TEMPERATURE ), 70 },
        { toUType( EmissionPIDs::VEHICLE_SPEED ), 35 } };
    // Verify all PID Signals are correctly decoded
    CollectedSignal signal;
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );
}

TEST_F( OBDOverCANModuleTest, BroadcastRequestsExtendedIDs )
{
    // Setup ECU Mock
    ecus = std::vector<ECUMock>( 2 );

    ecus[0].init(
        ECUID::BROADCAST_EXTENDED_ID, ECU_ID_MOCK::ENGINE_ECU_TX_EXTENDED, ECU_ID_MOCK::ENGINE_ECU_RX_EXTENDED );
    ecus[0].mSupportedPIDResponse1 = { 0x41, 0x00, 0x18, 0x00, 0x00, 0x00 };
    ecus[0].mSupportedPIDResponse2 = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ecus[0].mRequestPID1 = { 0x01, 0x04, 0x05 };
    ecus[0].mPIDResponse1 = { 0x41, 0x04, 0x99, 0x05, 0x6E };
    ecus[0].mDTCResponse = { 0x43, 0x02, 0x01, 0x43, 0x41, 0x96 };
    ecus[0].mThread.create( ecuResponse, &ecus[0] );

    ecus[1].init( ECUID::BROADCAST_EXTENDED_ID,
                  ECU_ID_MOCK::TRANSMISSION_ECU_TX_EXTENDED,
                  ECU_ID_MOCK::TRANSMISSION_ECU_RX_EXTENDED );
    ecus[1].mSupportedPIDResponse1 = { 0x41, 0x00, 0x00, 0x08, 0x00, 0x00 };
    ecus[1].mSupportedPIDResponse2 = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ecus[1].mRequestPID1 = { 0x01, 0x0D };
    ecus[1].mPIDResponse1 = { 0x41, 0x0D, 0x23 };
    ecus[1].mDTCResponse = { 0x43, 0x00 };
    ecus[1].mThread.create( ecuResponse, &ecus[1] );

    // Request PIDs every 2 seconds and no DTC request
    constexpr uint32_t obdPIDRequestInterval = 1; // 1 second
    constexpr uint32_t obdDTCRequestInterval = 0; // no DTC request
    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, true ) );
    ASSERT_TRUE( obdModule.connect() );
    // Create decoder dictionary
    auto decoderDictPtr = initDecoderDictionary();
    // publish decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );

    // Expected value for PID signals
    std::map<SignalID, SignalDoubleValue> expectedPIDSignalValue = {
        { toUType( EmissionPIDs::ENGINE_LOAD ), 60 },
        { toUType( EmissionPIDs::ENGINE_COOLANT_TEMPERATURE ), 70 },
        { toUType( EmissionPIDs::VEHICLE_SPEED ), 35 } };
    // Verify all PID Signals are correctly decoded
    CollectedSignal signal;
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );
    WAIT_ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value.value.doubleVal, expectedPIDSignalValue[signal.signalID] );
}
