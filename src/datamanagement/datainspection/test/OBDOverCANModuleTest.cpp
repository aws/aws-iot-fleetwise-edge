
/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "OBDOverCANModule.h"
#include "EnumUtility.h"
#include "OBDDataTypes.h"
#include "OBDOverCANSessionManager.h"
#include "businterfaces/ISOTPOverCANReceiver.h"
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>

#include <linux/can.h>
#include <linux/can/isotp.h>
#include <sys/socket.h>

using namespace Aws::IoTFleetWise::DataInspection;

namespace
{

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
    condition.minimumPublishInterval = 0;
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
initDecoderDictionary()
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
            decoderDictPtr->signalIDsToCollect.insert( pid | ( idx << 8 ) );
        }
        decoderDictPtr->canMessageDecoderMethod[0].emplace( pid, CANMessageDecoderMethod() );
        decoderDictPtr->canMessageDecoderMethod[0][pid].format = format;
    }
    return decoderDictPtr;
}

} // namespace

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
    }
};

TEST_F( OBDOverCANModuleTest, OBDOverCANModuleInitFailure )
{
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 256 );
    auto activeDTCBufferPtr = std::make_shared<ActiveDTCBuffer>( 256 );
    OBDOverCANModule obdModule;
    const uint32_t obdPIDRequestInterval = 0; // 0 seconds
    const uint32_t obdDTCRequestInterval = 0; // 0 seconds
    ASSERT_FALSE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, true, true ) );
}

TEST_F( OBDOverCANModuleTest, OBDOverCANSessionManagerInitTest )
{
    OBDOverCANSessionManager sessionManager;

    ASSERT_TRUE( sessionManager.init( "vcan0" ) );
    ASSERT_TRUE( sessionManager.connect() );
    ASSERT_TRUE( sessionManager.disconnect() );
}

TEST_F( OBDOverCANModuleTest, OBDOverCANSessionManagerAssertMessageSent )
{
    // Setup a receiver on the Bus
    ISOTPOverCANReceiver receiver;
    ISOTPOverCANReceiverOptions receiverOptions;
    receiverOptions.mSocketCanIFName = "vcan0";
    receiverOptions.mSourceCANId = toUType( ECUID::ENGINE_ECU_RX );
    receiverOptions.mDestinationCANId = toUType( ECUID::BROADCAST_ID );
    ASSERT_TRUE( receiver.init( receiverOptions ) );
    ASSERT_TRUE( receiver.connect() );
    std::vector<uint8_t> rxPDUData;
    // Setup the session manager and start it.
    // expect a CAN Message to be sent every OBD_KEEP_ALIVE_SECONDS interval
    OBDOverCANSessionManager sessionManager;
    ASSERT_TRUE( sessionManager.init( "vcan0" ) );
    ASSERT_TRUE( sessionManager.connect() );
    ASSERT_TRUE( sessionManager.sendHeartBeat() );
    ASSERT_TRUE( receiver.receivePDU( rxPDUData ) );
    ASSERT_TRUE( rxPDUData[0] == 0x01 );
    ASSERT_TRUE( rxPDUData[1] == 0x00 );
    rxPDUData.clear();
    // Cleanup
    ASSERT_TRUE( sessionManager.disconnect() );
    ASSERT_TRUE( receiver.disconnect() );
}

TEST_F( OBDOverCANModuleTest, OBDOverCANModuleAssert29BitMessageSent )
{
    ISOTPOverCANSenderReceiver engineECU;
    ISOTPOverCANSenderReceiverOptions engineECUOptions;

    // Engine ECU
    engineECUOptions.mSocketCanIFName = "vcan0";
    engineECUOptions.mSourceCANId = toUType( ECUID::ENGINE_ECU_RX_EXTENDED );
    engineECUOptions.mIsExtendedId = true;
    engineECUOptions.mDestinationCANId = toUType( ECUID::ENGINE_ECU_TX_EXTENDED );
    engineECUOptions.mP2TimeoutMs = P2_TIMEOUT_INFINITE;
    ASSERT_TRUE( engineECU.init( engineECUOptions ) );
    ASSERT_TRUE( engineECU.connect() );

    auto signalBufferPtr = std::make_shared<SignalBuffer>( 256 );
    auto activeDTCBufferPtr = std::make_shared<ActiveDTCBuffer>( 256 );
    OBDOverCANModule obdModule;
    std::vector<uint8_t> rxPDUData;
    std::vector<uint8_t> ecmTxPDUData;
    ecmTxPDUData = { 0x49, 0x02, 0x01, 0x31 };
    const uint32_t obdPIDRequestInterval = 2; // 0 seconds
    const uint32_t obdDTCRequestInterval = 2; // 0 seconds
    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, true, false ) );
    ASSERT_TRUE( obdModule.connect() );
    // Create decoder dictionary
    auto decoderDictPtr = initDecoderDictionary();
    // publish decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    ASSERT_TRUE( engineECU.receivePDU( rxPDUData ) );
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    // Cleanup
    ASSERT_TRUE( engineECU.disconnect() );
    ASSERT_TRUE( obdModule.disconnect() );
}

TEST_F( OBDOverCANModuleTest, OBDOverCANModuleInitTestSuccess_LinuxCANDep )
{
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 256 );
    auto activeDTCBufferPtr = std::make_shared<ActiveDTCBuffer>( 256 );
    OBDOverCANModule obdModule;
    const uint32_t obdPIDRequestInterval = 2; // 2 seconds
    const uint32_t obdDTCRequestInterval = 2; // 2 seconds
    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, true, true ) );
    ASSERT_TRUE( obdModule.connect() );
    ASSERT_TRUE( obdModule.disconnect() );
}

TEST_F( OBDOverCANModuleTest, OBDOverCANModuleInitTestFailure )
{
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 256 );
    auto activeDTCBufferPtr = std::make_shared<ActiveDTCBuffer>( 256 );
    OBDOverCANModule obdModule;
    const uint32_t obdPIDRequestInterval = 0; // 2 seconds
    const uint32_t obdDTCRequestInterval = 0; // 2 seconds
    ASSERT_FALSE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, true, true ) );
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
    std::vector<uint8_t> ecmTxPDUData;
    std::vector<uint8_t> expectedECMPIDs;
    const uint32_t obdPIDRequestInterval = 2; // 2 seconds
    const uint32_t obdDTCRequestInterval = 2; // 2 seconds

    ISOTPOverCANSenderReceiver engineECU;
    ISOTPOverCANSenderReceiverOptions engineECUOptions;

    // Engine ECU
    engineECUOptions.mSocketCanIFName = "vcan0";
    engineECUOptions.mSourceCANId = toUType( ECUID::ENGINE_ECU_RX );
    engineECUOptions.mDestinationCANId = toUType( ECUID::ENGINE_ECU_TX );
    ASSERT_TRUE( engineECU.init( engineECUOptions ) );
    ASSERT_TRUE( engineECU.connect() );
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 256 );
    auto activeDTCBufferPtr = std::make_shared<ActiveDTCBuffer>( 256 );
    OBDOverCANModule obdModule;
    ASSERT_TRUE(
        obdModule.init( signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval ) );
    ASSERT_TRUE( obdModule.connect() );
    // No Requests should be seen on the bus.
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    ASSERT_FALSE( engineECU.receivePDU( ecmRxPDUData ) );
    // Now activate the decoder manifest and observe that the module has sent a request
    // Create decoder dictionary
    auto decoderDictPtr = initDecoderDictionary();
    // publish decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    ASSERT_TRUE( engineECU.disconnect() );
    ASSERT_TRUE( obdModule.disconnect() );
}

TEST_F( OBDOverCANModuleTest, OBDOverCANModuleRequestVIN )
{
    std::vector<uint8_t> ecmRxPDUData;
    std::vector<uint8_t> ecmTxPDUData;
    std::vector<uint8_t> expectedECMPIDs;
    const uint32_t obdPIDRequestInterval = 2; // 2 seconds
    const uint32_t obdDTCRequestInterval = 2; // 2 seconds

    ISOTPOverCANSenderReceiver engineECU;
    ISOTPOverCANSenderReceiverOptions engineECUOptions;

    // Engine ECU
    engineECUOptions.mSocketCanIFName = "vcan0";
    engineECUOptions.mSourceCANId = toUType( ECUID::ENGINE_ECU_RX );
    engineECUOptions.mDestinationCANId = toUType( ECUID::ENGINE_ECU_TX );
    engineECUOptions.mP2TimeoutMs = P2_TIMEOUT_INFINITE;
    ASSERT_TRUE( engineECU.init( engineECUOptions ) );
    ASSERT_TRUE( engineECU.connect() );
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 256 );
    auto activeDTCBufferPtr = std::make_shared<ActiveDTCBuffer>( 256 );
    OBDOverCANModule obdModule;
    ASSERT_TRUE(
        obdModule.init( signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval ) );
    ASSERT_TRUE( obdModule.connect() );
    // Create decoder dictionary
    auto decoderDictPtr = initDecoderDictionary();
    // publish decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );
    // Wait for OBD module to send out VIN request
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // Received VIN request for SID 0x09 and PID 0x02 from FWE
    ASSERT_TRUE( ecmRxPDUData[0] == toUType( vehicleIdentificationNumberRequest.mSID ) );
    ASSERT_TRUE( ecmRxPDUData[1] == vehicleIdentificationNumberRequest.mPID );
    // Respond
    ecmTxPDUData = { 0x49, 0x02, 0x01, 0x31, 0x47, 0x31, 0x4A, 0x43, 0x35, 0x34,
                     0x34, 0x34, 0x52, 0x37, 0x32, 0x35, 0x32, 0x33, 0x36, 0x37 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    std::string vin;
    ASSERT_TRUE( obdModule.getVIN( vin ) );
    ASSERT_EQ( vin, "1G1JC5444R7252367" );
    ASSERT_TRUE( engineECU.disconnect() );
    ASSERT_TRUE( obdModule.disconnect() );
}

// This test aim to validate this module can handle invalid response from ECU when requesting supported PID range
TEST_F( OBDOverCANModuleTest, OBDOverCANModuleRequestEmissionInvalidSupportedPIDsTest )
{
    std::vector<uint8_t> ecmRxPDUData;
    std::vector<uint8_t> ecmTxPDUData;
    std::vector<uint8_t> expectedECMPIDs;
    const uint32_t obdPIDRequestInterval = 2; // 2 seconds
    const uint32_t obdDTCRequestInterval = 2; // 2 seconds

    ISOTPOverCANSenderReceiver engineECU;
    ISOTPOverCANSenderReceiverOptions engineECUOptions;
    // Engine ECU
    engineECUOptions.mSocketCanIFName = "vcan0";
    engineECUOptions.mSourceCANId = toUType( ECUID::ENGINE_ECU_RX );
    engineECUOptions.mDestinationCANId = toUType( ECUID::ENGINE_ECU_TX );
    engineECUOptions.mP2TimeoutMs = P2_TIMEOUT_INFINITE;
    ASSERT_TRUE( engineECU.init( engineECUOptions ) );
    ASSERT_TRUE( engineECU.connect() );

    OBDOverCANModule obdModule;
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 256 );
    auto activeDTCBufferPtr = std::make_shared<ActiveDTCBuffer>( 256 );
    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, false, true ) );
    ASSERT_TRUE( obdModule.connect() );
    // Create decoder dictionary
    auto decoderDictPtr = initDecoderDictionary();
    // publish decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );
    // Wait for OBD module to send out VIN request
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    // Respond to VIN request
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // Received VIN request for SID 0x09 and PID 0x02 from FWE
    ASSERT_TRUE( ecmRxPDUData[0] == toUType( vehicleIdentificationNumberRequest.mSID ) );
    ASSERT_TRUE( ecmRxPDUData[1] == vehicleIdentificationNumberRequest.mPID );

    // Respond
    ecmTxPDUData = { 0x49, 0x02, 0x01, 0x31, 0x47, 0x31, 0x4A, 0x43, 0x35, 0x34,
                     0x34, 0x34, 0x52, 0x37, 0x32, 0x35, 0x32, 0x33, 0x36, 0x37 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    // Make sure we wait for the request to arrive from the OBD Module thread
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    // Wait for the Supported PID request to come it to come.
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // Received Supported PID request for SID 0x01 from FWE
    ASSERT_TRUE( ecmRxPDUData[0] == toUType( SID::CURRENT_STATS ) );
    ASSERT_TRUE( ecmRxPDUData[1] == 0x00 );
    // Respond
    // In the ECU response below, the second PID is invalid.
    ecmTxPDUData = { 0x41, 0x00, 0xBF, 0xBF, 0xA8, 0x91, 0x10, 0xFF, 0xFF, 0xFF, 0xFF };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    ecmTxPDUData = { 0x7F, 0x01, 0x12 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    // flush the socket buffer before going to next cycle
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // Mock ECU ignore 0x01, 0xC0, 0xE0, need to wait until it times out
    std::this_thread::sleep_for( std::chrono::milliseconds( obdPIDRequestInterval ) );
    // wait until ECU receive another message so that we know the requestReceiveSupportedPIDs is complete
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    SupportedPIDs enginePIDs;
    ASSERT_TRUE( obdModule.getPIDsToRequest( SID::CURRENT_STATS, ECUType::ENGINE, enginePIDs ) );

    // Expected Result
    // The list should not include PID above 0x20
    expectedECMPIDs = { 0x01,
                        0x03,
                        0x04,
                        0x05,
                        0x06,
                        0x07,
                        0x08,
                        0x09,
                        0x0B,
                        0x0C,
                        0x0D,
                        0x0E,
                        0x0F,
                        0x10,
                        0x11,
                        0x13,
                        0x15,
                        0x19,
                        0x1C };
    ASSERT_EQ( enginePIDs, expectedECMPIDs );
    // Cleanup
    ASSERT_TRUE( engineECU.disconnect() );
    ASSERT_TRUE( obdModule.disconnect() );
}

TEST_F( OBDOverCANModuleTest, OBDOverCANModuleRequestEmissionSupportedPIDsTest )
{
    std::vector<uint8_t> ecmRxPDUData;
    std::vector<uint8_t> ecmTxPDUData;
    std::vector<uint8_t> tcmRxPDUData;
    std::vector<uint8_t> tcmTxPDUData;
    std::vector<uint8_t> expectedECMPIDs;
    std::vector<uint8_t> expectedTCMPIDs;
    const uint32_t obdPIDRequestInterval = 2; // 2 seconds
    const uint32_t obdDTCRequestInterval = 2; // 2 seconds

    ISOTPOverCANSenderReceiver engineECU;
    ISOTPOverCANSenderReceiverOptions engineECUOptions;
    ISOTPOverCANSenderReceiver transmissionECU;
    ISOTPOverCANSenderReceiverOptions transmissionECUOptions;
    // Engine ECU
    engineECUOptions.mSocketCanIFName = "vcan0";
    engineECUOptions.mSourceCANId = toUType( ECUID::ENGINE_ECU_RX );
    engineECUOptions.mDestinationCANId = toUType( ECUID::ENGINE_ECU_TX );
    engineECUOptions.mP2TimeoutMs = P2_TIMEOUT_INFINITE;
    ASSERT_TRUE( engineECU.init( engineECUOptions ) );
    ASSERT_TRUE( engineECU.connect() );

    OBDOverCANModule obdModule;
    auto signalBufferPtr = std::make_shared<SignalBuffer>( 256 );
    auto activeDTCBufferPtr = std::make_shared<ActiveDTCBuffer>( 256 );
    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, false, true ) );
    ASSERT_TRUE( obdModule.connect() );
    // Create decoder dictionary
    auto decoderDictPtr = initDecoderDictionary();
    // publish decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );
    // Wait for OBD module to send out VIN request
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    // Respond to VIN request
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // Received VIN request for SID 0x09 and PID 0x02 from FWE
    ASSERT_TRUE( ecmRxPDUData[0] == toUType( vehicleIdentificationNumberRequest.mSID ) );
    ASSERT_TRUE( ecmRxPDUData[1] == vehicleIdentificationNumberRequest.mPID );
    // Transmission ECU
    transmissionECUOptions.mSocketCanIFName = "vcan0";
    transmissionECUOptions.mSourceCANId = toUType( ECUID::TRANSMISSION_ECU_RX );
    transmissionECUOptions.mDestinationCANId = toUType( ECUID::TRANSMISSION_ECU_TX );
    transmissionECUOptions.mP2TimeoutMs = P2_TIMEOUT_INFINITE;
    ASSERT_TRUE( transmissionECU.init( transmissionECUOptions ) );
    ASSERT_TRUE( transmissionECU.connect() );
    // Respond
    ecmTxPDUData = { 0x49, 0x02, 0x01, 0x31, 0x47, 0x31, 0x4A, 0x43, 0x35, 0x34,
                     0x34, 0x34, 0x52, 0x37, 0x32, 0x35, 0x32, 0x33, 0x36, 0x37 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    // Make sure we wait for the request to arrive from the OBD Module thread
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    // Wait for the Supported PID request to come it to come.
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // Received Supported PID request for SID 0x01 from FWE
    ASSERT_TRUE( ecmRxPDUData[0] == toUType( SID::CURRENT_STATS ) );
    ASSERT_TRUE( ecmRxPDUData[1] == 0x00 );
    // Respond
    ecmTxPDUData = { 0x41, 0x00, 0xBF, 0xBF, 0xA8, 0x91, 0x20, 0x80, 0x00, 0x00, 0x00 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    ecmTxPDUData = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    // Transmission ECU Handling
    // The Supported PID request has now be issued.
    ASSERT_TRUE( transmissionECU.receivePDU( tcmRxPDUData ) );
    // Received Supported PID request for SID 0x01 from FWE
    ASSERT_TRUE( tcmRxPDUData[0] == toUType( SID::CURRENT_STATS ) );
    ASSERT_TRUE( tcmRxPDUData[1] == 0x00 );
    // Respond
    tcmTxPDUData = { 0x41, 0x00, 0x80, 0x08, 0x00, 0x00 };
    ASSERT_TRUE( transmissionECU.sendPDU( tcmTxPDUData ) );
    tcmTxPDUData = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ASSERT_TRUE( transmissionECU.sendPDU( tcmTxPDUData ) );
    // flush the socket buffer before going to next cycle
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    ASSERT_TRUE( transmissionECU.receivePDU( tcmRxPDUData ) );
    // Make sure the OBDModule thread has processed the response.
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    SupportedPIDs enginePIDs, transmissionPIDs;
    ASSERT_TRUE( obdModule.getPIDsToRequest( SID::CURRENT_STATS, ECUType::ENGINE, enginePIDs ) );

    // Expected Result
    // The list should not include the Supported PID Ids.
    expectedECMPIDs = { 0x01, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0B, 0x0C,
                        0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x13, 0x15, 0x19, 0x1C, 0x21 };
    ASSERT_EQ( enginePIDs, expectedECMPIDs );
    expectedTCMPIDs = { 0x01, 0x0D };
    ASSERT_TRUE( obdModule.getPIDsToRequest( SID::CURRENT_STATS, ECUType::TRANSMISSION, transmissionPIDs ) );
    ASSERT_EQ( transmissionPIDs, expectedTCMPIDs );
    // Cleanup
    ASSERT_TRUE( engineECU.disconnect() );
    ASSERT_TRUE( transmissionECU.disconnect() );
    ASSERT_TRUE( obdModule.disconnect() );
}

TEST_F( OBDOverCANModuleTest, OBDOverCANModuleRequestAllEmissionPIDDataTest )
{
    std::vector<uint8_t> ecmRxPDUData;
    std::vector<uint8_t> ecmTxPDUData;
    std::vector<uint8_t> tcmRxPDUData;
    std::vector<uint8_t> tcmTxPDUData;
    std::vector<uint8_t> expectedECMData;
    std::vector<uint8_t> expectedTCMData;
    std::vector<uint8_t> expectedECMPIDs;
    std::vector<uint8_t> expectedTCMPIDs;
    const uint32_t obdPIDRequestInterval = 2; // 2 seconds
    const uint32_t obdDTCRequestInterval = 0; // no DTC request

    ISOTPOverCANSenderReceiver engineECU;
    ISOTPOverCANSenderReceiverOptions engineECUOptions;
    ISOTPOverCANSenderReceiver transmissionECU;
    ISOTPOverCANSenderReceiverOptions transmissionECUOptions;
    // Engine ECU
    engineECUOptions.mSocketCanIFName = "vcan0";
    engineECUOptions.mSourceCANId = toUType( ECUID::ENGINE_ECU_RX );
    engineECUOptions.mDestinationCANId = toUType( ECUID::ENGINE_ECU_TX );
    engineECUOptions.mP2TimeoutMs = P2_TIMEOUT_INFINITE;
    ASSERT_TRUE( engineECU.init( engineECUOptions ) );
    ASSERT_TRUE( engineECU.connect() );

    auto signalBufferPtr = std::make_shared<SignalBuffer>( 256 );
    auto activeDTCBufferPtr = std::make_shared<ActiveDTCBuffer>( 256 );
    OBDOverCANModule obdModule;
    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, false, true ) );
    ASSERT_TRUE( obdModule.connect() );
    // Create decoder dictionary
    auto decoderDictPtr = initDecoderDictionary();
    // publish decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );
    // Wait for OBD module to send out VIN request
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    // Respond to VIN request
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // Received VIN request for SID 0x09 and PID 0x02 from FWE
    ASSERT_TRUE( ecmRxPDUData[0] == toUType( vehicleIdentificationNumberRequest.mSID ) );
    ASSERT_TRUE( ecmRxPDUData[1] == vehicleIdentificationNumberRequest.mPID );
    // Transmission ECU
    transmissionECUOptions.mSocketCanIFName = "vcan0";
    transmissionECUOptions.mSourceCANId = toUType( ECUID::TRANSMISSION_ECU_RX );
    transmissionECUOptions.mDestinationCANId = toUType( ECUID::TRANSMISSION_ECU_TX );
    transmissionECUOptions.mP2TimeoutMs = P2_TIMEOUT_INFINITE;
    ASSERT_TRUE( transmissionECU.init( transmissionECUOptions ) );
    ASSERT_TRUE( transmissionECU.connect() );
    // Respond
    ecmTxPDUData = { 0x49, 0x02, 0x01, 0x31, 0x47, 0x31, 0x4A, 0x43, 0x35, 0x34,
                     0x34, 0x34, 0x52, 0x37, 0x32, 0x35, 0x32, 0x33, 0x36, 0x37 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    // Make sure we wait for the request to arrive from the OBD Module thread
    // Wait for it to come.
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // Received Supported PID request for SID 0x01 from FWE
    ASSERT_TRUE( ecmRxPDUData[0] == toUType( SID::CURRENT_STATS ) );
    ASSERT_TRUE( ecmRxPDUData[1] == 0x00 );
    // Respond
    // Support PID 0x04,0x05
    ecmTxPDUData = { 0x41, 0x00, 0x18, 0x00, 0x00, 0x00 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    ecmTxPDUData = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );

    // Transmission ECU Handling
    // The Supported PID request has now be issued.
    ASSERT_TRUE( transmissionECU.receivePDU( tcmRxPDUData ) );
    // Received Supported PID request for SID 0x01 from FWE
    ASSERT_TRUE( tcmRxPDUData[0] == toUType( SID::CURRENT_STATS ) );
    ASSERT_TRUE( tcmRxPDUData[1] == 0x00 );
    // Respond
    // Support PID 0x0D
    tcmTxPDUData = { 0x41, 0x00, 0x00, 0x08, 0x00, 0x00, 0x40, 0x04, 0x00, 0x00, 0x00 };
    ASSERT_TRUE( transmissionECU.sendPDU( tcmTxPDUData ) );
    tcmTxPDUData = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ASSERT_TRUE( transmissionECU.sendPDU( tcmTxPDUData ) );
    // flush the socket buffer before going to next cycle
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    ASSERT_TRUE( transmissionECU.receivePDU( tcmRxPDUData ) );
    // Make sure the OBDModule thread has processed the response.
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    SupportedPIDs enginePIDs, transmissionPIDs;
    ASSERT_TRUE( obdModule.getPIDsToRequest( SID::CURRENT_STATS, ECUType::ENGINE, enginePIDs ) );
    ASSERT_TRUE( obdModule.getPIDsToRequest( SID::CURRENT_STATS, ECUType::TRANSMISSION, transmissionPIDs ) );

    ASSERT_EQ( enginePIDs[0], 0x04 );
    ASSERT_EQ( enginePIDs[1], 0x05 );
    ASSERT_EQ( transmissionPIDs[0], 0x0D );
    ASSERT_EQ( transmissionPIDs[1], 0x46 ); // ECU respond 0x40, 0x04, 0x00, 0x00, 0x00
    // Receive any PID Data request
    ecmRxPDUData.clear();
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // expect SID 1 and 2 PIDs
    expectedECMPIDs = { 0x01, 0x04, 0x05 };
    ASSERT_EQ( expectedECMPIDs, ecmRxPDUData );
    // Respond to FWE with the requested PIDs.
    // ECM , 60% Engine load, 70 degrees Temperature
    ecmTxPDUData = { 0x41, 0x04, 0x99, 0x05, 0x6E };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );

    tcmRxPDUData.clear();
    ASSERT_TRUE( transmissionECU.receivePDU( tcmRxPDUData ) );
    // expect SID 1 and 1 PID
    expectedTCMPIDs = { 0x01, 0x0D, 0x46 };
    ASSERT_EQ( expectedTCMPIDs, tcmRxPDUData );
    // TCM, Vehicle speed of 35 kph
    tcmTxPDUData = { 0x41, 0x0D, 0x23, 0x46, 0x40 };
    ASSERT_TRUE( transmissionECU.sendPDU( tcmTxPDUData ) );
    // Wait till the OBD Module receives the data
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );

    // Expected value for PID signals
    std::map<SignalID, SignalValue> expectedPIDSignalValue = {
        { toUType( EmissionPIDs::ENGINE_LOAD ), 60 },
        { toUType( EmissionPIDs::ENGINE_COOLANT_TEMPERATURE ), 70 },
        { toUType( EmissionPIDs::VEHICLE_SPEED ), 35 },
        { toUType( EmissionPIDs::AMBIENT_AIR_TEMPERATURE ), 24 } };
    // Verify all PID Signals are correctly decoded
    CollectedSignal signal;
    ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value, expectedPIDSignalValue[signal.signalID] );
    ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value, expectedPIDSignalValue[signal.signalID] );
    ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value, expectedPIDSignalValue[signal.signalID] );
    ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value, expectedPIDSignalValue[signal.signalID] );

    // Cleanup
    ASSERT_TRUE( engineECU.disconnect() );
    ASSERT_TRUE( transmissionECU.disconnect() );
    ASSERT_TRUE( obdModule.disconnect() );
}

// This test is to validate that OBDOverCANModule will only request PID that are specified in Decoder Dictionary
// In this test scenario, the decoder dictionary will request signals from PIDs 0x04, 0x14, 0x0D, 0x15, 0x16, 0x17
// Engine ECU supports PIDs 0x04,0x05,0x09,0x11,0x12,0x13,0x14
// Transmission ECU supports PIDs 0x0D
// Hence Edge Agent will only request 0x04, 0x14 to Engine ECU and 0x0D to Transmission ECU
TEST_F( OBDOverCANModuleTest, DecoderDictionaryOnlyRequstPartialEmissionPIDTest )
{
    std::vector<uint8_t> ecmRxPDUData;
    std::vector<uint8_t> ecmTxPDUData;
    std::vector<uint8_t> tcmRxPDUData;
    std::vector<uint8_t> tcmTxPDUData;
    std::vector<uint8_t> expectedECMData;
    std::vector<uint8_t> expectedTCMData;
    std::vector<uint8_t> expectedECMPIDs;
    std::vector<uint8_t> expectedTCMPIDs;
    const uint32_t obdPIDRequestInterval = 2; // 2 seconds
    const uint32_t obdDTCRequestInterval = 0; // no DTC request

    ISOTPOverCANSenderReceiver engineECU;
    ISOTPOverCANSenderReceiverOptions engineECUOptions;
    ISOTPOverCANSenderReceiver transmissionECU;
    ISOTPOverCANSenderReceiverOptions transmissionECUOptions;
    // Engine ECU
    engineECUOptions.mSocketCanIFName = "vcan0";
    engineECUOptions.mSourceCANId = toUType( ECUID::ENGINE_ECU_RX );
    engineECUOptions.mDestinationCANId = toUType( ECUID::ENGINE_ECU_TX );
    engineECUOptions.mP2TimeoutMs = P2_TIMEOUT_INFINITE;
    ASSERT_TRUE( engineECU.init( engineECUOptions ) );
    ASSERT_TRUE( engineECU.connect() );

    auto signalBufferPtr = std::make_shared<SignalBuffer>( 256 );
    auto activeDTCBufferPtr = std::make_shared<ActiveDTCBuffer>( 256 );
    OBDOverCANModule obdModule;
    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, false, true ) );
    ASSERT_TRUE( obdModule.connect() );
    // create decoder dictionary
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
    // publish the decoder dictionary
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );

    // Wait for OBD module to send out VIN request
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    // Respond to VIN request
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // Received VIN request for SID 0x09 and PID 0x02 from FWE
    ASSERT_TRUE( ecmRxPDUData[0] == toUType( vehicleIdentificationNumberRequest.mSID ) );
    ASSERT_TRUE( ecmRxPDUData[1] == vehicleIdentificationNumberRequest.mPID );
    // Transmission ECU
    transmissionECUOptions.mSocketCanIFName = "vcan0";
    transmissionECUOptions.mSourceCANId = toUType( ECUID::TRANSMISSION_ECU_RX );
    transmissionECUOptions.mDestinationCANId = toUType( ECUID::TRANSMISSION_ECU_TX );
    transmissionECUOptions.mP2TimeoutMs = P2_TIMEOUT_INFINITE;
    ASSERT_TRUE( transmissionECU.init( transmissionECUOptions ) );
    ASSERT_TRUE( transmissionECU.connect() );
    // Respond
    ecmTxPDUData = { 0x49, 0x02, 0x01, 0x31, 0x47, 0x31, 0x4A, 0x43, 0x35, 0x34,
                     0x34, 0x34, 0x52, 0x37, 0x32, 0x35, 0x32, 0x33, 0x36, 0x37 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    // Make sure we wait for the request to arrive from the OBD Module thread
    // Wait for it to come.
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // Received Supported PID request for SID 0x01 from FWE
    ASSERT_TRUE( ecmRxPDUData[0] == toUType( SID::CURRENT_STATS ) );
    ASSERT_TRUE( ecmRxPDUData[1] == 0x00 );
    // Respond
    // Support PID 0x04,0x05,0x09,0x11,0x12,0x13,0x14
    ecmTxPDUData = { 0x41, 0x00, 0x18, 0x80, 0xF0, 0x00 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    ecmTxPDUData = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    // Transmission ECU Handling
    // The Supported PID request has now be issued.
    ASSERT_TRUE( transmissionECU.receivePDU( tcmRxPDUData ) );
    // Received Supported PID request for SID 0x01 from FWE
    ASSERT_TRUE( tcmRxPDUData[0] == toUType( SID::CURRENT_STATS ) );
    ASSERT_TRUE( tcmRxPDUData[1] == 0x00 );
    // Respond
    // Support PID 0x0D
    tcmTxPDUData = { 0x41, 0x00, 0x00, 0x08, 0x00, 0x00 };
    ASSERT_TRUE( transmissionECU.sendPDU( tcmTxPDUData ) );
    tcmTxPDUData = { 0x41, 0xC0, 0x80, 0x10, 0x00, 0x00 };
    ASSERT_TRUE( transmissionECU.sendPDU( tcmTxPDUData ) );
    // flush the socket buffer before going to next cycle
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    ASSERT_TRUE( transmissionECU.receivePDU( tcmRxPDUData ) );
    // Make sure the OBDModule thread has processed the response.
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    SupportedPIDs enginePIDs, transmissionPIDs;
    ASSERT_TRUE( obdModule.getPIDsToRequest( SID::CURRENT_STATS, ECUType::ENGINE, enginePIDs ) );
    ASSERT_TRUE( obdModule.getPIDsToRequest( SID::CURRENT_STATS, ECUType::TRANSMISSION, transmissionPIDs ) );

    // 0x05, 0x09, 0x11, 0x12, 0x13 should not be requested as it's not required by decoder dictionary
    ASSERT_EQ( enginePIDs[0], 0x04 );
    // 0x14 shall be requested as it contains signals in decoder dictionary
    ASSERT_EQ( enginePIDs[1], 0x14 );
    ASSERT_EQ( transmissionPIDs[0], 0x0D );
    ASSERT_EQ( transmissionPIDs[1], 0xC1 );
    // Receive any PID Data request
    ecmRxPDUData.clear();
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // expect SID 1 and 2 PIDs
    // 0x05 should not be requested as it's not required by decoder dictionary
    expectedECMPIDs = { 0x01, 0x04, 0x14 };
    ASSERT_EQ( expectedECMPIDs, ecmRxPDUData );
    // Respond to FWE with the requested PIDs.
    // ECM , Engine load, O2 Sensor 1
    ecmTxPDUData = { 0x41, 0x04, 0x99, 0x14, 0x10, 0x20 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );

    tcmRxPDUData.clear();
    ASSERT_TRUE( transmissionECU.receivePDU( tcmRxPDUData ) );
    // expect SID 1 and 1 PID
    expectedTCMPIDs = { 0x01, 0x0D, 0xC1 };
    ASSERT_EQ( expectedTCMPIDs, tcmRxPDUData );
    // TCM, Vehicle speed of 35 kph
    tcmTxPDUData = { 0x41, 0x0D, 0x23, 0xC1, 0xAA };
    ASSERT_TRUE( transmissionECU.sendPDU( tcmTxPDUData ) );
    // Wait till the OBD Module receives the data
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );

    // Expected value for PID signals
    std::map<SignalID, SignalValue> expectedPIDSignalValue = {
        { toUType( EmissionPIDs::ENGINE_LOAD ), 60 },
        { toUType( EmissionPIDs::OXYGEN_SENSOR1_1 ) | 0 << 8, (double)0x10 / 200 },
        { toUType( EmissionPIDs::OXYGEN_SENSOR1_1 ) | 1 << 8, (double)0x20 * 100 / 128 - 100 },
        { toUType( EmissionPIDs::VEHICLE_SPEED ), 35 },
        { 0xC1, 0xAA } };
    // Verify all PID Signals are correctly decoded
    CollectedSignal signal;
    for ( size_t idx = 0; idx < expectedPIDSignalValue.size(); ++idx )
    {
        ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
        ASSERT_DOUBLE_EQ( signal.value, expectedPIDSignalValue[signal.signalID] );
    }
    ASSERT_TRUE( obdModule.getSignalBufferPtr()->empty() );

    // Cleanup
    ASSERT_TRUE( engineECU.disconnect() );
    ASSERT_TRUE( transmissionECU.disconnect() );
    ASSERT_TRUE( obdModule.disconnect() );
}

// This test is to validate that OBDOverCANModule can udpate the PID request list when receiving new decoder manifest
// In this test scenario, decoder dictionary will first request PID 0x04, 0x14 and 0x0D; then it will switch
// to 0x05, 0x14 and 0x0C.
TEST_F( OBDOverCANModuleTest, DecoderDictionaryUpdatePIDsToCollectTest )
{
    std::vector<uint8_t> ecmRxPDUData;
    std::vector<uint8_t> ecmTxPDUData;
    std::vector<uint8_t> tcmRxPDUData;
    std::vector<uint8_t> tcmTxPDUData;
    std::vector<uint8_t> expectedECMData;
    std::vector<uint8_t> expectedTCMData;
    std::vector<uint8_t> expectedECMPIDs;
    std::vector<uint8_t> expectedTCMPIDs;
    const uint32_t obdPIDRequestInterval = 2; // 2 seconds
    const uint32_t obdDTCRequestInterval = 0; // no DTC request

    ISOTPOverCANSenderReceiver engineECU;
    ISOTPOverCANSenderReceiverOptions engineECUOptions;
    ISOTPOverCANSenderReceiver transmissionECU;
    ISOTPOverCANSenderReceiverOptions transmissionECUOptions;
    // Engine ECU
    engineECUOptions.mSocketCanIFName = "vcan0";
    engineECUOptions.mSourceCANId = toUType( ECUID::ENGINE_ECU_RX );
    engineECUOptions.mDestinationCANId = toUType( ECUID::ENGINE_ECU_TX );
    engineECUOptions.mP2TimeoutMs = P2_TIMEOUT_INFINITE;
    ASSERT_TRUE( engineECU.init( engineECUOptions ) );
    ASSERT_TRUE( engineECU.connect() );

    auto signalBufferPtr = std::make_shared<SignalBuffer>( 256 );
    auto activeDTCBufferPtr = std::make_shared<ActiveDTCBuffer>( 256 );
    OBDOverCANModule obdModule;
    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, false, true ) );
    ASSERT_TRUE( obdModule.connect() );

    // Create decoder dictionary
    auto decoderDictPtr = initDecoderDictionary();
    // Specify the signals to collect
    decoderDictPtr->signalIDsToCollect.clear();
    decoderDictPtr->signalIDsToCollect.insert( 0x04 );
    // Oxygen Sensor 1 contains two signals, we want to collect the second signal
    decoderDictPtr->signalIDsToCollect.insert( 0x14 | 1 << 8 );
    decoderDictPtr->signalIDsToCollect.insert( 0x0D );
    // The decoder dictionary request PID 0x15, 0x16, 0x17 but they will not be supported by ECU
    decoderDictPtr->signalIDsToCollect.insert( 0x15 );
    decoderDictPtr->signalIDsToCollect.insert( 0x16 );
    decoderDictPtr->signalIDsToCollect.insert( 0x17 );
    // publish decoder dictionary
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );

    // Wait for OBD module to send out VIN request
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    // Respond to VIN request
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // Received VIN request for SID 0x09 and PID 0x02 from FWE
    ASSERT_TRUE( ecmRxPDUData[0] == toUType( vehicleIdentificationNumberRequest.mSID ) );
    ASSERT_TRUE( ecmRxPDUData[1] == vehicleIdentificationNumberRequest.mPID );
    // Transmission ECU
    transmissionECUOptions.mSocketCanIFName = "vcan0";
    transmissionECUOptions.mSourceCANId = toUType( ECUID::TRANSMISSION_ECU_RX );
    transmissionECUOptions.mDestinationCANId = toUType( ECUID::TRANSMISSION_ECU_TX );
    transmissionECUOptions.mP2TimeoutMs = P2_TIMEOUT_INFINITE;
    ASSERT_TRUE( transmissionECU.init( transmissionECUOptions ) );
    ASSERT_TRUE( transmissionECU.connect() );
    // Respond
    ecmTxPDUData = { 0x49, 0x02, 0x01, 0x31, 0x47, 0x31, 0x4A, 0x43, 0x35, 0x34,
                     0x34, 0x34, 0x52, 0x37, 0x32, 0x35, 0x32, 0x33, 0x36, 0x37 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    // Make sure we wait for the request to arrive from the OBD Module thread
    // Wait for it to come.
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // Received Supported PID request for SID 0x01 from FWE
    ASSERT_TRUE( ecmRxPDUData[0] == toUType( SID::CURRENT_STATS ) );
    ASSERT_TRUE( ecmRxPDUData[1] == 0x00 );
    // Respond
    // Support PID 0x04,0x05,0x09,0x11,0x12,0x13,0x14
    ecmTxPDUData = { 0x41, 0x00, 0x18, 0x80, 0xF0, 0x00 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    ecmTxPDUData = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    // Transmission ECU Handling
    // The Supported PID request has now be issued.
    ASSERT_TRUE( transmissionECU.receivePDU( tcmRxPDUData ) );
    // Received Supported PID request for SID 0x01 from FWE
    ASSERT_TRUE( tcmRxPDUData[0] == toUType( SID::CURRENT_STATS ) );
    ASSERT_TRUE( tcmRxPDUData[1] == 0x00 );
    // Respond
    // Support PID 0x0C 0x0D
    tcmTxPDUData = { 0x41, 0x00, 0x00, 0x18, 0x00, 0x00 };
    ASSERT_TRUE( transmissionECU.sendPDU( tcmTxPDUData ) );
    tcmTxPDUData = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ASSERT_TRUE( transmissionECU.sendPDU( tcmTxPDUData ) );
    // flush the socket buffer before going to next cycle
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    ASSERT_TRUE( transmissionECU.receivePDU( tcmRxPDUData ) );
    // Make sure the OBDModule thread has processed the response.
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    SupportedPIDs enginePIDs, transmissionPIDs;
    ASSERT_TRUE( obdModule.getPIDsToRequest( SID::CURRENT_STATS, ECUType::ENGINE, enginePIDs ) );
    ASSERT_TRUE( obdModule.getPIDsToRequest( SID::CURRENT_STATS, ECUType::TRANSMISSION, transmissionPIDs ) );

    // 0x05, 0x09, 0x11, 0x12, 0x13 should not be requested as it's not required by decoder dictionary
    ASSERT_EQ( enginePIDs[0], 0x04 );
    // 0x14 shall be requested as it contains signals in decoder dictionary
    ASSERT_EQ( enginePIDs[1], 0x14 );
    ASSERT_EQ( transmissionPIDs[0], 0x0D );
    // Receive any PID Data request
    ecmRxPDUData.clear();
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // expect SID 1 and 2 PIDs
    // 0x05 should not be requested as it's not required by decoder dictionary
    expectedECMPIDs = { 0x01, 0x04, 0x14 };
    ASSERT_EQ( expectedECMPIDs, ecmRxPDUData );
    // Respond to FWE with the requested PIDs.
    // ECM , Engine load, O2 Sensor 1
    ecmTxPDUData = { 0x41, 0x04, 0x99, 0x14, 0x10, 0x20 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );

    tcmRxPDUData.clear();
    ASSERT_TRUE( transmissionECU.receivePDU( tcmRxPDUData ) );
    // expect SID 1 and 1 PID
    expectedTCMPIDs = { 0x01, 0x0D };
    ASSERT_EQ( expectedTCMPIDs, tcmRxPDUData );
    // TCM, Vehicle speed of 35 kph
    tcmTxPDUData = { 0x41, 0x0D, 0x23 };
    ASSERT_TRUE( transmissionECU.sendPDU( tcmTxPDUData ) );

    // Update Decoder Dictionary to collect PID 0x05
    decoderDictPtr->signalIDsToCollect.erase( 0x04 );
    decoderDictPtr->signalIDsToCollect.insert( 0x05 );
    decoderDictPtr->signalIDsToCollect.insert( 0x0C );
    decoderDictPtr->signalIDsToCollect.erase( 0x0D );
    decoderDictPtr->signalIDsToCollect.insert( 0x0E );
    // publish the new decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );
    ASSERT_TRUE( obdModule.getPIDsToRequest( SID::CURRENT_STATS, ECUType::ENGINE, enginePIDs ) );
    ASSERT_TRUE( obdModule.getPIDsToRequest( SID::CURRENT_STATS, ECUType::TRANSMISSION, transmissionPIDs ) );

    // Due to new decoder dictionary, we on longer collect PID 0x04 and 0x0D. 0x05 and 0x0C are newly added
    // 0x0E will not be requested as it's not supported by ECU
    ASSERT_EQ( enginePIDs[0], 0x05 );
    ASSERT_EQ( enginePIDs[1], 0x14 );
    ASSERT_EQ( enginePIDs.size(), 2 );
    ASSERT_EQ( transmissionPIDs[0], 0x0C );
    ASSERT_EQ( transmissionPIDs.size(), 1 );

    // Update Decoder Dictionary to not collect any PIDs
    decoderDictPtr->signalIDsToCollect.clear();
    // publish the new decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );
    ASSERT_TRUE( obdModule.getPIDsToRequest( SID::CURRENT_STATS, ECUType::ENGINE, enginePIDs ) );
    ASSERT_TRUE( obdModule.getPIDsToRequest( SID::CURRENT_STATS, ECUType::TRANSMISSION, transmissionPIDs ) );

    // With new decoder dictionary, no PID will be requested
    ASSERT_EQ( enginePIDs.size(), 0 );
    ASSERT_EQ( transmissionPIDs.size(), 0 );

    // Cleanup
    ASSERT_TRUE( engineECU.disconnect() );
    ASSERT_TRUE( transmissionECU.disconnect() );
    ASSERT_TRUE( obdModule.disconnect() );
}

TEST_F( OBDOverCANModuleTest, OBDOverCANModuleRequestPIDAndDTCsTest )
{
    std::vector<uint8_t> ecmRxPDUData;
    std::vector<uint8_t> ecmTxPDUData;
    std::vector<uint8_t> tcmRxPDUData;
    std::vector<uint8_t> tcmTxPDUData;
    std::vector<uint8_t> expectedECMData;
    std::vector<uint8_t> expectedTCMData;
    std::vector<uint8_t> expectedECMPIDs;
    std::vector<uint8_t> expectedTCMPIDs;
    // Request PIDs every 2 seconds, and DTCs every 2 seconds
    const uint32_t obdPIDRequestInterval = 2; // 2 seconds
    const uint32_t obdDTCRequestInterval = 2; // Request anyway

    ISOTPOverCANSenderReceiver engineECU;
    ISOTPOverCANSenderReceiverOptions engineECUOptions;
    ISOTPOverCANSenderReceiver transmissionECU;
    ISOTPOverCANSenderReceiverOptions transmissionECUOptions;
    // Engine ECU
    engineECUOptions.mSocketCanIFName = "vcan0";
    engineECUOptions.mSourceCANId = toUType( ECUID::ENGINE_ECU_RX );
    engineECUOptions.mDestinationCANId = toUType( ECUID::ENGINE_ECU_TX );
    engineECUOptions.mP2TimeoutMs = P2_TIMEOUT_INFINITE;
    ASSERT_TRUE( engineECU.init( engineECUOptions ) );
    ASSERT_TRUE( engineECU.connect() );
    // Transmission ECU
    transmissionECUOptions.mSocketCanIFName = "vcan0";
    transmissionECUOptions.mSourceCANId = toUType( ECUID::TRANSMISSION_ECU_RX );
    transmissionECUOptions.mDestinationCANId = toUType( ECUID::TRANSMISSION_ECU_TX );
    transmissionECUOptions.mP2TimeoutMs = P2_TIMEOUT_INFINITE;
    ASSERT_TRUE( transmissionECU.init( transmissionECUOptions ) );
    ASSERT_TRUE( transmissionECU.connect() );
    // OBD Module
    OBDOverCANModule obdModule;

    auto signalBufferPtr = std::make_shared<SignalBuffer>( 256 );
    auto activeDTCBufferPtr = std::make_shared<ActiveDTCBuffer>( 256 );
    ASSERT_TRUE( obdModule.init(
        signalBufferPtr, activeDTCBufferPtr, "vcan0", obdPIDRequestInterval, obdDTCRequestInterval, false, true ) );
    ASSERT_TRUE( obdModule.connect() );
    // Create decoder dictionary
    auto decoderDictPtr = initDecoderDictionary();
    // publish decoder dictionary to OBD module
    obdModule.onChangeOfActiveDictionary( decoderDictPtr, VehicleDataSourceProtocol::OBD );
    initInspectionMatrix( obdModule );
    // Wait for OBD module to send out VIN request
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    // Respond to VIN request
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // Received VIN request for SID 0x09 and PID 0x02 from FWE
    ASSERT_TRUE( ecmRxPDUData[0] == toUType( vehicleIdentificationNumberRequest.mSID ) );
    ASSERT_TRUE( ecmRxPDUData[1] == vehicleIdentificationNumberRequest.mPID );
    // Respond
    ecmTxPDUData = { 0x49, 0x02, 0x01, 0x31, 0x47, 0x31, 0x4A, 0x43, 0x35, 0x34,
                     0x34, 0x34, 0x52, 0x37, 0x32, 0x35, 0x32, 0x33, 0x36, 0x37 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    // Make sure we wait for the request to arrive from the OBD Module thread
    // Wait for it to come.
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // Received Supported PID request for SID 0x01 from FWE
    ASSERT_TRUE( ecmRxPDUData[0] == toUType( SID::CURRENT_STATS ) );
    ASSERT_TRUE( ecmRxPDUData[1] == 0x00 );
    // Respond
    // Support PID 0x04,0x05
    ecmTxPDUData = { 0x41, 0x00, 0x18, 0x00, 0x00, 0x00 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );
    ecmTxPDUData = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );

    // Transmission ECU Handling
    // The Supported PID request has now be issued.
    ASSERT_TRUE( transmissionECU.receivePDU( tcmRxPDUData ) );
    // Received Supported PID request for SID 0x01 from FWE
    ASSERT_TRUE( tcmRxPDUData[0] == toUType( SID::CURRENT_STATS ) );
    ASSERT_TRUE( tcmRxPDUData[1] == 0x00 );
    // Respond
    // Support PID 0x0D
    tcmTxPDUData = { 0x41, 0x00, 0x00, 0x08, 0x00, 0x00 };
    ASSERT_TRUE( transmissionECU.sendPDU( tcmTxPDUData ) );
    tcmTxPDUData = { 0x41, 0xC0, 0x00, 0x00, 0x00, 0x00 };
    ASSERT_TRUE( transmissionECU.sendPDU( tcmTxPDUData ) );
    // flush the socket buffer before going to next cycle
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    ASSERT_TRUE( transmissionECU.receivePDU( tcmRxPDUData ) );
    // Make sure the OBDModule thread has processed the response.
    std::this_thread::sleep_for( std::chrono::seconds( obdPIDRequestInterval ) );
    // Receive any PID Data request
    ecmRxPDUData.clear();
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    // expect SID 1 and 2 PIDs
    expectedECMPIDs = { 0x01, 0x04, 0x05 };
    ASSERT_EQ( expectedECMPIDs, ecmRxPDUData );
    // Respond to FWE with the requested PIDs.
    // ECM , 60% Engine load, 70 degrees Temperature
    ecmTxPDUData = { 0x41, 0x04, 0x99, 0x05, 0x6E };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );

    tcmRxPDUData.clear();
    ASSERT_TRUE( transmissionECU.receivePDU( tcmRxPDUData ) );
    // TCM, Vehicle speed of 35 kph
    expectedTCMPIDs = { 0x01, 0x0D };
    ASSERT_EQ( expectedTCMPIDs, tcmRxPDUData );
    tcmTxPDUData = { 0x41, 0x0D, 0x23 };
    ASSERT_TRUE( transmissionECU.sendPDU( tcmTxPDUData ) );

    // Handle DTC Requests
    ecmRxPDUData.clear();
    ASSERT_TRUE( engineECU.receivePDU( ecmRxPDUData ) );
    expectedECMPIDs = { toUType( SID::STORED_DTC ) };
    ASSERT_EQ( expectedECMPIDs, ecmRxPDUData );
    // Respond with 4 DTC from ECM
    ecmTxPDUData.clear();
    ecmTxPDUData = { 0x43, 0x04, 0x01, 0x43, 0x41, 0x96, 0x81, 0x48, 0xC1, 0x48 };
    ASSERT_TRUE( engineECU.sendPDU( ecmTxPDUData ) );

    tcmRxPDUData.clear();
    ASSERT_TRUE( transmissionECU.receivePDU( tcmRxPDUData ) );
    // expect SID 3 Stored DTC request
    expectedTCMPIDs = { toUType( SID::STORED_DTC ) };
    ASSERT_EQ( expectedTCMPIDs, tcmRxPDUData );
    // TCM, 0 DTCs
    tcmTxPDUData = { 0x43, 0x00 };
    ASSERT_TRUE( transmissionECU.sendPDU( tcmTxPDUData ) );
    // Wait till the OBD Module receives the data
    std::this_thread::sleep_for( std::chrono::seconds( 1 ) );

    // This is the expected value for PID signals.
    std::map<SignalID, SignalValue> expectedPIDSignalValue = {
        { toUType( EmissionPIDs::ENGINE_LOAD ), 60 },
        { toUType( EmissionPIDs::ENGINE_COOLANT_TEMPERATURE ), 70 },
        { toUType( EmissionPIDs::VEHICLE_SPEED ), 35 } };
    // Verify produced PID signals are correctly decoded
    CollectedSignal signal;
    ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value, expectedPIDSignalValue[signal.signalID] );
    ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value, expectedPIDSignalValue[signal.signalID] );
    ASSERT_TRUE( obdModule.getSignalBufferPtr()->pop( signal ) );
    ASSERT_DOUBLE_EQ( signal.value, expectedPIDSignalValue[signal.signalID] );

    // Verify produced DTC Buffer is correct. 4 codes for ECM , 0 codes for TCM
    DTCInfo dtcInfo;
    ASSERT_TRUE( obdModule.getActiveDTCBufferPtr()->pop( dtcInfo ) );
    ASSERT_EQ( dtcInfo.mDTCCodes.size(), 4 );
    ASSERT_EQ( dtcInfo.mSID, SID::STORED_DTC );
    ASSERT_EQ( dtcInfo.mDTCCodes[0], "P0143" );
    ASSERT_EQ( dtcInfo.mDTCCodes[1], "C0196" );
    ASSERT_EQ( dtcInfo.mDTCCodes[2], "B0148" );
    ASSERT_EQ( dtcInfo.mDTCCodes[3], "U0148" );
    ASSERT_TRUE( obdModule.getActiveDTCBufferPtr()->empty() );

    // Cleanup
    ASSERT_TRUE( engineECU.disconnect() );
    ASSERT_TRUE( transmissionECU.disconnect() );
    ASSERT_TRUE( obdModule.disconnect() );
}
