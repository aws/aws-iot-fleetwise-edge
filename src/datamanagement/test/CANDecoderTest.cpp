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

#include "CANDecoder.h"
#include <gtest/gtest.h>
#include <unistd.h>

using namespace Aws::IoTFleetWise::DataManagement;

TEST( CANDecoderTest, CANDecoderTestSimpleMessage )
{
    // Test for Basic Decoding UseCase
    std::vector<uint8_t> frameData;
    frameData.emplace_back( 0x08 );
    frameData.emplace_back( 0x46 );
    frameData.emplace_back( 0xFF );
    frameData.emplace_back( 0x4B );
    frameData.emplace_back( 0x00 );
    frameData.emplace_back( 0xD0 );
    frameData.emplace_back( 0x00 );

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
    CANDecodedMessage decodedMsg;
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 7 };
    ASSERT_TRUE( decoder.decodeCANMessage( frameData.data(), 8, msgFormat, signalIDsToCollect, decodedMsg ) );
    ASSERT_EQ( decodedMsg.mFrameInfo.mSignals.size(), 2 );
    ASSERT_EQ( decodedMsg.mFrameInfo.mSignals[0].mRawValue, 13 );
    ASSERT_EQ( decodedMsg.mFrameInfo.mSignals[1].mRawValue, 4084 );
}

TEST( CANDecoderTest, CANDecoderTestSimpleMessage2 )
{
    // Test for Signed Numbers
    std::vector<uint8_t> frameData;
    frameData.emplace_back( 0x09 );
    frameData.emplace_back( 0x28 );
    frameData.emplace_back( 0x54 );
    frameData.emplace_back( 0xF9 );
    frameData.emplace_back( 0x6E );
    frameData.emplace_back( 0x23 );
    frameData.emplace_back( 0x6E );
    frameData.emplace_back( 0xA6 );

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
    CANDecodedMessage decodedMsg;
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 7 };
    ASSERT_TRUE( decoder.decodeCANMessage( frameData.data(), 8, msgFormat, signalIDsToCollect, decodedMsg ) );
    ASSERT_EQ( decodedMsg.mFrameInfo.mSignals.size(), 2 );
    ASSERT_EQ( decodedMsg.mFrameInfo.mSignals[0].mRawValue, 153638137 );
    ASSERT_EQ( decodedMsg.mFrameInfo.mSignals[1].mRawValue, -299667802 );
}

TEST( CANDecoderTest, CANDecoderTestSimpleMessage3 )
{
    // Test for BigEndian & LittleEndian Signals
    std::vector<uint8_t> frameData;
    frameData.emplace_back( 0x01 );
    frameData.emplace_back( 0x23 );
    frameData.emplace_back( 0x45 );
    frameData.emplace_back( 0x67 );
    frameData.emplace_back( 0x89 );
    frameData.emplace_back( 0xAB );

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
    CANDecodedMessage decodedMsg;
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 2, 3 };
    ASSERT_TRUE( decoder.decodeCANMessage( frameData.data(), 8, msgFormat, signalIDsToCollect, decodedMsg ) );
    ASSERT_EQ( decodedMsg.mFrameInfo.mSignals.size(), 3 );
    EXPECT_EQ( decodedMsg.mFrameInfo.mSignals[0].mRawValue, 0x2301 );
    EXPECT_EQ( decodedMsg.mFrameInfo.mSignals[1].mRawValue, 0x4567 );
    EXPECT_EQ( decodedMsg.mFrameInfo.mSignals[2].mRawValue, 0x89AB );
}

TEST( CANDecoderTest, CANDecoderTestOnlyDecodeSomeSignals )
{
    // Test for BigEndian & LittleEndian Signals
    std::vector<uint8_t> frameData;
    frameData.emplace_back( 0x01 );
    frameData.emplace_back( 0x23 );
    frameData.emplace_back( 0x45 );
    frameData.emplace_back( 0x67 );
    frameData.emplace_back( 0x89 );
    frameData.emplace_back( 0xAB );

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
    CANDecodedMessage decodedMsg;
    // Only collect Signal ID 1 and 3. Do not collect and decode Signal ID 2
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 3 };
    ASSERT_TRUE( decoder.decodeCANMessage( frameData.data(), 8, msgFormat, signalIDsToCollect, decodedMsg ) );
    ASSERT_EQ( decodedMsg.mFrameInfo.mSignals.size(), 2 );
    EXPECT_EQ( decodedMsg.mFrameInfo.mSignals[0].mRawValue, 0x2301 );
    EXPECT_EQ( decodedMsg.mFrameInfo.mSignals[1].mRawValue, 0x89AB );
}

TEST( CANDecoderTest, CANDecoderTestMultiplexedMessage1 )
{
    // Multiplexed Frame
    std::vector<uint8_t> frameData;
    frameData.emplace_back( 0x05 );
    frameData.emplace_back( 0x02 );
    frameData.emplace_back( 0x0B );
    frameData.emplace_back( 0x00 );
    frameData.emplace_back( 0xD3 );
    frameData.emplace_back( 0x00 );
    frameData.emplace_back( 0x4B );
    frameData.emplace_back( 0x18 );

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
    CANDecodedMessage decodedMsg;
    std::unordered_set<SignalID> signalIDsToCollect = { 50, 51, 52, 53 };
    ASSERT_TRUE( decoder.decodeCANMessage( frameData.data(), 8, msgFormat, signalIDsToCollect, decodedMsg ) );
    ASSERT_EQ( decodedMsg.mFrameInfo.mSignals.size(), 4 );

    EXPECT_EQ( decodedMsg.mFrameInfo.mSignals[0].mRawValue, 0x05 );
    EXPECT_EQ( decodedMsg.mFrameInfo.mSignals[1].mRawValue, 0x4B );
    EXPECT_EQ( decodedMsg.mFrameInfo.mSignals[2].mRawValue, 0x20B );
    EXPECT_EQ( decodedMsg.mFrameInfo.mSignals[3].mRawValue, 0xD3 );
}

TEST( CANDecoderTest, CANDecoderTestMultiplexedMessage2 )
{
    // Multiplexed Frame
    std::vector<uint8_t> frameData;
    frameData.emplace_back( 0x06 );
    frameData.emplace_back( 0x03 );
    frameData.emplace_back( 0xEB );
    frameData.emplace_back( 0x0E );
    frameData.emplace_back( 0x3B );
    frameData.emplace_back( 0x00 );
    frameData.emplace_back( 0x1B );
    frameData.emplace_back( 0x18 );

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
    CANDecodedMessage decodedMsg;
    std::unordered_set<SignalID> signalIDsToCollect = { 54, 55, 56, 57, 58 };
    ASSERT_TRUE( decoder.decodeCANMessage( frameData.data(), 8, msgFormat, signalIDsToCollect, decodedMsg ) );
    ASSERT_EQ( decodedMsg.mFrameInfo.mSignals.size(), 4 );

    EXPECT_EQ( decodedMsg.mFrameInfo.mSignals[0].mRawValue, 0x06 );
    EXPECT_EQ( decodedMsg.mFrameInfo.mSignals[1].mRawValue, 0x1B );
    EXPECT_EQ( decodedMsg.mFrameInfo.mSignals[2].mRawValue, 0x3EB );
    EXPECT_EQ( decodedMsg.mFrameInfo.mSignals[3].mRawValue, 0xE3B );
}

TEST( CANDecoderTest, CANDecoderTestInvalidSignalLayout )
{
    // Test for a shorter CAN frame than Signal layout.
    // Message has only 1 byte while the signal 2 to collect locates at bit 8.
    uint8_t frameSize = 1;
    std::vector<uint8_t> frameData;
    frameData.emplace_back( 0x01 );

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
    CANDecodedMessage decodedMsg;
    // Only collect Signal ID 1 and 2.
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 2 };
    ASSERT_FALSE( decoder.decodeCANMessage( frameData.data(), frameSize, msgFormat, signalIDsToCollect, decodedMsg ) );
    ASSERT_EQ( decodedMsg.mFrameInfo.mSignals.size(), 1 ); // we are expecting the signal 1 only.
}

TEST( CANDecoderTest, CANDecoderTestSkippingOutOfBoundSignals1 )
{
    // skip signals when mSizeInBits > frameSize
    // skip signals when mSizeInBits < 1

    uint8_t frameSize = 1;
    std::vector<uint8_t> frameData;
    frameData.emplace_back( 0x01 );

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
    CANDecodedMessage decodedMsg;
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 2 };
    ASSERT_FALSE( decoder.decodeCANMessage( frameData.data(), frameSize, msgFormat, signalIDsToCollect, decodedMsg ) );
    ASSERT_EQ( decodedMsg.mFrameInfo.mSignals.size(), 0 );
}

TEST( CANDecoderTest, CANDecoderTestSkippingOutOfBoundSignals2 )
{
    // skip signals when mFirstBitPosition + mSizeInBits > mSizeInBits  for when mIsBigEndian is False
    uint8_t frameSize = 1;
    std::vector<uint8_t> frameData;
    frameData.emplace_back( 0x01 );

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
    CANDecodedMessage decodedMsg;
    std::unordered_set<SignalID> signalIDsToCollect = { 1, 2 };
    ASSERT_FALSE( decoder.decodeCANMessage( frameData.data(), frameSize, msgFormat, signalIDsToCollect, decodedMsg ) );
    ASSERT_EQ( decodedMsg.mFrameInfo.mSignals.size(), 1 );
}