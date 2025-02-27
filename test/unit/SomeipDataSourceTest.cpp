// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/SomeipDataSource.h"
#include "CommonAPIProxyMock.h"
#include "ExampleSomeipInterfaceProxyMock.h"
#include "RawDataBufferManagerSpy.h"
#include "WaitUntil.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/ExampleSomeipInterfaceWrapper.h"
#include "aws/iotfleetwise/IDecoderDictionary.h"
#include "aws/iotfleetwise/IDecoderManifest.h"
#include "aws/iotfleetwise/NamedSignalDataSource.h"
#include "aws/iotfleetwise/QueueTypes.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/VehicleDataSourceTypes.h"
#include "v1/commonapi/CommonTypes.hpp"
#include <atomic>
#include <boost/optional/optional.hpp>
#include <cstdint>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::An;
using ::testing::AnyNumber;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Matcher;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SaveArg;
using ::testing::StrictMock;

class SomeipDataSourceTest : public ::testing::Test
{
protected:
    SomeipDataSourceTest()
        : mCommonAPIProxy( std::make_shared<StrictMock<CommonAPIProxyMock>>() )
        , mXAttributeMock( std::make_shared<StrictMock<CommonAPIObservableAttributeMock<int32_t>>>() )
        , mTemperatureAttributeMock( std::make_shared<StrictMock<CommonAPIObservableAttributeMock<int32_t>>>() )
        , mA1AttributeMock(
              std::make_shared<StrictMock<CommonAPIObservableAttributeMock<v1::commonapi::CommonTypes::a1Struct>>>() )
        , mXAttributeChangedEventMock(
              std::make_shared<StrictMock<CommonAPIObservableAttributeChangedEventMock<int32_t>>>() )
        , mTemperatureAttributeChangedEventMock(
              std::make_shared<StrictMock<CommonAPIObservableAttributeChangedEventMock<int32_t>>>() )
        , mA1AttributeChangedEventMock(
              std::make_shared<
                  StrictMock<CommonAPIObservableAttributeChangedEventMock<v1::commonapi::CommonTypes::a1Struct>>>() )
        , mProxy( std::make_shared<StrictMock<ExampleSomeipInterfaceProxyMock<>>>( mCommonAPIProxy ) )
        , mRawDataBufferManagerSpy( RawData::BufferManagerConfig::create().get() )
        , mExampleSomeipInterfaceWrapper( std::make_shared<ExampleSomeipInterfaceWrapper>(
              "",
              "",
              "",
              [this]( std::string, std::string, std::string ) {
                  return mProxy;
              },
              &mRawDataBufferManagerSpy,
              false ) )
        , mSignalBuffer( std::make_shared<SignalBuffer>( 2, "Signal Buffer" ) )
        , mNamedSignalDataSource( std::make_shared<NamedSignalDataSource>( "5", mSignalBufferDistributor ) )
        , mSomeipDataSource( std::make_shared<SomeipDataSource>(
              mExampleSomeipInterfaceWrapper, mNamedSignalDataSource, &mRawDataBufferManagerSpy, 1000 ) )
        , mDictionary( std::make_shared<CustomDecoderDictionary>() )
    {
        mSignalBufferDistributor.registerQueue( mSignalBuffer );
    }

    void
    SetUp() override
    {
        ON_CALL( *mProxy, isAvailable() ).WillByDefault( Return( false ) );
        ON_CALL( *mProxy, getXAttribute() ).WillByDefault( ReturnRef( *mXAttributeMock ) );
        ON_CALL( *mXAttributeMock, getChangedEvent() ).WillByDefault( ReturnRef( *mXAttributeChangedEventMock ) );
        ON_CALL( *mXAttributeChangedEventMock, onFirstListenerAdded( _ ) )
            .WillByDefault( Invoke( [this]( std::function<void( const int32_t &val )> listener ) {
                mXAttributeListener = listener;
            } ) );
        ON_CALL( *mProxy, getTemperatureAttribute() ).WillByDefault( ReturnRef( *mTemperatureAttributeMock ) );
        ON_CALL( *mTemperatureAttributeMock, getChangedEvent() )
            .WillByDefault( ReturnRef( *mTemperatureAttributeChangedEventMock ) );
        ON_CALL( *mTemperatureAttributeChangedEventMock, onFirstListenerAdded( _ ) )
            .WillByDefault( Invoke( [this]( std::function<void( const int32_t &val )> listener ) {
                mTemperatureAttributeListener = listener;
            } ) );
        ON_CALL( *mProxy, getA1Attribute() ).WillByDefault( ReturnRef( *mA1AttributeMock ) );
        ON_CALL( *mA1AttributeMock, getChangedEvent() ).WillByDefault( ReturnRef( *mA1AttributeChangedEventMock ) );
        ON_CALL( *mA1AttributeChangedEventMock, onFirstListenerAdded( _ ) )
            .WillByDefault(
                Invoke( [this]( std::function<void( const v1::commonapi::CommonTypes::a1Struct &val )> listener ) {
                    mA1AttributeListener = listener;
                } ) );

        mDictionary->customDecoderMethod["5"]["Vehicle.ExampleSomeipInterface.X"] =
            CustomSignalDecoderFormat{ "5", "Vehicle.ExampleSomeipInterface.X", 1, SignalType::DOUBLE };
        mDictionary->customDecoderMethod["5"]["Vehicle.ExampleSomeipInterface.Temperature"] =
            CustomSignalDecoderFormat{ "5", "Vehicle.ExampleSomeipInterface.Temperature", 1, SignalType::DOUBLE };
        mDictionary->customDecoderMethod["5"]["Vehicle.ExampleSomeipInterface.A1.A2.A"] =
            CustomSignalDecoderFormat{ "5", "Vehicle.ExampleSomeipInterface.A1.A2.A", 2, SignalType::DOUBLE };
        mDictionary->customDecoderMethod["5"]["Vehicle.ExampleSomeipInterface.A1.A2.B"] =
            CustomSignalDecoderFormat{ "5", "Vehicle.ExampleSomeipInterface.A1.A2.B", 3, SignalType::DOUBLE };
        mDictionary->customDecoderMethod["5"]["Vehicle.ExampleSomeipInterface.A1.A2.D"] =
            CustomSignalDecoderFormat{ "5", "Vehicle.ExampleSomeipInterface.A1.A2.D", 4, SignalType::DOUBLE };
    }

    std::shared_ptr<StrictMock<CommonAPIProxyMock>> mCommonAPIProxy;
    std::shared_ptr<StrictMock<CommonAPIObservableAttributeMock<int32_t>>> mXAttributeMock;
    std::shared_ptr<StrictMock<CommonAPIObservableAttributeMock<int32_t>>> mTemperatureAttributeMock;
    std::shared_ptr<StrictMock<CommonAPIObservableAttributeMock<v1::commonapi::CommonTypes::a1Struct>>>
        mA1AttributeMock;
    std::shared_ptr<StrictMock<CommonAPIObservableAttributeChangedEventMock<int32_t>>> mXAttributeChangedEventMock;
    std::shared_ptr<StrictMock<CommonAPIObservableAttributeChangedEventMock<int32_t>>>
        mTemperatureAttributeChangedEventMock;
    std::shared_ptr<StrictMock<CommonAPIObservableAttributeChangedEventMock<v1::commonapi::CommonTypes::a1Struct>>>
        mA1AttributeChangedEventMock;
    std::function<void( const int32_t &val )> mXAttributeListener;
    std::function<void( const int32_t &val )> mTemperatureAttributeListener;
    std::function<void( const v1::commonapi::CommonTypes::a1Struct &val )> mA1AttributeListener;
    std::shared_ptr<StrictMock<ExampleSomeipInterfaceProxyMock<>>> mProxy;
    NiceMock<Testing::RawDataBufferManagerSpy> mRawDataBufferManagerSpy;
    std::shared_ptr<ExampleSomeipInterfaceWrapper> mExampleSomeipInterfaceWrapper;
    std::shared_ptr<SignalBuffer> mSignalBuffer;
    SignalBufferDistributor mSignalBufferDistributor;
    std::shared_ptr<NamedSignalDataSource> mNamedSignalDataSource;
    std::shared_ptr<SomeipDataSource> mSomeipDataSource;
    std::shared_ptr<CustomDecoderDictionary> mDictionary;
};

TEST_F( SomeipDataSourceTest, initUnsuccessful )
{
    auto badExampleSomeipInterfaceWrapper = std::make_shared<ExampleSomeipInterfaceWrapper>(
        "",
        "",
        "",
        []( std::string, std::string, std::string ) {
            return nullptr;
        },
        nullptr,
        false );
    auto dataSource = std::make_shared<SomeipDataSource>(
        badExampleSomeipInterfaceWrapper, mNamedSignalDataSource, &mRawDataBufferManagerSpy, 0 );
    ASSERT_FALSE( dataSource->connect() );
}

TEST_F( SomeipDataSourceTest, initSuccessfulIngestValues )
{
    // Use a mutex to prevent interruption of the block below. The isAvailable mock will be
    // periodically called by another thread, which will push more values to the signal buffer once
    // it returns as true. We do not want this in this case as we want to check that a single call
    // of the callback will push a single value to the buffer.
    std::mutex mutex;
    std::atomic<bool> insideIsAvailable{ false };
    bool isAvailable = false;
    EXPECT_CALL( *mProxy, isAvailable() ).Times( AnyNumber() ).WillRepeatedly( Invoke( [&]() {
        insideIsAvailable.store( true );
        std::lock_guard<std::mutex> lock( mutex );
        insideIsAvailable.store( false );
        return isAvailable;
    } ) );
    EXPECT_CALL( *mProxy, getXAttribute() );
    EXPECT_CALL( *mXAttributeMock, getChangedEvent() );
    EXPECT_CALL( *mXAttributeChangedEventMock, onFirstListenerAdded( _ ) );
    EXPECT_CALL( *mProxy, getTemperatureAttribute() );
    EXPECT_CALL( *mTemperatureAttributeMock, getChangedEvent() );
    EXPECT_CALL( *mTemperatureAttributeChangedEventMock, onFirstListenerAdded( _ ) );
    EXPECT_CALL( *mProxy, getA1Attribute() );
    EXPECT_CALL( *mA1AttributeMock, getChangedEvent() );
    EXPECT_CALL( *mA1AttributeChangedEventMock, onFirstListenerAdded( _ ) );
    ASSERT_TRUE( mSomeipDataSource->connect() );
    ASSERT_TRUE( mXAttributeListener );
    ASSERT_TRUE( mA1AttributeListener );

    CollectedDataFrame collectedDataFrame;
    ASSERT_FALSE( mSignalBuffer->pop( collectedDataFrame ) );

    // Other protocol:
    mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::RAW_SOCKET );

    // Custom protocol:
    mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );
    DELAY_ASSERT_FALSE( mSignalBuffer->pop( collectedDataFrame ) );

    {
        std::lock_guard<std::mutex> lock( mutex );
        isAvailable = true;
    }
    DELAY_ASSERT_FALSE( mSignalBuffer->pop( collectedDataFrame ) );

    {
        // Lock the mutex and call each callback, expecting that one of each value is pushed to the
        // buffer:
        std::lock_guard<std::mutex> lock( mutex );

        mXAttributeListener( 123 );
        ASSERT_TRUE( mSignalBuffer->pop( collectedDataFrame ) );
        ASSERT_EQ( collectedDataFrame.mActiveDTCs, nullptr );
        ASSERT_EQ( collectedDataFrame.mCollectedSignals.size(), 1 );
        ASSERT_EQ( collectedDataFrame.mCollectedSignals[0].signalID, 1 );
        ASSERT_NEAR( collectedDataFrame.mCollectedSignals[0].value.value.doubleVal, 123, 0.0001 );
        ASSERT_FALSE( mSignalBuffer->pop( collectedDataFrame ) );

        mTemperatureAttributeListener( 123 );
        ASSERT_TRUE( mSignalBuffer->pop( collectedDataFrame ) );
        ASSERT_EQ( collectedDataFrame.mActiveDTCs, nullptr );
        ASSERT_EQ( collectedDataFrame.mCollectedSignals.size(), 1 );
        ASSERT_EQ( collectedDataFrame.mCollectedSignals[0].signalID, 1 );
        ASSERT_NEAR( collectedDataFrame.mCollectedSignals[0].value.value.doubleVal, 123, 0.0001 );
        ASSERT_FALSE( mSignalBuffer->pop( collectedDataFrame ) );

        v1::commonapi::CommonTypes::a1Struct a1Struct;
        a1Struct.setS( "ABC" );
        v1::commonapi::CommonTypes::a2Struct a2Struct;
        a2Struct.setA( 456 );
        a2Struct.setB( true );
        a2Struct.setD( 789.012 );
        a1Struct.setA2( a2Struct );
        // Before calling the listener, ensure that the data source thread is waiting for the mocked
        // isAvailable call. This is intended to make the test behavior more predictable and to
        // ensure that this syncronization mechanism is working appropriately without any deadlocks
        // because the code that call isAvailable() could also be holding other mutexes that are
        // needed in the listener call.
        WAIT_ASSERT_TRUE( insideIsAvailable.load() );
        mA1AttributeListener( a1Struct );

        ASSERT_TRUE( mSignalBuffer->pop( collectedDataFrame ) );
        ASSERT_EQ( collectedDataFrame.mActiveDTCs, nullptr );
        ASSERT_EQ( collectedDataFrame.mCollectedSignals.size(), 3 );
        ASSERT_EQ( collectedDataFrame.mCollectedSignals[0].signalID, 2 );
        ASSERT_NEAR( collectedDataFrame.mCollectedSignals[0].value.value.doubleVal, 456, 0.0001 );
        ASSERT_EQ( collectedDataFrame.mCollectedSignals[1].signalID, 3 );
        ASSERT_NEAR( collectedDataFrame.mCollectedSignals[1].value.value.doubleVal, 1, 0.0001 );
        ASSERT_EQ( collectedDataFrame.mCollectedSignals[2].signalID, 4 );
        ASSERT_NEAR( collectedDataFrame.mCollectedSignals[2].value.value.doubleVal, 789.012, 0.0001 );

        ASSERT_FALSE( mSignalBuffer->pop( collectedDataFrame ) );
    }

    // Now with the mutex unlocked, wait until the background thread pushes another value:
    WAIT_ASSERT_TRUE( mSignalBuffer->pop( collectedDataFrame ) );

    mNamedSignalDataSource->onChangeOfActiveDictionary( nullptr, VehicleDataSourceProtocol::CUSTOM_DECODING );
}

} // namespace IoTFleetWise
} // namespace Aws
