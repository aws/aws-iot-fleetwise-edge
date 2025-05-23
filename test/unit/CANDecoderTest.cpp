// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CANDecoder.h"
#include "aws/iotfleetwise/CANDataTypes.h"
#include "aws/iotfleetwise/MessageTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <unordered_set>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

TEST( CANDecoderTest, SimpleMessage )
{
    // Test for Basic Decoding UseCase
    std::vector<uint8_t> frameData{ 0x08, 0x46, 0xFF, 0x4B, 0x00, 0xD0, 0x00 };

    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 1;
    sigFormat1.mIsBigEndian = true;
    sigFormat1.mIsSigned = false;
    sigFormat1.mFirstBitPosition = 44;
    sigFormat1.mSizeInBits = 4;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;

    CANSignalFormat sigFormat2;
    sigFormat2.mSignalID = 7;
    sigFormat2.mIsBigEndian = true;
    sigFormat2.mIsSigned = false;
    sigFormat2.mFirstBitPosition = 28;
    sigFormat2.mSizeInBits = 12;
    sigFormat2.mOffset = 0.0;
    sigFormat2.mFactor = 0.1;

    CANMessageFormat msgFormat;
    msgFormat.mMessageID = 0x101;
    msgFormat.mSizeInBytes = 7;
    msgFormat.mSignals.emplace_back( sigFormat1 );
    msgFormat.mSignals.emplace_back( sigFormat2 );

    CANDecoder decoder;
    std::vector<CANDecodedSignal> decodedSignals;
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 7 };
    ASSERT_TRUE( decoder.decodeCANMessage( frameData.data(), 8, msgFormat, signalIDsToCollect, decodedSignals ) );
    ASSERT_EQ( decodedSignals.size(), 2 );
    ASSERT_EQ( decodedSignals[0].mPhysicalValue.signalValue.doubleVal, 13 );
    ASSERT_DOUBLE_EQ( decodedSignals[1].mPhysicalValue.signalValue.doubleVal, 408.4 );
}

TEST( CANDecoderTest, SimpleMessage2 )
{
    // Test for Signed Numbers
    std::vector<uint8_t> frameData{ 0x09, 0x28, 0x54, 0xF9, 0x6E, 0x23, 0x6E, 0xA6 };

    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 1;
    sigFormat1.mIsBigEndian = true;
    sigFormat1.mIsSigned = true;
    sigFormat1.mFirstBitPosition = 24;
    sigFormat1.mSizeInBits = 30;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;

    CANSignalFormat sigFormat2;
    sigFormat2.mSignalID = 7;
    sigFormat2.mIsBigEndian = true;
    sigFormat2.mIsSigned = true;
    sigFormat2.mFirstBitPosition = 56;
    sigFormat2.mSizeInBits = 31;
    sigFormat2.mOffset = 0.0;
    sigFormat2.mFactor = 1.0;

    CANMessageFormat msgFormat;
    msgFormat.mMessageID = 0x32A;
    msgFormat.mSizeInBytes = 8;
    msgFormat.mSignals.emplace_back( sigFormat1 );
    msgFormat.mSignals.emplace_back( sigFormat2 );

    CANDecoder decoder;
    std::vector<CANDecodedSignal> decodedSignals;
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 7 };
    ASSERT_TRUE( decoder.decodeCANMessage( frameData.data(), 8, msgFormat, signalIDsToCollect, decodedSignals ) );
    ASSERT_EQ( decodedSignals.size(), 2 );
    ASSERT_EQ( decodedSignals[0].mPhysicalValue.signalValue.doubleVal, 153638137 );
    ASSERT_EQ( decodedSignals[1].mPhysicalValue.signalValue.doubleVal, -299667802 );
}

// Precision Test
TEST( CANDecoderTest, CANDecoderPrecisionTest )
{
    constexpr auto maxUnSignedVal = std::numeric_limits<uint64_t>::max();
    constexpr auto maxSignedVal = std::numeric_limits<int64_t>::max();

    // Test for max val
    constexpr uint32_t msgSizeBytes = 16;
    std::vector<uint8_t> frameData;
    for ( size_t i = 0; i < 15; i++ )
    {
        frameData.emplace_back( 0xFF );
    }
    frameData.emplace_back( 0x7F );

    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 1;
    sigFormat1.mIsBigEndian = false;
    sigFormat1.mIsSigned = false;
    sigFormat1.mFirstBitPosition = 0;
    sigFormat1.mSizeInBits = 64;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;
    sigFormat1.mSignalType = SignalType::UINT64;

    CANSignalFormat sigFormat2;
    sigFormat2.mSignalID = 7;
    sigFormat2.mIsBigEndian = false;
    sigFormat2.mIsSigned = true;
    sigFormat2.mFirstBitPosition = 64;
    sigFormat2.mSizeInBits = 64;
    sigFormat2.mOffset = 0.0;
    sigFormat2.mFactor = 1.0;
    sigFormat2.mSignalType = SignalType::INT64;

    CANMessageFormat msgFormat;
    msgFormat.mMessageID = 0x32A;
    msgFormat.mSizeInBytes = msgSizeBytes;
    msgFormat.mSignals.emplace_back( sigFormat1 );
    msgFormat.mSignals.emplace_back( sigFormat2 );

    CANDecoder decoder;
    std::vector<CANDecodedSignal> decodedSignals;
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 7 };
    ASSERT_TRUE(
        decoder.decodeCANMessage( frameData.data(), msgSizeBytes, msgFormat, signalIDsToCollect, decodedSignals ) );

    ASSERT_EQ( decodedSignals.size(), 2 );
    ASSERT_EQ( decodedSignals[0].mSignalType, SignalType::UINT64 );
    ASSERT_EQ( decodedSignals[1].mSignalType, SignalType::INT64 );
    ASSERT_EQ( decodedSignals[0].mPhysicalValue.signalValue.uint64Val, maxUnSignedVal );
    ASSERT_EQ( decodedSignals[1].mPhysicalValue.signalValue.int64Val, maxSignedVal );
}

TEST( CANDecoderTest, CANDecoderPrecisionSignedTest )
{
    constexpr auto minSignedVal = std::numeric_limits<int64_t>::min();

    // Test for max val
    constexpr uint32_t msgSizeBytes = 8;
    std::vector<uint8_t> frameData;
    frameData.emplace_back( 0x00 );
    for ( size_t i = 0; i < 6; i++ )
    {
        frameData.emplace_back( 0x00 );
    }
    frameData.emplace_back( 0x80 );

    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 1;
    sigFormat1.mIsBigEndian = false;
    sigFormat1.mIsSigned = true;
    sigFormat1.mFirstBitPosition = 0;
    sigFormat1.mSizeInBits = 64;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;
    sigFormat1.mSignalType = SignalType::INT64;

    CANMessageFormat msgFormat;
    msgFormat.mMessageID = 0x32A;
    msgFormat.mSizeInBytes = msgSizeBytes;
    msgFormat.mSignals.emplace_back( sigFormat1 );

    CANDecoder decoder;
    std::vector<CANDecodedSignal> decodedSignals;
    std::unordered_set<SignalID> signalIDsToCollect = { 1 };
    ASSERT_TRUE(
        decoder.decodeCANMessage( frameData.data(), msgSizeBytes, msgFormat, signalIDsToCollect, decodedSignals ) );

    ASSERT_EQ( decodedSignals.size(), 1 );
    ASSERT_EQ( decodedSignals[0].mSignalType, SignalType::INT64 );
    ASSERT_EQ( decodedSignals[0].mPhysicalValue.signalValue.int64Val, minSignedVal );
}

TEST( CANDecoderTest, SimpleMessage3 )
{
    // Test for BigEndian & LittleEndian Signals
    std::vector<uint8_t> frameData{ 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB };

    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 1;
    sigFormat1.mIsBigEndian = false;
    sigFormat1.mIsSigned = false;
    sigFormat1.mFirstBitPosition = 0;
    sigFormat1.mSizeInBits = 16;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;

    CANSignalFormat sigFormat2;
    sigFormat2.mSignalID = 2;
    sigFormat2.mIsBigEndian = true;
    sigFormat2.mIsSigned = false;
    sigFormat2.mFirstBitPosition = 24;
    sigFormat2.mSizeInBits = 16;
    sigFormat2.mOffset = 0.0;
    sigFormat2.mFactor = 1.0;

    CANSignalFormat sigFormat3;
    sigFormat3.mSignalID = 3;
    sigFormat3.mIsBigEndian = true;
    sigFormat3.mIsSigned = false;
    sigFormat3.mFirstBitPosition = 40;
    sigFormat3.mSizeInBits = 16;
    sigFormat3.mOffset = 0.0;
    sigFormat3.mFactor = 1.0;

    CANMessageFormat msgFormat;
    msgFormat.mMessageID = 0;
    msgFormat.mSizeInBytes = 8;
    msgFormat.mSignals.emplace_back( sigFormat1 );
    msgFormat.mSignals.emplace_back( sigFormat2 );
    msgFormat.mSignals.emplace_back( sigFormat3 );

    CANDecoder decoder;
    std::vector<CANDecodedSignal> decodedSignals;
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 2, 3 };
    ASSERT_TRUE( decoder.decodeCANMessage( frameData.data(), 8, msgFormat, signalIDsToCollect, decodedSignals ) );
    ASSERT_EQ( decodedSignals.size(), 3 );
    EXPECT_EQ( decodedSignals[0].mPhysicalValue.signalValue.doubleVal, 0x2301 );
    EXPECT_EQ( decodedSignals[1].mPhysicalValue.signalValue.doubleVal, 0x4567 );
    EXPECT_EQ( decodedSignals[2].mPhysicalValue.signalValue.doubleVal, 0x89AB );
}
TEST( CANDecoderTest, SimpleCanFdMessage )
{
    // Test for BigEndian & LittleEndian Signals
    std::vector<uint8_t> frameData;
    for ( int i = 0; i < 16; i++ )
    {
        frameData.emplace_back( 0x01 );
        frameData.emplace_back( 0x23 );
        frameData.emplace_back( 0x45 );
        frameData.emplace_back( 0x67 );
    }

    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 1;
    sigFormat1.mIsBigEndian = false;
    sigFormat1.mIsSigned = false;
    sigFormat1.mFirstBitPosition = 0;
    sigFormat1.mSizeInBits = 16;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;

    CANSignalFormat sigFormat2;
    sigFormat2.mSignalID = 2;
    sigFormat2.mIsBigEndian = true;
    sigFormat2.mIsSigned = false;
    sigFormat2.mFirstBitPosition = 124;
    sigFormat2.mSizeInBits = 32;
    sigFormat2.mOffset = 0.0;
    sigFormat2.mFactor = 1.0;

    CANSignalFormat sigFormat3;
    sigFormat3.mSignalID = 3;
    sigFormat3.mIsBigEndian = true;
    sigFormat3.mIsSigned = false;
    sigFormat3.mFirstBitPosition = 256;
    sigFormat3.mSizeInBits = 24;
    sigFormat3.mOffset = 0.0;
    sigFormat3.mFactor = 1.0;

    CANSignalFormat sigFormat4;
    sigFormat4.mSignalID = 4;
    sigFormat4.mIsBigEndian = true;
    sigFormat4.mIsSigned = false;
    sigFormat4.mFirstBitPosition = 300;
    sigFormat4.mSizeInBits = 16;
    sigFormat4.mOffset = 0.0;
    sigFormat4.mFactor = 1.0;

    CANMessageFormat msgFormat;
    msgFormat.mMessageID = 0;
    msgFormat.mSizeInBytes = 64;
    msgFormat.mSignals.emplace_back( sigFormat1 );
    msgFormat.mSignals.emplace_back( sigFormat2 );
    msgFormat.mSignals.emplace_back( sigFormat3 );
    msgFormat.mSignals.emplace_back( sigFormat4 );

    CANDecoder decoder;
    std::vector<CANDecodedSignal> decodedSignals;
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 2, 3, 4 };
    ASSERT_TRUE( decoder.decodeCANMessage( frameData.data(), 64, msgFormat, signalIDsToCollect, decodedSignals ) );
    ASSERT_EQ( decodedSignals.size(), 4 );
    EXPECT_EQ( decodedSignals[0].mPhysicalValue.signalValue.doubleVal, 0x2301 );
    EXPECT_EQ( decodedSignals[1].mPhysicalValue.signalValue.doubleVal, 0x70123456 );
    EXPECT_EQ( decodedSignals[2].mPhysicalValue.signalValue.doubleVal, 0x456701 );
    EXPECT_EQ( decodedSignals[3].mPhysicalValue.signalValue.doubleVal, 0x7012 );
}

TEST( CANDecoderTest, OnlyDecodeSomeSignals )
{
    // Test for BigEndian & LittleEndian Signals
    std::vector<uint8_t> frameData{ 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB };

    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 1;
    sigFormat1.mIsBigEndian = false;
    sigFormat1.mIsSigned = false;
    sigFormat1.mFirstBitPosition = 0;
    sigFormat1.mSizeInBits = 16;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;

    CANSignalFormat sigFormat2;
    sigFormat2.mSignalID = 2;
    sigFormat2.mIsBigEndian = true;
    sigFormat2.mIsSigned = false;
    sigFormat2.mFirstBitPosition = 24;
    sigFormat2.mSizeInBits = 16;
    sigFormat2.mOffset = 0.0;
    sigFormat2.mFactor = 1.0;

    CANSignalFormat sigFormat3;
    sigFormat3.mSignalID = 3;
    sigFormat3.mIsBigEndian = true;
    sigFormat3.mIsSigned = false;
    sigFormat3.mFirstBitPosition = 40;
    sigFormat3.mSizeInBits = 16;
    sigFormat3.mOffset = 0.0;
    sigFormat3.mFactor = 1.0;

    CANMessageFormat msgFormat;
    msgFormat.mMessageID = 0;
    msgFormat.mSizeInBytes = 8;
    msgFormat.mSignals.emplace_back( sigFormat1 );
    msgFormat.mSignals.emplace_back( sigFormat2 );
    msgFormat.mSignals.emplace_back( sigFormat3 );

    CANDecoder decoder;
    std::vector<CANDecodedSignal> decodedSignals;
    // Only collect Signal ID 1 and 3. Do not collect and decode Signal ID 2
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 3 };
    ASSERT_TRUE( decoder.decodeCANMessage( frameData.data(), 8, msgFormat, signalIDsToCollect, decodedSignals ) );
    ASSERT_EQ( decodedSignals.size(), 2 );
    EXPECT_EQ( decodedSignals[0].mPhysicalValue.signalValue.doubleVal, 0x2301 );
    EXPECT_EQ( decodedSignals[1].mPhysicalValue.signalValue.doubleVal, 0x89AB );
}

TEST( CANDecoderTest, MultiplexedMessage1 )
{
    // Multiplexed Frame
    std::vector<uint8_t> frameData{ 0x05, 0x02, 0x0B, 0x00, 0xD3, 0x00, 0x4B, 0x18 };

    // Definition of the MUX Signal
    CANSignalFormat multiplexorSignal;
    multiplexorSignal.mSignalID = 50;
    multiplexorSignal.mIsBigEndian = false;
    multiplexorSignal.mIsSigned = false;
    multiplexorSignal.mFirstBitPosition = 0;
    multiplexorSignal.mSizeInBits = 4;
    multiplexorSignal.mOffset = 0.0;
    multiplexorSignal.mFactor = 1.0;
    multiplexorSignal.mIsMultiplexorSignal = true;

    // MUX value of 5
    CANSignalFormat sigMuXValueOfFive1;
    sigMuXValueOfFive1.mSignalID = 51;
    sigMuXValueOfFive1.mIsBigEndian = true;
    sigMuXValueOfFive1.mIsSigned = false;
    sigMuXValueOfFive1.mFirstBitPosition = 48;
    sigMuXValueOfFive1.mSizeInBits = 16;
    sigMuXValueOfFive1.mOffset = 0.0;
    sigMuXValueOfFive1.mFactor = 1.0;
    sigMuXValueOfFive1.mMultiplexorValue = 5;

    // MUX value of 5
    CANSignalFormat sigMuXValueOfFive2;
    sigMuXValueOfFive2.mSignalID = 52;
    sigMuXValueOfFive2.mIsBigEndian = true;
    sigMuXValueOfFive2.mIsSigned = false;
    sigMuXValueOfFive2.mFirstBitPosition = 16;
    sigMuXValueOfFive2.mSizeInBits = 16;
    sigMuXValueOfFive2.mOffset = 0.0;
    sigMuXValueOfFive2.mFactor = 1.0;
    sigMuXValueOfFive2.mMultiplexorValue = 5;

    // MUX value of 5
    CANSignalFormat sigMuXValueOfFive3;
    sigMuXValueOfFive3.mSignalID = 53;
    sigMuXValueOfFive3.mIsBigEndian = true;
    sigMuXValueOfFive3.mIsSigned = false;
    sigMuXValueOfFive3.mFirstBitPosition = 32;
    sigMuXValueOfFive3.mSizeInBits = 16;
    sigMuXValueOfFive3.mOffset = 0.0;
    sigMuXValueOfFive3.mFactor = 1.0;
    sigMuXValueOfFive3.mMultiplexorValue = 5;

    // Test for Mux Group 5
    // Added 1 Multiplexor Signal and 3 signals belonging to the same Mux Group
    CANMessageFormat msgFormat;
    msgFormat.mSizeInBytes = 8;
    msgFormat.mSignals.emplace_back( multiplexorSignal );
    msgFormat.mSignals.emplace_back( sigMuXValueOfFive1 );
    msgFormat.mSignals.emplace_back( sigMuXValueOfFive2 );
    msgFormat.mSignals.emplace_back( sigMuXValueOfFive3 );
    msgFormat.mIsMultiplexed = true;

    CANDecoder decoder;
    std::vector<CANDecodedSignal> decodedSignals;
    std::unordered_set<SignalID> signalIDsToCollect = { 50, 51, 52, 53 };
    ASSERT_TRUE( decoder.decodeCANMessage( frameData.data(), 8, msgFormat, signalIDsToCollect, decodedSignals ) );
    ASSERT_EQ( decodedSignals.size(), 4 );
    EXPECT_EQ( decodedSignals[0].mPhysicalValue.signalValue.doubleVal, 0x05 );
    EXPECT_EQ( decodedSignals[1].mPhysicalValue.signalValue.doubleVal, 0x4B );
    EXPECT_EQ( decodedSignals[2].mPhysicalValue.signalValue.doubleVal, 0x20B );
    EXPECT_EQ( decodedSignals[3].mPhysicalValue.signalValue.doubleVal, 0xD3 );
}

TEST( CANDecoderTest, MultiplexedMessage2 )
{
    // Multiplexed Frame
    std::vector<uint8_t> frameData{ 0x06, 0x03, 0xEB, 0x0E, 0x3B, 0x00, 0x1B, 0x18 };

    // Definition of the MUX Signal
    CANSignalFormat multiplexorSignal;
    multiplexorSignal.mSignalID = 54;
    multiplexorSignal.mIsBigEndian = false;
    multiplexorSignal.mIsSigned = false;
    multiplexorSignal.mFirstBitPosition = 0;
    multiplexorSignal.mSizeInBits = 4;
    multiplexorSignal.mOffset = 0.0;
    multiplexorSignal.mFactor = 1.0;
    multiplexorSignal.mIsMultiplexorSignal = true;

    // MUX value of 6
    CANSignalFormat sigMuXValueOfSix1;
    sigMuXValueOfSix1.mSignalID = 55;
    sigMuXValueOfSix1.mIsBigEndian = true;
    sigMuXValueOfSix1.mIsSigned = false;
    sigMuXValueOfSix1.mFirstBitPosition = 48;
    sigMuXValueOfSix1.mSizeInBits = 16;
    sigMuXValueOfSix1.mOffset = 0.0;
    sigMuXValueOfSix1.mFactor = 1.0;
    sigMuXValueOfSix1.mMultiplexorValue = 6;

    // MUX value of 6
    CANSignalFormat sigMuXValueOfSix2;
    sigMuXValueOfSix2.mSignalID = 56;
    sigMuXValueOfSix2.mIsBigEndian = true;
    sigMuXValueOfSix2.mIsSigned = false;
    sigMuXValueOfSix2.mFirstBitPosition = 16;
    sigMuXValueOfSix2.mSizeInBits = 16;
    sigMuXValueOfSix2.mOffset = 0.0;
    sigMuXValueOfSix2.mFactor = 1.0;
    sigMuXValueOfSix2.mMultiplexorValue = 6;

    // MUX value of 6
    CANSignalFormat sigMuXValueOfSix3;
    sigMuXValueOfSix3.mSignalID = 57;
    sigMuXValueOfSix3.mIsBigEndian = true;
    sigMuXValueOfSix3.mIsSigned = false;
    sigMuXValueOfSix3.mFirstBitPosition = 32;
    sigMuXValueOfSix3.mSizeInBits = 16;
    sigMuXValueOfSix3.mOffset = 0.0;
    sigMuXValueOfSix3.mFactor = 1.0;
    sigMuXValueOfSix3.mMultiplexorValue = 6;

    // Add a signal with MUX value of 7
    // Assert that this signal is not added to the CANDecodedMessage
    CANSignalFormat sigMuXValueOfSeven1;
    sigMuXValueOfSeven1.mSignalID = 58;
    sigMuXValueOfSeven1.mIsBigEndian = true;
    sigMuXValueOfSeven1.mIsSigned = true;
    sigMuXValueOfSeven1.mFirstBitPosition = 11;
    sigMuXValueOfSeven1.mSizeInBits = 4;
    sigMuXValueOfSeven1.mOffset = 0.0;
    sigMuXValueOfSeven1.mFactor = 1.0;
    sigMuXValueOfSeven1.mMultiplexorValue = 7;

    // Test for Mux Group 6
    // Added 1 Multiplexor Signal and 3 signals belonging to the same Mux Group 6
    // and added 1 signal belonging to Mux Group 7 which should not be decoded
    CANMessageFormat msgFormat;
    msgFormat.mSizeInBytes = 8;
    msgFormat.mSignals.emplace_back( multiplexorSignal );
    msgFormat.mSignals.emplace_back( sigMuXValueOfSix1 );
    msgFormat.mSignals.emplace_back( sigMuXValueOfSix2 );
    msgFormat.mSignals.emplace_back( sigMuXValueOfSix3 );
    msgFormat.mSignals.emplace_back( sigMuXValueOfSeven1 );
    msgFormat.mIsMultiplexed = true;

    CANDecoder decoder;
    std::vector<CANDecodedSignal> decodedSignals;
    std::unordered_set<SignalID> signalIDsToCollect = { 54, 55, 56, 57, 58 };
    ASSERT_TRUE( decoder.decodeCANMessage( frameData.data(), 8, msgFormat, signalIDsToCollect, decodedSignals ) );
    ASSERT_EQ( decodedSignals.size(), 4 );
    EXPECT_EQ( decodedSignals[0].mPhysicalValue.signalValue.doubleVal, 0x06 );
    EXPECT_EQ( decodedSignals[1].mPhysicalValue.signalValue.doubleVal, 0x1B );
    EXPECT_EQ( decodedSignals[2].mPhysicalValue.signalValue.doubleVal, 0x3EB );
    EXPECT_EQ( decodedSignals[3].mPhysicalValue.signalValue.doubleVal, 0xE3B );
}

TEST( CANDecoderTest, InvalidSignalLayout )
{
    // Test for a shorter CAN frame than Signal layout.
    // Message has only 1 byte while the signal 2 to collect locates at bit 8.
    uint8_t frameSize = 1;
    std::vector<uint8_t> frameData{ 0x01 };

    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 1;
    sigFormat1.mIsBigEndian = false;
    sigFormat1.mIsSigned = false;
    sigFormat1.mFirstBitPosition = 0;
    sigFormat1.mSizeInBits = 8;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;

    CANSignalFormat sigFormat2;
    sigFormat2.mSignalID = 2;
    sigFormat2.mIsBigEndian = false;
    sigFormat2.mIsSigned = false;
    sigFormat2.mFirstBitPosition = 8;
    sigFormat2.mSizeInBits = 8;
    sigFormat2.mOffset = 0.0;
    sigFormat2.mFactor = 1.0;

    CANMessageFormat msgFormat;
    msgFormat.mMessageID = 0;
    msgFormat.mSizeInBytes = 1;
    msgFormat.mSignals.emplace_back( sigFormat1 );
    msgFormat.mSignals.emplace_back( sigFormat2 );

    CANDecoder decoder;
    std::vector<CANDecodedSignal> decodedSignals;
    // Only collect Signal ID 1 and 2.
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 2 };
    ASSERT_FALSE(
        decoder.decodeCANMessage( frameData.data(), frameSize, msgFormat, signalIDsToCollect, decodedSignals ) );
    ASSERT_EQ( decodedSignals.size(), 1 ); // we are expecting the signal 1 only.
}

TEST( CANDecoderTest, SkippingOutOfBoundSignals1 )
{
    // skip signals when mSizeInBits > frameSize
    // skip signals when mSizeInBits < 1

    uint8_t frameSize = 1;
    std::vector<uint8_t> frameData{ 0x01 };

    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 1;
    sigFormat1.mIsBigEndian = false;
    sigFormat1.mIsSigned = false;
    sigFormat1.mFirstBitPosition = 2;
    sigFormat1.mSizeInBits = 9;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;

    CANSignalFormat sigFormat2;
    sigFormat2.mSignalID = 2;
    sigFormat2.mIsBigEndian = false;
    sigFormat2.mIsSigned = false;
    sigFormat2.mFirstBitPosition = 1;
    sigFormat2.mSizeInBits = 0;
    sigFormat2.mOffset = 0.0;
    sigFormat2.mFactor = 1.0;

    CANMessageFormat msgFormat;
    msgFormat.mMessageID = 0x100;
    msgFormat.mSizeInBytes = 1;
    msgFormat.mSignals.emplace_back( sigFormat1 );
    msgFormat.mSignals.emplace_back( sigFormat2 );

    CANDecoder decoder;
    std::vector<CANDecodedSignal> decodedSignals;
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 2 };
    ASSERT_FALSE(
        decoder.decodeCANMessage( frameData.data(), frameSize, msgFormat, signalIDsToCollect, decodedSignals ) );
    ASSERT_EQ( decodedSignals.size(), 0 );
}

TEST( CANDecoderTest, SkippingOutOfBoundSignals2 )
{
    // skip signals when mFirstBitPosition + mSizeInBits > mSizeInBits  for when mIsBigEndian is False
    uint8_t frameSize = 1;
    std::vector<uint8_t> frameData{ 0x01 };

    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 1;
    sigFormat1.mIsBigEndian = false;
    sigFormat1.mIsSigned = false;
    sigFormat1.mFirstBitPosition = 8;
    sigFormat1.mSizeInBits = 1;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;

    CANSignalFormat sigFormat2;
    sigFormat2.mSignalID = 2;
    sigFormat2.mIsBigEndian = true;
    sigFormat2.mIsSigned = false;
    sigFormat2.mFirstBitPosition = 7;
    sigFormat2.mSizeInBits = 2;
    sigFormat2.mOffset = 0.0;
    sigFormat2.mFactor = 1.0;

    CANMessageFormat msgFormat;
    msgFormat.mMessageID = 0x100;
    msgFormat.mSizeInBytes = 1;
    msgFormat.mSignals.emplace_back( sigFormat1 );
    msgFormat.mSignals.emplace_back( sigFormat2 );

    CANDecoder decoder;
    std::vector<CANDecodedSignal> decodedSignals;
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 2 };
    ASSERT_FALSE(
        decoder.decodeCANMessage( frameData.data(), frameSize, msgFormat, signalIDsToCollect, decodedSignals ) );
    ASSERT_EQ( decodedSignals.size(), 1 );
}

TEST( CANDecoderTest, SkipWrongFloatingPointSize )
{
    // skip signals when mFirstBitPosition + mSizeInBits > mSizeInBits  for when mIsBigEndian is False
    std::vector<uint8_t> frameData{ 0x08, 0x46, 0xFF, 0x4B, 0x00, 0xD0, 0x00 };

    CANSignalFormat sigFormat1;
    sigFormat1.mSignalID = 1;
    sigFormat1.mIsBigEndian = true;
    sigFormat1.mIsSigned = false;
    sigFormat1.mFirstBitPosition = 44;
    sigFormat1.mSizeInBits = 4;
    sigFormat1.mSignalType = SignalType::DOUBLE;
    sigFormat1.mRawSignalType = RawSignalType::INTEGER;
    sigFormat1.mOffset = 0.0;
    sigFormat1.mFactor = 1.0;

    CANSignalFormat sigFormat2;
    sigFormat2.mSignalID = 7;
    sigFormat2.mIsBigEndian = true;
    sigFormat2.mIsSigned = false;
    sigFormat2.mFirstBitPosition = 28;
    sigFormat2.mSizeInBits = 12;
    sigFormat2.mSignalType = SignalType::DOUBLE;
    // A raw floating point must be 32 or 64 bits. So this signal should be skipped.
    sigFormat2.mRawSignalType = RawSignalType::FLOATING_POINT;
    sigFormat2.mOffset = 0.0;
    sigFormat2.mFactor = 0.1;

    CANMessageFormat msgFormat;
    msgFormat.mMessageID = 0x101;
    msgFormat.mSizeInBytes = 7;
    msgFormat.mSignals.emplace_back( sigFormat1 );
    msgFormat.mSignals.emplace_back( sigFormat2 );

    CANDecoder decoder;
    std::vector<CANDecodedSignal> decodedSignals;
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 7 };
    ASSERT_FALSE( decoder.decodeCANMessage( frameData.data(), 8, msgFormat, signalIDsToCollect, decodedSignals ) );
    // Only the first signal should have been decoded
    ASSERT_EQ( decodedSignals.size(), 1 );
    ASSERT_EQ( decodedSignals[0].mPhysicalValue.signalValue.doubleVal, 13 );
}

TEST( CANDecoderTest, DecodeRawFloatingPointSignals )
{
    std::vector<uint8_t> frameData{ 0x08,
                                    0x46,
                                    // float 145.35215 (0x43115a27) in little endian shifted 3 bits to the right
                                    0x38,
                                    0xD1,
                                    0x8A,
                                    0x18,
                                    0x02,
                                    // float 145.35215 in big endian
                                    0x43,
                                    0x11,
                                    0x5A,
                                    0x27,
                                    // double 47.29873879 (0x4047A63D129A8C5E) in big endian
                                    0x40,
                                    0x47,
                                    0xA6,
                                    0x3D,
                                    0x12,
                                    0x9A,
                                    0x8C,
                                    0x5E };

    CANSignalFormat formatFloatLittleEndian;
    formatFloatLittleEndian.mSignalID = 1;
    formatFloatLittleEndian.mIsBigEndian = false;
    formatFloatLittleEndian.mIsSigned = false;
    formatFloatLittleEndian.mFirstBitPosition = 19;
    formatFloatLittleEndian.mSizeInBits = 32;
    formatFloatLittleEndian.mSignalType = SignalType::DOUBLE;
    formatFloatLittleEndian.mRawSignalType = RawSignalType::FLOATING_POINT;
    formatFloatLittleEndian.mOffset = -10.0;
    formatFloatLittleEndian.mFactor = 2.0;

    CANSignalFormat formatFloatBigEndian;
    formatFloatBigEndian.mSignalID = 12;
    formatFloatBigEndian.mIsBigEndian = true;
    formatFloatBigEndian.mIsSigned = false;
    formatFloatBigEndian.mFirstBitPosition = 80;
    formatFloatBigEndian.mSizeInBits = 32;
    formatFloatBigEndian.mSignalType = SignalType::DOUBLE;
    formatFloatBigEndian.mRawSignalType = RawSignalType::FLOATING_POINT;
    formatFloatBigEndian.mOffset = 32.0;
    formatFloatBigEndian.mFactor = 0.1;

    CANSignalFormat formatDoubleBigEndian;
    formatDoubleBigEndian.mSignalID = 17;
    formatDoubleBigEndian.mIsBigEndian = true;
    formatDoubleBigEndian.mIsSigned = false;
    formatDoubleBigEndian.mFirstBitPosition = 144;
    formatDoubleBigEndian.mSizeInBits = 64;
    formatDoubleBigEndian.mSignalType = SignalType::DOUBLE;
    formatDoubleBigEndian.mRawSignalType = RawSignalType::FLOATING_POINT;
    formatDoubleBigEndian.mOffset = 100.0;
    formatDoubleBigEndian.mFactor = 10.0;

    CANMessageFormat msgFormat;
    msgFormat.mMessageID = 0x101;
    msgFormat.mSizeInBytes = 18;
    msgFormat.mSignals.emplace_back( formatFloatLittleEndian );
    msgFormat.mSignals.emplace_back( formatFloatBigEndian );
    msgFormat.mSignals.emplace_back( formatDoubleBigEndian );

    CANDecoder decoder;
    std::vector<CANDecodedSignal> decodedSignals;
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 12, 17 };
    ASSERT_TRUE( decoder.decodeCANMessage( frameData.data(), 19, msgFormat, signalIDsToCollect, decodedSignals ) );
    ASSERT_EQ( decodedSignals.size(), 3 );
    ASSERT_FLOAT_EQ( static_cast<float>( decodedSignals[0].mPhysicalValue.signalValue.doubleVal ),
                     // (145.35215 * 2) - 10
                     280.7043f );
    ASSERT_FLOAT_EQ( static_cast<float>( decodedSignals[1].mPhysicalValue.signalValue.doubleVal ),
                     // (145.35215 * 0.1) + 32
                     46.535215f );
    ASSERT_DOUBLE_EQ( decodedSignals[2].mPhysicalValue.signalValue.doubleVal,
                      // (47.29873879 * 10) + 100
                      572.9873879 );
}

} // namespace IoTFleetWise
} // namespace Aws
