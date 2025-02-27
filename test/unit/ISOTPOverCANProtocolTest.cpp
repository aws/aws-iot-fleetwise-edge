
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "Testing.h"
#include "aws/iotfleetwise/ISOTPOverCANOptions.h"
#include "aws/iotfleetwise/ISOTPOverCANReceiver.h"
#include "aws/iotfleetwise/ISOTPOverCANSender.h"
#include "aws/iotfleetwise/ISOTPOverCANSenderReceiver.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <cstdint>
#include <functional>
#include <gtest/gtest.h>
#include <linux/can.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

/**
 * @brief This is an integration test of the ISO-TP Kernel Module
 * and FWE Send and Receive APIs. If you want to run this test,
 * you must have the ISOTP Module compiled and installed on your
 * Linux Kernel by following the steps in this link
 * https://github.com/hartkopp/can-isotp  .
 * If you have a Linux Kernel version >= 5.10, the Kernel Module is already
 * pre-installed.
 */

namespace Aws
{
namespace IoTFleetWise
{

class ISOTPOverCANProtocolTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        if ( !socketAvailable() )
        {
            GTEST_FAIL() << "Test failed due to unavailability of socket";
        }
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

    static void
    sendPDU( ISOTPOverCANSender &sender, const std::vector<uint8_t> &pdu )
    {
        if ( !sender.sendPDU( pdu ) )
        {
            FWE_LOG_ERROR( "Error sending ISO-TP message" );
        }
    }

    static void
    senderReceiverSendPDU( ISOTPOverCANSenderReceiver &senderReceiver, const std::vector<uint8_t> &pdu )
    {
        if ( !senderReceiver.sendPDU( pdu ) )
        {
            FWE_LOG_ERROR( "Error sending ISO-TP message" );
        }
    }

    static void
    senderReceiverReceivePDU( ISOTPOverCANSenderReceiver &senderReceiver, std::vector<uint8_t> &pdu )
    {
        if ( !senderReceiver.receivePDU( pdu ) )
        {
            FWE_LOG_ERROR( "Error receiving ISO-TP message" );
        }
    }

    static void
    receivePDU( ISOTPOverCANReceiver &receiver, std::vector<uint8_t> &pdu )
    {
        if ( !receiver.receivePDU( pdu ) )
        {
            FWE_LOG_ERROR( "Error receiving ISO-TP message" );
        }
    }

    std::string mCanInterfaceName = getCanInterfaceName();
};

TEST_F( ISOTPOverCANProtocolTest, isotpSenderTestLifeCycle )
{
    ISOTPOverCANSenderOptions options;
    options.mSocketCanIFName = mCanInterfaceName;
    options.mSourceCANId = 0x123;
    options.mDestinationCANId = 0x456;
    ISOTPOverCANSender sender( options );
    ASSERT_TRUE( sender.connect() );
    ASSERT_TRUE( sender.disconnect() );
}

TEST_F( ISOTPOverCANProtocolTest, isotpSenderTestSendSingleFramePDU )
{
    ISOTPOverCANSenderOptions options;
    options.mSocketCanIFName = mCanInterfaceName;
    options.mSourceCANId = 0x123;
    options.mDestinationCANId = 0x456;
    ISOTPOverCANSender sender( options );
    ASSERT_TRUE( sender.connect() );
    // SF PDU of 5 Bytes.
    auto pduData = std::vector<uint8_t>( { 0x11, 0x22, 0x33, 0x44, 0x55 } );
    ASSERT_TRUE( sender.sendPDU( pduData ) );
    ASSERT_TRUE( sender.disconnect() );
}

TEST_F( ISOTPOverCANProtocolTest, isotpReceiverTestLifeCycle )
{
    ISOTPOverCANReceiverOptions options;
    options.mSocketCanIFName = mCanInterfaceName;
    options.mSourceCANId = 0x456;
    options.mDestinationCANId = 0x123;
    ISOTPOverCANReceiver receiver( options );
    ASSERT_TRUE( receiver.connect() );
    ASSERT_TRUE( receiver.disconnect() );
}

TEST_F( ISOTPOverCANProtocolTest, isotpSendAndReceiveSingleFrame )
{
    // Setup the sender
    ISOTPOverCANSenderOptions senderOptions;
    senderOptions.mSocketCanIFName = mCanInterfaceName;
    senderOptions.mSourceCANId = 0x123;
    senderOptions.mDestinationCANId = 0x456;
    ISOTPOverCANSender sender( senderOptions );
    ASSERT_TRUE( sender.connect() );
    // Setup the receiver
    ISOTPOverCANReceiverOptions receiverOptions;
    receiverOptions.mSocketCanIFName = mCanInterfaceName;
    receiverOptions.mSourceCANId = 0x456;
    receiverOptions.mDestinationCANId = 0x123;
    ISOTPOverCANReceiver receiver( receiverOptions );
    ASSERT_TRUE( receiver.connect() );
    // Setup a Single Frame send with 1 thread, and received from another thread
    std::vector<uint8_t> rxPDUData;
    auto txPDUData = std::vector<uint8_t>( { 0x11, 0x22, 0x33, 0x44, 0x55 } );

    std::thread senderThread( &sendPDU, std::ref( sender ), std::ref( txPDUData ) );
    senderThread.join();
    // Start the receiver thread and assert that the PDU is received
    std::thread receiverThread( &receivePDU, std::ref( receiver ), std::ref( rxPDUData ) );
    receiverThread.join();
    // Assert that the txPDU and the rxPDU are equal.
    ASSERT_EQ( rxPDUData, txPDUData );

    // Cleanup
    ASSERT_TRUE( receiver.disconnect() );
    ASSERT_TRUE( sender.disconnect() );
}

TEST_F( ISOTPOverCANProtocolTest, isotpSendAndReceiveMultiFrame )
{
    // Setup the sender
    ISOTPOverCANSenderOptions senderOptions;
    senderOptions.mSocketCanIFName = mCanInterfaceName;
    senderOptions.mSourceCANId = 0x123;
    senderOptions.mDestinationCANId = 0x456;
    ISOTPOverCANSender sender( senderOptions );
    ASSERT_TRUE( sender.connect() );
    // Setup the receiver
    ISOTPOverCANReceiverOptions receiverOptions;
    receiverOptions.mSocketCanIFName = mCanInterfaceName;
    receiverOptions.mSourceCANId = 0x456;
    receiverOptions.mDestinationCANId = 0x123;
    ISOTPOverCANReceiver receiver( receiverOptions );
    ASSERT_TRUE( receiver.connect() );
    // Setup a Multi Frame ( size > 8 bytes ) send with 1 thread, and received from another thread
    std::vector<uint8_t> rxPDUData;
    auto txPDUData = std::vector<uint8_t>( { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAB } );

    std::thread senderThread( &sendPDU, std::ref( sender ), std::ref( txPDUData ) );
    senderThread.join();
    // Start the receiver thread and assert that the PDU is received
    std::thread receiverThread( &receivePDU, std::ref( receiver ), std::ref( rxPDUData ) );
    receiverThread.join();
    // Assert that the txPDU and the rxPDU are equal.
    ASSERT_EQ( rxPDUData, txPDUData );

    // Cleanup
    ASSERT_TRUE( receiver.disconnect() );
    ASSERT_TRUE( sender.disconnect() );
}

TEST_F( ISOTPOverCANProtocolTest, isotpSendAndReceiveSameSocket )
{
    // Setup the sender receiver
    ISOTPOverCANSenderReceiverOptions senderReceiverOptions;
    senderReceiverOptions.mSocketCanIFName = mCanInterfaceName;
    senderReceiverOptions.mSourceCANId = 0x123;
    senderReceiverOptions.mDestinationCANId = 0x456;
    ISOTPOverCANSenderReceiver senderReceiver( senderReceiverOptions );
    ASSERT_TRUE( senderReceiver.connect() );
    // First Use case we would send a PDU and setup a receiver
    // to consume it in a separate channel.
    ISOTPOverCANReceiverOptions receiverOptions;
    receiverOptions.mSocketCanIFName = mCanInterfaceName;
    receiverOptions.mSourceCANId = 0x456;
    receiverOptions.mDestinationCANId = 0x123;
    ISOTPOverCANReceiver receiver( receiverOptions );
    ASSERT_TRUE( receiver.connect() );
    // Setup a Frame ) send with 1 thread, and received from another thread
    std::vector<uint8_t> rxPDUData;
    auto txPDUData = std::vector<uint8_t>( { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAB } );

    std::thread senderReceiverThread( &senderReceiverSendPDU, std::ref( senderReceiver ), std::ref( txPDUData ) );
    senderReceiverThread.join();
    // Start the receiver thread and assert that the PDU is received
    std::thread receiverThread( &receivePDU, std::ref( receiver ), std::ref( rxPDUData ) );
    receiverThread.join();
    // Assert that the txPDU and the rxPDU are equal.
    ASSERT_EQ( rxPDUData, txPDUData );

    // Now we send data in the other direction
    ISOTPOverCANSenderOptions senderOptions;
    senderOptions.mSocketCanIFName = mCanInterfaceName;
    senderOptions.mSourceCANId = 0x456;
    senderOptions.mDestinationCANId = 0x123;
    ISOTPOverCANSender sender( senderOptions );
    ASSERT_TRUE( sender.connect() );

    rxPDUData.clear();
    txPDUData.clear();
    txPDUData = std::vector<uint8_t>( { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAB } );

    std::thread senderThread( &sendPDU, std::ref( sender ), std::ref( txPDUData ) );
    senderThread.join();
    // Start the receiver thread and assert that the PDU is received
    std::thread secondSenderReceiverThread(
        &senderReceiverReceivePDU, std::ref( senderReceiver ), std::ref( rxPDUData ) );
    secondSenderReceiverThread.join();
    // Assert that the txPDU and the rxPDU are equal.
    ASSERT_EQ( rxPDUData, txPDUData );
    // Cleanup
    ASSERT_TRUE( senderReceiver.disconnect() );
    ASSERT_TRUE( receiver.disconnect() );
    ASSERT_TRUE( sender.disconnect() );
}

} // namespace IoTFleetWise
} // namespace Aws
