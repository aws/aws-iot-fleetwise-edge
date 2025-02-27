// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/snf/DataSenderProtoReader.h"
#include "aws/iotfleetwise/CANInterfaceIDTranslator.h"
#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/DataSenderProtoWriter.h"
#include "aws/iotfleetwise/OBDDataTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

class DataSenderProtoReaderTest : public ::testing::Test
{

protected:
    DataSenderProtoReaderTest()
    {
        // required for serializing/deserializing channel ids
        mTranslator.add( "can123" );

        mProtoReader = std::make_shared<DataSenderProtoReader>( mTranslator );
        mProtoWriter = std::make_shared<DataSenderProtoWriter>( mTranslator, nullptr );
        setupTestData();
    }

    CANInterfaceIDTranslator mTranslator;
    std::shared_ptr<DataSenderProtoReader> mProtoReader;
    std::shared_ptr<DataSenderProtoWriter> mProtoWriter;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    // test data
    std::string serializedCompleteVehicleData;
    TriggeredCollectionSchemeData completeVehicleData;

    void
    setupTestData()
    {
        // complete vehicle data
        {
            TriggeredCollectionSchemeData data;
            data.eventID = 1234;

            DTCInfo info{};
            info.receiveTime = mClock->systemTimeSinceEpochMs();
            info.mSID = SID::TESTING;
            info.mDTCCodes.emplace_back( "code" );
            data.mDTCInfo = info;

            data.signals = { CollectedSignal{ 0, mClock->systemTimeSinceEpochMs(), 0, SignalType::UINT64 } };

            data.metadata = PassThroughMetadata{ true, true, 1, "decoderId", "campaignId", "campaignArn" };

            data.triggerTime = mClock->systemTimeSinceEpochMs();

            mProtoWriter->setupVehicleData( data, data.eventID );
            mProtoWriter->setupDTCInfo( info );
            for ( auto code : info.mDTCCodes )
            {
                mProtoWriter->append( code );
            }
            for ( auto signal : data.signals )
            {
                mProtoWriter->append( signal );
            }

            ASSERT_TRUE( mProtoWriter->serializeVehicleData( &serializedCompleteVehicleData ) );
            completeVehicleData = data;
        }
    }
};

TEST_F( DataSenderProtoReaderTest, TestDeserializeCompleteVehicleData )
{
    TriggeredCollectionSchemeData data{};
    mProtoReader->setupVehicleData( serializedCompleteVehicleData );
    ASSERT_TRUE( mProtoReader->deserializeVehicleData( data ) );

    ASSERT_EQ( data.eventID, completeVehicleData.eventID );
    ASSERT_EQ( data.triggerTime, completeVehicleData.triggerTime );

    // metadata
    ASSERT_EQ( data.metadata.decoderID, completeVehicleData.metadata.decoderID );
    ASSERT_EQ( data.metadata.collectionSchemeID, completeVehicleData.metadata.collectionSchemeID );
    // the following metadata isn't preserved during serialization
    ASSERT_NE( data.metadata.priority, completeVehicleData.metadata.priority );
    ASSERT_NE( data.metadata.compress, completeVehicleData.metadata.compress );
    ASSERT_NE( data.metadata.persist, completeVehicleData.metadata.persist );

    // signals
    ASSERT_EQ( data.signals.size(), completeVehicleData.signals.size() );
    for ( size_t i = 0; i < data.signals.size(); ++i )
    {
        ASSERT_EQ( data.signals[i].receiveTime, completeVehicleData.signals[i].receiveTime );
        ASSERT_EQ( data.signals[i].signalID, completeVehicleData.signals[i].signalID );
        ASSERT_EQ( data.signals[i].value.value.doubleVal,
                   static_cast<double>( completeVehicleData.signals[i].value.value.uint8Val ) );
    }

    // dtc info
    // mSID is not preserved during serialization
    ASSERT_NE( data.mDTCInfo.mSID, completeVehicleData.mDTCInfo.mSID );
    ASSERT_EQ( data.mDTCInfo.receiveTime, completeVehicleData.mDTCInfo.receiveTime );
    ASSERT_EQ( data.mDTCInfo.mDTCCodes, completeVehicleData.mDTCInfo.mDTCCodes );
}

} // namespace IoTFleetWise
} // namespace Aws
