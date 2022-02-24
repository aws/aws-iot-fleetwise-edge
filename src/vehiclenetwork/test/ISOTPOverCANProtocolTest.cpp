
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

#include "businterfaces/ISOTPOverCANReceiver.h"
#include "businterfaces/ISOTPOverCANSender.h"
#include "businterfaces/ISOTPOverCANSenderReceiver.h"
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>

/**
 * @brief This is an integration test of the ISO-TP Kernel Module
 * and FWE Send and Receive APIs. If you want to run this test,
 * you must have the ISOTP Module compiled and installed on your
 * Linux Kernel by following the steps in this link
 * https://github.com/hartkopp/can-isotp  .
 * If you have a Linux Kernel version >= 5.10, the Kernel Module is already
 * pre-installed.
 */

using namespace Aws::IoTFleetWise::VehicleNetwork;

void
sendPDU( ISOTPOverCANSender &sender, const std::vector<uint8_t> &pdu )
{
    ASSERT_TRUE( sender.sendPDU( pdu ) );
}

void
senderReceiverSendPDU( ISOTPOverCANSenderReceiver &senderReceiver, const std::vector<uint8_t> &pdu )
{
    ASSERT_TRUE( senderReceiver.sendPDU( pdu ) );
}

void
senderReceiverReceivePDU( ISOTPOverCANSenderReceiver &senderReceiver, std::vector<uint8_t> &pdu )
{
    ASSERT_TRUE( senderReceiver.receivePDU( pdu ) );
}

void
receivePDU( ISOTPOverCANReceiver &receiver, std::vector<uint8_t> &pdu )
{
    ASSERT_TRUE( receiver.receivePDU( pdu ) );
}

TEST( ISOTPOverCANProtocolTest, isotpSenderTestLifeCycle_LinuxCANDep )
{
    ISOTPOverCANSender sender;
    ISOTPOverCANSenderOptions options;
    options.mSocketCanIFName = "vcan0";
    options.mSourceCANId = 0x123;
    options.mDestinationCANId = 0x456;
    ASSERT_TRUE( sender.init( options ) );
    ASSERT_TRUE( sender.connect() );
    ASSERT_TRUE( sender.disconnect() );
}

TEST( ISOTPOverCANProtocolTest, isotpSenderTestSendSingleFramePDU_LinuxCANDep )
{
    ISOTPOverCANSender sender;
    ISOTPOverCANSenderOptions options;
    options.mSocketCanIFName = "vcan0";
    options.mSourceCANId = 0x123;
    options.mDestinationCANId = 0x456;
    ASSERT_TRUE( sender.init( options ) );
    ASSERT_TRUE( sender.connect() );
    // SF PDU of 5 Bytes.
    auto pduData = std::vector<uint8_t>( { 0x11, 0x22, 0x33, 0x44, 0x55 } );
    ASSERT_TRUE( sender.sendPDU( pduData ) );
    ASSERT_TRUE( sender.disconnect() );
}

TEST( ISOTPOverCANProtocolTest, isotpReceiverTestLifeCycle_LinuxCANDep )
{
    ISOTPOverCANReceiver receiver;
    ISOTPOverCANReceiverOptions options;
    options.mSocketCanIFName = "vcan0";
    options.mSourceCANId = 0x456;
    options.mDestinationCANId = 0x123;
    ASSERT_TRUE( receiver.init( options ) );
    ASSERT_TRUE( receiver.connect() );
    ASSERT_TRUE( receiver.disconnect() );
}

TEST( ISOTPOverCANProtocolTest, isotpSendAndReceiveSingleFrame_LinuxCANDep )
{
    // Setup the sender
    ISOTPOverCANSender sender;
    ISOTPOverCANSenderOptions senderOptions;
    senderOptions.mSocketCanIFName = "vcan0";
    senderOptions.mSourceCANId = 0x123;
    senderOptions.mDestinationCANId = 0x456;
    ASSERT_TRUE( sender.init( senderOptions ) );
    ASSERT_TRUE( sender.connect() );
    // Setup the receiver
    ISOTPOverCANReceiver receiver;
    ISOTPOverCANReceiverOptions receiverOptions;
    receiverOptions.mSocketCanIFName = "vcan0";
    receiverOptions.mSourceCANId = 0x456;
    receiverOptions.mDestinationCANId = 0x123;
    ASSERT_TRUE( receiver.init( receiverOptions ) );
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

TEST( ISOTPOverCANProtocolTest, isotpSendAndReceiveMultiFrame_LinuxCANDep )
{
    // Setup the sender
    ISOTPOverCANSender sender;
    ISOTPOverCANSenderOptions senderOptions;
    senderOptions.mSocketCanIFName = "vcan0";
    senderOptions.mSourceCANId = 0x123;
    senderOptions.mDestinationCANId = 0x456;
    ASSERT_TRUE( sender.init( senderOptions ) );
    ASSERT_TRUE( sender.connect() );
    // Setup the receiver
    ISOTPOverCANReceiver receiver;
    ISOTPOverCANReceiverOptions receiverOptions;
    receiverOptions.mSocketCanIFName = "vcan0";
    receiverOptions.mSourceCANId = 0x456;
    receiverOptions.mDestinationCANId = 0x123;
    ASSERT_TRUE( receiver.init( receiverOptions ) );
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

TEST( ISOTPOverCANProtocolTest, isotpSendAndReceiveSameSocket_LinuxCANDep )
{
    // Setup the sender receiver
    ISOTPOverCANSenderReceiver senderReceiver;
    ISOTPOverCANSenderReceiverOptions senderReceiverOptions;
    senderReceiverOptions.mSocketCanIFName = "vcan0";
    senderReceiverOptions.mSourceCANId = 0x123;
    senderReceiverOptions.mDestinationCANId = 0x456;
    ASSERT_TRUE( senderReceiver.init( senderReceiverOptions ) );
    ASSERT_TRUE( senderReceiver.connect() );
    // First Use case we would send a PDU and setup a receiver
    // to consume it in a separate channel.
    ISOTPOverCANReceiver receiver;
    ISOTPOverCANReceiverOptions receiverOptions;
    receiverOptions.mSocketCanIFName = "vcan0";
    receiverOptions.mSourceCANId = 0x456;
    receiverOptions.mDestinationCANId = 0x123;
    ASSERT_TRUE( receiver.init( receiverOptions ) );
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
    ISOTPOverCANSender sender;
    ISOTPOverCANSenderOptions senderOptions;
    senderOptions.mSocketCanIFName = "vcan0";
    senderOptions.mSourceCANId = 0x456;
    senderOptions.mDestinationCANId = 0x123;
    ASSERT_TRUE( sender.init( senderOptions ) );
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