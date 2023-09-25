// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

// According to J1979 6.2.2.7, we should wait at least P2CAN Max ( 50 ms )
// We relax that a bit e.g. up to 1000 ms for network latency.
const uint32_t P2_TIMEOUT_DEFAULT_MS = 1000; // P2*CAN Max is 1 second
const uint32_t P2_TIMEOUT_INFINITE = 0;
/**
 * @brief Set of Options a Sender of a PDU must provide to the ISO-TP Stack
 * to establish the Socket between the sender ECU and the receiver ECU.
 * @param socketCanIFName Bus name as it's configured on OS IPRoute e.g. Bus1
 * @param sourceCANId This is the ID that the ECU listens to on the CAN network.
 * @param destinationCANId This is the ID that the ECU uses to respond on the CAN Network.
 * @param isExtendedId Defines whether the CAN IDs are extended IDs or not ( 29/11 bits)
 * @param p2TimeoutMs Receiver timeout when waiting for a message from the sender.
 *                    Zero means infinite, default is P2_TIMEOUT_DEFAULT_MSI
 */
struct ISOTPOverCANSenderOptions
{
    ISOTPOverCANSenderOptions( std::string socketCanIFName,
                               const uint32_t sourceCANId,
                               const uint32_t destinationCANId,
                               const bool isExtendedId = false,
                               const uint32_t p2TimeoutMs = P2_TIMEOUT_DEFAULT_MS )
        : mSocketCanIFName( std::move( socketCanIFName ) )
        , mSourceCANId( sourceCANId )
        , mDestinationCANId( destinationCANId )
        , mIsExtendedId( isExtendedId )
        , mP2TimeoutMs( p2TimeoutMs )
    {
    }

    ISOTPOverCANSenderOptions() = default;

    std::string mSocketCanIFName;
    uint32_t mSourceCANId{ 0x0 };
    uint32_t mDestinationCANId{ 0x0 };
    bool mIsExtendedId{ false };
    uint32_t mP2TimeoutMs{ P2_TIMEOUT_DEFAULT_MS };
};

/**
 * @brief Set of Options a Receiver of a PDU must provide to the ISO-TP Stack
 * to establish the Socket between the Receiver ECU and the Sender ECU.
 * @param socketCanIFName Bus name as it's configured on OS IPRoute e.g. Bus1
 * @param sourceCANId This is the ID that the ECU listens to on the CAN network.
 * @param destinationCANId This is the ID that the ECU uses to respond on the CAN Network.
 * @param isExtendedId Defines whether the CAN IDs are extended IDs or not ( 29/11 bits)
 * @param blockSize defines the number of consecutive Frames the receiver expects
 *                  before a control frame in case of a Multi-Frame PDU.
 *                  Default is zero ( no limit )
 * @param frameSeparationTimeMs Minimum Separation time between 2 Consecutive Frames.
 *                              Default is zero  i.e. ( as soon as possible )
 * @param p2TimeoutMs Receiver timeout when waiting for a message from the sender.
 *                    Zero means infinite, default is P2_TIMEOUT_DEFAULT_MS
 */
struct ISOTPOverCANReceiverOptions
{
    ISOTPOverCANReceiverOptions( std::string socketCanIFName,
                                 uint32_t sourceCANId,
                                 uint32_t destinationCANId,
                                 bool isExtendedId = false,
                                 uint8_t blockSize = 0,
                                 uint32_t frameSeparationTimeMs = 0,
                                 uint32_t p2TimeoutMs = P2_TIMEOUT_DEFAULT_MS )
        : mSocketCanIFName( std::move( socketCanIFName ) )
        , mSourceCANId( sourceCANId )
        , mDestinationCANId( destinationCANId )
        , mIsExtendedId( isExtendedId )
        , mBlockSize( blockSize )
        , mFrameSeparationTimeMs( frameSeparationTimeMs )
        , mP2TimeoutMs( p2TimeoutMs )
    {
    }

    ISOTPOverCANReceiverOptions() = default;

    std::string mSocketCanIFName;
    uint32_t mSourceCANId{ 0x0 };
    uint32_t mDestinationCANId{ 0x0 };
    bool mIsExtendedId{ false };
    uint8_t mBlockSize{ 0x0 };
    uint32_t mFrameSeparationTimeMs{ 0x0 };
    uint32_t mP2TimeoutMs{ P2_TIMEOUT_DEFAULT_MS };
};

/**
 * @brief Set of Options for establishing the channel between 2 ECUs.
 * @param socketCanIFName Bus name as it's configured on OS IPRoute e.g. Bus1
 * @param sourceCANId This is the ID that the ECU listens to on the CAN network.
 * @param destinationCANId This is the ID that the ECU uses to respond on the CAN Network.
 * @param isExtendedId Defines whether the CAN IDs are extended IDs or not ( 29/11 bits)
 * @param blockSize defines the number of consecutive Frames the receiver expects
 *                  before a control frame in case of a Multi-Frame PDU.
 *                  Default is zero ( no limit )
 * @param frameSeparationTimeMs Minimum Separation time between 2 Consecutive Frames.
 *                              Default is zero  i.e. ( as soon as possible )
 * @param p2TimeoutMs Receiver timeout when waiting for a message from the sender.
 *                    Zero means infinite, default is P2_TIMEOUT_DEFAULT_MS
 */
struct ISOTPOverCANSenderReceiverOptions
{
    ISOTPOverCANSenderReceiverOptions() = default;

    ISOTPOverCANSenderReceiverOptions( std::string socketCanIFName,
                                       uint32_t sourceCANId,
                                       uint32_t destinationCANId,
                                       bool isExtendedId = false,
                                       uint8_t blockSize = 0,
                                       uint32_t frameSeparationTimeMs = 0,
                                       uint32_t p2TimeoutMs = P2_TIMEOUT_DEFAULT_MS,
                                       int broadcastSocket = -1 )
        : mSocketCanIFName( std::move( socketCanIFName ) )
        , mSourceCANId( sourceCANId )
        , mDestinationCANId( destinationCANId )
        , mIsExtendedId( isExtendedId )
        , mBlockSize( blockSize )
        , mFrameSeparationTimeMs( frameSeparationTimeMs )
        , mP2TimeoutMs( p2TimeoutMs )
        , mBroadcastSocket( broadcastSocket )
    {
    }

    std::string mSocketCanIFName;
    uint32_t mSourceCANId{ 0x0 };
    uint32_t mDestinationCANId{ 0x0 };
    bool mIsExtendedId{ false };
    uint8_t mBlockSize{ 0x0 };
    uint32_t mFrameSeparationTimeMs{ 0x0 };
    uint32_t mP2TimeoutMs{ P2_TIMEOUT_DEFAULT_MS };
    int mBroadcastSocket{ -1 };
};

} // namespace IoTFleetWise
} // namespace Aws
