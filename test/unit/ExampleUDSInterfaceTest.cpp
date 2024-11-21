// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ExampleUDSInterface.h"
#include "CollectionInspectionAPITypes.h"
#include "DataFetchManagerAPITypes.h"
#include "IDecoderDictionary.h"
#include "IDecoderManifest.h"
#include "IRemoteDiagnostics.h"
#include "ISOTPOverCANOptions.h"
#include "ISOTPOverCANSender.h"
#include "ISOTPOverCANSenderReceiver.h"
#include "LoggingModule.h"
#include "NamedSignalDataSource.h"
#include "QueueTypes.h"
#include "RawDataManager.h"
#include "RemoteDiagnosticDataSource.h"
#include "SignalTypes.h"
#include "Testing.h"
#include "VehicleDataSourceTypes.h"
#include "WaitUntil.h"
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>
#include <cstdint>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

class UDSTemplateInterfaceTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        EcuConfig ecuConfig;
        ecuConfig.targetAddress = 1;
        ecuConfig.canBus = "vcan0";
        ecuConfig.ecuName = "ECM";
        ecuConfig.functionalAddress = 0x7DF;
        ecuConfig.physicalRequestID = 0x123;
        ecuConfig.physicalResponseID = 0x456;
        mEcuConfigs.emplace_back( ecuConfig );
        mSignalBuffer = std::make_shared<SignalBuffer>( 256, "Signal Buffer" );
        mSignalBufferDistributor = std::make_shared<SignalBufferDistributor>();
        mSignalBufferDistributor->registerQueue( mSignalBuffer );
        mNamedSignalDataSource = std::make_shared<NamedSignalDataSource>( "interface1", mSignalBufferDistributor );
        mUDSInterface = std::make_shared<ExampleUDSInterface>();
        mDictionary = std::make_shared<CustomDecoderDictionary>();
        mDictionary->customDecoderMethod["interface1"]["Vehicle.ECU1.DTC_INFO"] =
            CustomSignalDecoderFormat{ "interface1", "Vehicle.ECU1.DTC_INFO", 0x1234, SignalType::STRING };
    }
    void
    TearDown() override
    {
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
    functionalSenderReceiverSendPDU( ISOTPOverCANSenderReceiver &senderReceiver, std::vector<uint8_t> &pdu )
    {
        if ( !senderReceiver.receivePDU( pdu ) )
        {
            FWE_LOG_ERROR( "Error receiving ISO-TP message" );
            return;
        }
        if ( pdu.at( 1 ) == 0x02 )
        {
            auto txPDUData = std::vector<uint8_t>( { 0x59, 0x02, 0xAA, 0x11, 0x22, 0x33 } );
            ISOTPOverCANSender sender;
            ISOTPOverCANSenderOptions senderOptions;
            senderOptions.mSocketCanIFName = "vcan0";
            senderOptions.mSourceCANId = 0x456;
            senderOptions.mDestinationCANId = 0x123;
            ASSERT_TRUE( sender.init( senderOptions ) );
            ASSERT_TRUE( sender.connect() );
            sender.sendPDU( txPDUData );
            sender.disconnect();
        }
        else if ( pdu.at( 1 ) == 0x04 )
        {
            auto txPDUData = std::vector<uint8_t>(
                { 0x59, 0x04, 0x11, 0x22, 0x33, 0x24, 0x02, 0x01, 0x47, 0x11, 0xA6, 0x66, 0x07, 0x50, 0x20 } );
            ISOTPOverCANSender sender;
            ISOTPOverCANSenderOptions senderOptions;
            senderOptions.mSocketCanIFName = "vcan0";
            senderOptions.mSourceCANId = 0x456;
            senderOptions.mDestinationCANId = 0x123;
            ASSERT_TRUE( sender.init( senderOptions ) );
            ASSERT_TRUE( sender.connect() );
            sender.sendPDU( txPDUData );
            sender.disconnect();
        }
        else if ( pdu.at( 1 ) == 0x06 )
        {
            auto txPDUData = std::vector<uint8_t>(
                { 0x59, 0x06, 0x11, 0x22, 0x33, 0x24, 0x02, 0x01, 0x47, 0x11, 0xA6, 0x66, 0x07, 0x50, 0x20 } );
            ISOTPOverCANSender sender;
            ISOTPOverCANSenderOptions senderOptions;
            senderOptions.mSocketCanIFName = "vcan0";
            senderOptions.mSourceCANId = 0x456;
            senderOptions.mDestinationCANId = 0x123;
            ASSERT_TRUE( sender.init( senderOptions ) );
            ASSERT_TRUE( sender.connect() );
            sender.sendPDU( txPDUData );
            sender.disconnect();
        }
    }

    static void
    phySenderReceiverSendPDU( ISOTPOverCANSenderReceiver &senderReceiver, std::vector<uint8_t> &pdu )
    {
        if ( !senderReceiver.receivePDU( pdu ) )
        {
            FWE_LOG_ERROR( "Error receiving ISO-TP message" );
            return;
        }
        if ( pdu.at( 1 ) == 0x02 )
        {
            auto txPDUData = std::vector<uint8_t>( { 0x59, 0x02, 0xFF, 0xAA, 0x11, 0x22, 0x33 } );
            senderReceiver.sendPDU( txPDUData );
        }
        if ( pdu.at( 1 ) == 0x03 )
        {
            auto txPDUData = std::vector<uint8_t>( { 0x59, 0x03, 0xAA, 0x11, 0x22, 0x01 } );
            senderReceiver.sendPDU( txPDUData );
        }
        else if ( pdu.at( 1 ) == 0x04 )
        {
            auto txPDUData = std::vector<uint8_t>(
                { 0x59, 0x04, 0x11, 0x22, 0x33, 0x24, 0x02, 0x01, 0x47, 0x11, 0xA6, 0x66, 0x07, 0x50, 0x20 } );
            senderReceiver.sendPDU( txPDUData );
        }
        else if ( pdu.at( 1 ) == 0x06 )
        {
            auto txPDUData = std::vector<uint8_t>(
                { 0x59, 0x06, 0xAA, 0x11, 0x22, 0x33, 0x24, 0x02, 0x01, 0x47, 0x11, 0xA6, 0x66, 0x07, 0x50, 0x20 } );
            senderReceiver.sendPDU( txPDUData );
        }
    }

    std::thread mThread;
    std::shared_ptr<RemoteDiagnosticDataSource> mUdsModule;
    std::shared_ptr<ExampleUDSInterface> mUDSInterface;
    SignalBufferPtr mSignalBuffer;
    SignalBufferDistributorPtr mSignalBufferDistributor;
    std::shared_ptr<NamedSignalDataSource> mNamedSignalDataSource;
    std::shared_ptr<CustomDecoderDictionary> mDictionary;
    std::vector<EcuConfig> mEcuConfigs;
};

TEST_F( UDSTemplateInterfaceTest, InitFailureEmptyConfig )
{
    std::vector<EcuConfig> emptyConfig;
    ASSERT_FALSE( mUDSInterface->init( emptyConfig ) );
}

TEST_F( UDSTemplateInterfaceTest, NoNamedSignalDataSource )
{
    mUdsModule = std::make_shared<RemoteDiagnosticDataSource>( nullptr, nullptr, mUDSInterface );

    ASSERT_TRUE( mUDSInterface->init( mEcuConfigs ) );
    ASSERT_TRUE( mUdsModule->start() );
    std::vector<InspectionValue> params;
    InspectionValue targetAddress, udsSubFunctionValue, udsStatusMaskValue;
    targetAddress = static_cast<double>( -1 );
    udsSubFunctionValue = static_cast<double>( UDSSubFunction::DTC_BY_STATUS_MASK );
    udsStatusMaskValue = static_cast<double>( -1 );
    params.push_back( std::move( targetAddress ) );
    params.push_back( std::move( udsSubFunctionValue ) );
    params.push_back( std::move( udsStatusMaskValue ) );

    ASSERT_EQ( mUdsModule->DTC_QUERY( 0x1234, 0x03, params ), FetchErrorCode::SIGNAL_NOT_FOUND );
}

TEST_F( UDSTemplateInterfaceTest, UnknownSignal )
{
    mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );

    mUdsModule = std::make_shared<RemoteDiagnosticDataSource>( mNamedSignalDataSource, nullptr, mUDSInterface );

    ASSERT_TRUE( mUDSInterface->init( mEcuConfigs ) );
    ASSERT_TRUE( mUdsModule->start() );
    std::vector<InspectionValue> params;
    InspectionValue targetAddress, udsSubFunctionValue, udsStatusMaskValue;
    targetAddress = static_cast<double>( -1 );
    udsSubFunctionValue = static_cast<double>( UDSSubFunction::DTC_BY_STATUS_MASK );
    udsStatusMaskValue = static_cast<double>( -1 );
    params.push_back( std::move( targetAddress ) );
    params.push_back( std::move( udsSubFunctionValue ) );
    params.push_back( std::move( udsStatusMaskValue ) );

    // Unknown SignalID
    ASSERT_EQ( mUdsModule->DTC_QUERY( 756, 0x03, params ), FetchErrorCode::SIGNAL_NOT_FOUND );
}

TEST_F( UDSTemplateInterfaceTest, UnsupportedDTCQueryParameters )
{
    mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );

    mUdsModule = std::make_shared<RemoteDiagnosticDataSource>( mNamedSignalDataSource, nullptr, mUDSInterface );

    ASSERT_TRUE( mUDSInterface->init( mEcuConfigs ) );
    ASSERT_TRUE( mUdsModule->start() );
    std::vector<InspectionValue> params;
    InspectionValue targetAddress, udsSubFunctionValue, udsStatusMaskValue;
    params.push_back( std::move( targetAddress ) );
    ASSERT_EQ( mUdsModule->DTC_QUERY( 0x1234, 0x03, params ), FetchErrorCode::UNSUPPORTED_PARAMETERS );
    params.clear();

    targetAddress = static_cast<double>( -1 );
    udsSubFunctionValue = static_cast<double>( UDSSubFunction::DTC_BY_STATUS_MASK );
    udsStatusMaskValue = static_cast<double>( -1 );

    params.push_back( "wrong target address" );
    params.push_back( std::move( udsSubFunctionValue ) );
    params.push_back( std::move( udsStatusMaskValue ) );
    ASSERT_EQ( mUdsModule->DTC_QUERY( 0x1234, 0x03, params ), FetchErrorCode::UNSUPPORTED_PARAMETERS );
    params.clear();

    params.push_back( std::move( targetAddress ) );
    params.push_back( "wrong sub function" );
    params.push_back( std::move( udsStatusMaskValue ) );
    ASSERT_EQ( mUdsModule->DTC_QUERY( 0x1234, 0x03, params ), FetchErrorCode::UNSUPPORTED_PARAMETERS );
    params.clear();

    params.push_back( std::move( targetAddress ) );
    params.push_back( -1 );
    params.push_back( std::move( udsStatusMaskValue ) );
    ASSERT_EQ( mUdsModule->DTC_QUERY( 0x1234, 0x03, params ), FetchErrorCode::UNSUPPORTED_PARAMETERS );
    params.clear();

    params.push_back( std::move( targetAddress ) );
    params.push_back( std::move( udsSubFunctionValue ) );
    params.push_back( 0 );
    ASSERT_EQ( mUdsModule->DTC_QUERY( 0x1234, 0x03, params ), FetchErrorCode::UNSUPPORTED_PARAMETERS );
    params.clear();

    params.push_back( std::move( targetAddress ) );
    params.push_back( std::move( udsSubFunctionValue ) );
    params.push_back( "wrong status mask" );
    ASSERT_EQ( mUdsModule->DTC_QUERY( 0x1234, 0x03, params ), FetchErrorCode::UNSUPPORTED_PARAMETERS );
    params.clear();

    params.push_back( std::move( targetAddress ) );
    params.push_back( std::move( udsSubFunctionValue ) );
    params.push_back( std::move( udsStatusMaskValue ) );
    params.push_back( 0 );
    ASSERT_EQ( mUdsModule->DTC_QUERY( 0x1234, 0x03, params ), FetchErrorCode::UNSUPPORTED_PARAMETERS );
    params.clear();

    params.push_back( std::move( targetAddress ) );
    params.push_back( std::move( udsSubFunctionValue ) );
    params.push_back( std::move( udsStatusMaskValue ) );
    params.push_back( "wrong dtc number" );
    ASSERT_EQ( mUdsModule->DTC_QUERY( 0x1234, 0x03, params ), FetchErrorCode::UNSUPPORTED_PARAMETERS );
    params.clear();

    params.push_back( std::move( targetAddress ) );
    params.push_back( std::move( udsSubFunctionValue ) );
    params.push_back( std::move( udsStatusMaskValue ) );
    params.push_back( "0x112233" );
    params.push_back( "wrong record id" );
    ASSERT_EQ( mUdsModule->DTC_QUERY( 0x1234, 0x03, params ), FetchErrorCode::UNSUPPORTED_PARAMETERS );
    params.clear();
}

TEST_F( UDSTemplateInterfaceTest, TestDTCQuery )
{
    std::shared_ptr<RawData::BufferManager> rawDataBufferManager;
    boost::optional<RawData::BufferManagerConfig> rawDataBufferManagerConfig;
    RawData::SignalUpdateConfig signalUpdateConfig1;
    std::vector<RawData::SignalBufferOverrides> rawDataBufferOverridesPerSignal;
    signalUpdateConfig1.typeId = 0x1234;
    RawData::SignalBufferOverrides signalOverrides;
    signalOverrides.interfaceId = "interface1";
    signalUpdateConfig1.interfaceId = signalOverrides.interfaceId;
    signalOverrides.messageId = "Vehicle.ECU1.DTC_INFO";
    signalUpdateConfig1.messageId = signalOverrides.messageId;
    rawDataBufferOverridesPerSignal = { signalOverrides };
    std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> updatedSignals;
    updatedSignals = { { 0x1234, signalUpdateConfig1 } };
    rawDataBufferManagerConfig = RawData::BufferManagerConfig::create(
        1_GiB, boost::none, boost::none, boost::none, boost::none, rawDataBufferOverridesPerSignal );

    rawDataBufferManager = std::make_shared<RawData::BufferManager>( rawDataBufferManagerConfig.get() );
    rawDataBufferManager->updateConfig( updatedSignals );

    mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );

    ASSERT_TRUE( mUDSInterface->init( mEcuConfigs ) );
    ASSERT_TRUE( mUDSInterface->start() );
    mUdsModule =
        std::make_shared<RemoteDiagnosticDataSource>( mNamedSignalDataSource, rawDataBufferManager, mUDSInterface );

    ASSERT_TRUE( mUdsModule->start() );
    std::vector<InspectionValue> params;
    InspectionValue targetAddress, udsSubFunctionValue, udsStatusMaskValue;
    targetAddress = static_cast<double>( -1 );
    udsSubFunctionValue = static_cast<double>( UDSSubFunction::DTC_BY_STATUS_MASK );
    udsStatusMaskValue = static_cast<double>( -1 );
    params.push_back( std::move( targetAddress ) );
    params.push_back( std::move( udsSubFunctionValue ) );
    params.push_back( std::move( udsStatusMaskValue ) );

    ISOTPOverCANSenderReceiver senderReceiver;
    ISOTPOverCANSenderReceiverOptions senderReceiverOptions;
    senderReceiverOptions.mSocketCanIFName = "vcan0";
    senderReceiverOptions.mSourceCANId = 0;
    senderReceiverOptions.mDestinationCANId = 0x7DF;
    ASSERT_TRUE( senderReceiver.init( senderReceiverOptions ) );
    ASSERT_TRUE( senderReceiver.connect() );
    std::vector<uint8_t> rxPDUData;
    std::thread receiverThread( &functionalSenderReceiverSendPDU, std::ref( senderReceiver ), std::ref( rxPDUData ) );

    ASSERT_EQ( mUdsModule->DTC_QUERY( 0x1234, 0x03, params ), FetchErrorCode::SUCCESSFUL );
    receiverThread.join();
    senderReceiver.disconnect();

    CollectedDataFrame collectedDataFrame;
    WAIT_ASSERT_TRUE( mSignalBuffer->pop( collectedDataFrame ) );
    ASSERT_EQ( collectedDataFrame.mCollectedSignals.size(), 1 );
    auto signal = collectedDataFrame.mCollectedSignals[0];
    ASSERT_EQ( signal.value.type, SignalType::STRING );
    ASSERT_EQ( signal.fetchRequestID, 0x03 );
    // Check string content
    std::string dtcInfo =
        "{\"DetectedDTCs\":[{\"DTCAndSnapshot\":{\"DTCStatusAvailabilityMask\":\"AA\",\"dtcCodes\":[{\"DTC\":"
        "\"112233\",\"DTCExtendedData\":\"\",\"DTCSnapshotRecord\":\"\"}]},\"ECUID\":\"01\"}]}";
    auto loanedRawDataFrame = rawDataBufferManager->borrowFrame(
        0x1234, static_cast<RawData::BufferHandle>( signal.getValue().value.uint32Val ) );
    if ( !loanedRawDataFrame.isNull() )
    {
        auto data = loanedRawDataFrame.getData();
        auto size = loanedRawDataFrame.getSize();
        std::string dataString = std::string( reinterpret_cast<const char *>( data ), size );
        ASSERT_EQ( dataString, dtcInfo );
    }
}

TEST_F( UDSTemplateInterfaceTest, TestDTCSnapshotQueryForSpecificCodeForSpecificRecord )
{
    std::shared_ptr<RawData::BufferManager> rawDataBufferManager;
    boost::optional<RawData::BufferManagerConfig> rawDataBufferManagerConfig;
    RawData::SignalUpdateConfig signalUpdateConfig1;
    std::vector<RawData::SignalBufferOverrides> rawDataBufferOverridesPerSignal;
    signalUpdateConfig1.typeId = 0x1234;
    RawData::SignalBufferOverrides signalOverrides;
    signalOverrides.interfaceId = "interface1";
    signalUpdateConfig1.interfaceId = signalOverrides.interfaceId;
    signalOverrides.messageId = "Vehicle.ECU1.DTC_INFO";
    signalUpdateConfig1.messageId = signalOverrides.messageId;
    rawDataBufferOverridesPerSignal = { signalOverrides };
    std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> updatedSignals;
    updatedSignals = { { 0x1234, signalUpdateConfig1 } };
    rawDataBufferManagerConfig = RawData::BufferManagerConfig::create(
        1_GiB, boost::none, boost::none, boost::none, boost::none, rawDataBufferOverridesPerSignal );

    rawDataBufferManager = std::make_shared<RawData::BufferManager>( rawDataBufferManagerConfig.get() );
    rawDataBufferManager->updateConfig( updatedSignals );

    mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );

    ASSERT_TRUE( mUDSInterface->init( mEcuConfigs ) );
    ASSERT_TRUE( mUDSInterface->start() );
    mUdsModule =
        std::make_shared<RemoteDiagnosticDataSource>( mNamedSignalDataSource, rawDataBufferManager, mUDSInterface );
    ASSERT_TRUE( mUdsModule->start() );
    std::vector<InspectionValue> params;
    InspectionValue targetAddress, udsSubFunctionValue, udsStatusMaskValue, dtcString, recordNumber;
    targetAddress = static_cast<double>( 0x01 );
    udsSubFunctionValue = static_cast<double>( UDSSubFunction::DTC_SNAPSHOT_RECORD_BY_DTC_NUMBER );
    udsStatusMaskValue = static_cast<double>( -1 );
    dtcString = static_cast<std::string>( "0x112233" );
    recordNumber = static_cast<double>( 1 );
    params.push_back( std::move( targetAddress ) );
    params.push_back( std::move( udsSubFunctionValue ) );
    params.push_back( std::move( udsStatusMaskValue ) );
    params.push_back( std::move( dtcString ) );
    params.push_back( std::move( recordNumber ) );

    ISOTPOverCANSenderReceiver senderReceiver;
    ISOTPOverCANSenderReceiverOptions senderReceiverOptions;
    senderReceiverOptions.mSocketCanIFName = "vcan0";
    senderReceiverOptions.mSourceCANId = 0x456;
    senderReceiverOptions.mDestinationCANId = 0x123;
    ASSERT_TRUE( senderReceiver.init( senderReceiverOptions ) );
    ASSERT_TRUE( senderReceiver.connect() );
    std::vector<uint8_t> rxPDUData;
    std::thread receiverThread( &phySenderReceiverSendPDU, std::ref( senderReceiver ), std::ref( rxPDUData ) );
    ASSERT_EQ( mUdsModule->DTC_QUERY( 0x1234, 0x03, params ), FetchErrorCode::SUCCESSFUL );
    CollectedDataFrame collectedDataFrame;
    WAIT_ASSERT_TRUE( mSignalBuffer->pop( collectedDataFrame ) );
    receiverThread.join();
    senderReceiver.disconnect();
    ASSERT_EQ( collectedDataFrame.mCollectedSignals.size(), 1 );
    auto signal = collectedDataFrame.mCollectedSignals[0];
    ASSERT_EQ( signal.value.type, SignalType::STRING );
    ASSERT_EQ( signal.fetchRequestID, 0x03 );
    // Check string content
    std::string dtcInfo = "{\"DetectedDTCs\":[{\"DTCAndSnapshot\":{\"DTCStatusAvailabilityMask\":\"00\",\"dtcCodes\":[{"
                          "\"DTC\":\"112233\",\"DTCExtendedData\":\"\",\"DTCSnapshotRecord\":"
                          "\"1122332402014711A666075020\"}]},\"ECUID\":\"01\"}]}";
    auto loanedRawDataFrame = rawDataBufferManager->borrowFrame(
        0x1234, static_cast<RawData::BufferHandle>( signal.getValue().value.uint32Val ) );
    if ( !loanedRawDataFrame.isNull() )
    {
        auto data = loanedRawDataFrame.getData();
        auto size = loanedRawDataFrame.getSize();
        std::string dataString = std::string( reinterpret_cast<const char *>( data ), size );
        ASSERT_EQ( dataString, dtcInfo );
    }
}

TEST_F( UDSTemplateInterfaceTest, TestDTCExtendedQuery )
{
    std::shared_ptr<RawData::BufferManager> rawDataBufferManager;
    boost::optional<RawData::BufferManagerConfig> rawDataBufferManagerConfig;
    RawData::SignalUpdateConfig signalUpdateConfig1;
    std::vector<RawData::SignalBufferOverrides> rawDataBufferOverridesPerSignal;
    signalUpdateConfig1.typeId = 0x1234;
    RawData::SignalBufferOverrides signalOverrides;
    signalOverrides.interfaceId = "interface1";
    signalUpdateConfig1.interfaceId = signalOverrides.interfaceId;
    signalOverrides.messageId = "Vehicle.ECU1.DTC_INFO";
    signalUpdateConfig1.messageId = signalOverrides.messageId;
    rawDataBufferOverridesPerSignal = { signalOverrides };
    std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> updatedSignals;
    updatedSignals = { { 0x1234, signalUpdateConfig1 } };
    rawDataBufferManagerConfig = RawData::BufferManagerConfig::create(
        1_GiB, boost::none, boost::none, boost::none, boost::none, rawDataBufferOverridesPerSignal );

    rawDataBufferManager = std::make_shared<RawData::BufferManager>( rawDataBufferManagerConfig.get() );
    rawDataBufferManager->updateConfig( updatedSignals );

    mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );

    ASSERT_TRUE( mUDSInterface->init( mEcuConfigs ) );
    ASSERT_TRUE( mUDSInterface->start() );
    mUdsModule =
        std::make_shared<RemoteDiagnosticDataSource>( mNamedSignalDataSource, rawDataBufferManager, mUDSInterface );
    ASSERT_TRUE( mUdsModule->start() );
    std::vector<InspectionValue> params;
    InspectionValue targetAddress, udsSubFunctionValue, udsStatusMaskValue, dtcString, recordNumber;
    targetAddress = static_cast<double>( 0x01 );
    udsSubFunctionValue = static_cast<double>( UDSSubFunction::DTC_EXT_DATA_RECORD_BY_DTC_NUMBER );
    udsStatusMaskValue = static_cast<double>( -1 );
    params.push_back( std::move( targetAddress ) );
    params.push_back( std::move( udsSubFunctionValue ) );
    params.push_back( std::move( udsStatusMaskValue ) );

    ISOTPOverCANSenderReceiver senderReceiver;
    ISOTPOverCANSenderReceiverOptions senderReceiverOptions;
    senderReceiverOptions.mSocketCanIFName = "vcan0";
    senderReceiverOptions.mSourceCANId = 0x456;
    senderReceiverOptions.mDestinationCANId = 0x123;
    ASSERT_TRUE( senderReceiver.init( senderReceiverOptions ) );
    ASSERT_TRUE( senderReceiver.connect() );
    std::vector<uint8_t> rxPDUData;
    std::thread receiverThread( &phySenderReceiverSendPDU, std::ref( senderReceiver ), std::ref( rxPDUData ) );
    ASSERT_EQ( mUdsModule->DTC_QUERY( 0x1234, 0x03, params ), FetchErrorCode::SUCCESSFUL );
    receiverThread.join();

    std::thread recordIDReceiverThread( &phySenderReceiverSendPDU, std::ref( senderReceiver ), std::ref( rxPDUData ) );
    recordIDReceiverThread.join();

    std::thread extendedDataReceiverThread(
        &phySenderReceiverSendPDU, std::ref( senderReceiver ), std::ref( rxPDUData ) );
    extendedDataReceiverThread.join();

    CollectedDataFrame collectedDataFrame;
    WAIT_ASSERT_TRUE( mSignalBuffer->pop( collectedDataFrame ) );
    senderReceiver.disconnect();
    ASSERT_EQ( collectedDataFrame.mCollectedSignals.size(), 1 );
    auto signal = collectedDataFrame.mCollectedSignals[0];
    ASSERT_EQ( signal.value.type, SignalType::STRING );
    ASSERT_EQ( signal.fetchRequestID, 0x03 );
    // Check string content
    std::string dtcInfo =
        "{\"DetectedDTCs\":[{\"DTCAndSnapshot\":{\"DTCStatusAvailabilityMask\":\"FF\",\"dtcCodes\":[{"
        "\"DTC\":\"AA1122\",\"DTCExtendedData\":\"AA1122332402014711A666075020\",\"DTCSnapshotRecord\":"
        "\"\"}]},\"ECUID\":\"01\"}]}";
    auto loanedRawDataFrame = rawDataBufferManager->borrowFrame(
        0x1234, static_cast<RawData::BufferHandle>( signal.getValue().value.uint32Val ) );
    if ( !loanedRawDataFrame.isNull() )
    {
        auto data = loanedRawDataFrame.getData();
        auto size = loanedRawDataFrame.getSize();
        std::string dataString = std::string( reinterpret_cast<const char *>( data ), size );
        ASSERT_EQ( dataString, dtcInfo );
    }
}

TEST_F( UDSTemplateInterfaceTest, TestDTCExtendedQueryForSpecificCode )
{
    std::shared_ptr<RawData::BufferManager> rawDataBufferManager;
    boost::optional<RawData::BufferManagerConfig> rawDataBufferManagerConfig;
    RawData::SignalUpdateConfig signalUpdateConfig1;
    std::vector<RawData::SignalBufferOverrides> rawDataBufferOverridesPerSignal;
    signalUpdateConfig1.typeId = 0x1234;
    RawData::SignalBufferOverrides signalOverrides;
    signalOverrides.interfaceId = "interface1";
    signalUpdateConfig1.interfaceId = signalOverrides.interfaceId;
    signalOverrides.messageId = "Vehicle.ECU1.DTC_INFO";
    signalUpdateConfig1.messageId = signalOverrides.messageId;
    rawDataBufferOverridesPerSignal = { signalOverrides };
    std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> updatedSignals;
    updatedSignals = { { 0x1234, signalUpdateConfig1 } };
    rawDataBufferManagerConfig = RawData::BufferManagerConfig::create(
        1_GiB, boost::none, boost::none, boost::none, boost::none, rawDataBufferOverridesPerSignal );

    rawDataBufferManager = std::make_shared<RawData::BufferManager>( rawDataBufferManagerConfig.get() );
    rawDataBufferManager->updateConfig( updatedSignals );

    mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );

    ASSERT_TRUE( mUDSInterface->init( mEcuConfigs ) );
    ASSERT_TRUE( mUDSInterface->start() );
    mUdsModule =
        std::make_shared<RemoteDiagnosticDataSource>( mNamedSignalDataSource, rawDataBufferManager, mUDSInterface );
    ASSERT_TRUE( mUdsModule->start() );
    std::vector<InspectionValue> params;
    InspectionValue targetAddress, udsSubFunctionValue, udsStatusMaskValue, dtcString, recordNumber;
    targetAddress = static_cast<double>( 0x01 );
    udsSubFunctionValue = static_cast<double>( UDSSubFunction::DTC_EXT_DATA_RECORD_BY_DTC_NUMBER );
    udsStatusMaskValue = static_cast<double>( -1 );
    dtcString = static_cast<std::string>( "0xAA1122" );
    params.push_back( std::move( targetAddress ) );
    params.push_back( std::move( udsSubFunctionValue ) );
    params.push_back( std::move( udsStatusMaskValue ) );
    params.push_back( std::move( dtcString ) );

    ISOTPOverCANSenderReceiver senderReceiver;
    ISOTPOverCANSenderReceiverOptions senderReceiverOptions;
    senderReceiverOptions.mSocketCanIFName = "vcan0";
    senderReceiverOptions.mSourceCANId = 0x456;
    senderReceiverOptions.mDestinationCANId = 0x123;
    ASSERT_TRUE( senderReceiver.init( senderReceiverOptions ) );
    ASSERT_TRUE( senderReceiver.connect() );
    std::vector<uint8_t> rxPDUData;
    std::thread receiverThread( &phySenderReceiverSendPDU, std::ref( senderReceiver ), std::ref( rxPDUData ) );
    ASSERT_EQ( mUdsModule->DTC_QUERY( 0x1234, 0x03, params ), FetchErrorCode::SUCCESSFUL );
    receiverThread.join();

    std::thread recordIDReceiverThread( &phySenderReceiverSendPDU, std::ref( senderReceiver ), std::ref( rxPDUData ) );
    recordIDReceiverThread.join();

    std::thread extendedDataReceiverThread(
        &phySenderReceiverSendPDU, std::ref( senderReceiver ), std::ref( rxPDUData ) );
    extendedDataReceiverThread.join();

    CollectedDataFrame collectedDataFrame;
    WAIT_ASSERT_TRUE( mSignalBuffer->pop( collectedDataFrame ) );
    senderReceiver.disconnect();
    ASSERT_EQ( collectedDataFrame.mCollectedSignals.size(), 1 );
    auto signal = collectedDataFrame.mCollectedSignals[0];
    ASSERT_EQ( signal.value.type, SignalType::STRING );
    ASSERT_EQ( signal.fetchRequestID, 0x03 );
    // Check string content
    std::string dtcInfo =
        "{\"DetectedDTCs\":[{\"DTCAndSnapshot\":{\"DTCStatusAvailabilityMask\":\"00\",\"dtcCodes\":[{"
        "\"DTC\":\"AA1122\",\"DTCExtendedData\":\"AA1122332402014711A666075020\",\"DTCSnapshotRecord\":"
        "\"\"}]},\"ECUID\":\"01\"}]}";
    auto loanedRawDataFrame = rawDataBufferManager->borrowFrame(
        0x1234, static_cast<RawData::BufferHandle>( signal.getValue().value.uint32Val ) );
    if ( !loanedRawDataFrame.isNull() )
    {
        auto data = loanedRawDataFrame.getData();
        auto size = loanedRawDataFrame.getSize();
        std::string dataString = std::string( reinterpret_cast<const char *>( data ), size );
        ASSERT_EQ( dataString, dtcInfo );
    }
}

TEST_F( UDSTemplateInterfaceTest, InvalidDTCQueryParametersForSnapshotQuery )
{
    std::shared_ptr<RawData::BufferManager> rawDataBufferManager;
    boost::optional<RawData::BufferManagerConfig> rawDataBufferManagerConfig;
    RawData::SignalUpdateConfig signalUpdateConfig1;
    std::vector<RawData::SignalBufferOverrides> rawDataBufferOverridesPerSignal;
    signalUpdateConfig1.typeId = 0x1234;
    RawData::SignalBufferOverrides signalOverrides;
    signalOverrides.interfaceId = "interface1";
    signalUpdateConfig1.interfaceId = signalOverrides.interfaceId;
    signalOverrides.messageId = "Vehicle.ECU1.DTC_INFO";
    signalUpdateConfig1.messageId = signalOverrides.messageId;
    rawDataBufferOverridesPerSignal = { signalOverrides };
    std::unordered_map<RawData::BufferTypeId, RawData::SignalUpdateConfig> updatedSignals;
    updatedSignals = { { 0x1234, signalUpdateConfig1 } };
    rawDataBufferManagerConfig = RawData::BufferManagerConfig::create(
        1_GiB, boost::none, boost::none, boost::none, boost::none, rawDataBufferOverridesPerSignal );

    rawDataBufferManager = std::make_shared<RawData::BufferManager>( rawDataBufferManagerConfig.get() );
    rawDataBufferManager->updateConfig( updatedSignals );

    mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );

    ASSERT_TRUE( mUDSInterface->init( mEcuConfigs ) );
    ASSERT_TRUE( mUDSInterface->start() );
    mUdsModule =
        std::make_shared<RemoteDiagnosticDataSource>( mNamedSignalDataSource, rawDataBufferManager, mUDSInterface );
    ASSERT_TRUE( mUdsModule->start() );
    std::vector<InspectionValue> params;
    InspectionValue targetAddress, udsSubFunctionValue, udsStatusMaskValue, dtcString, recordNumber;
    targetAddress = static_cast<double>( -1 );
    udsSubFunctionValue = static_cast<double>( UDSSubFunction::DTC_EXT_DATA_RECORD_BY_DTC_NUMBER );
    udsStatusMaskValue = static_cast<double>( -1 );
    dtcString = static_cast<std::string>( "0xAA1122" );
    recordNumber = static_cast<double>( 1 );
    params.push_back( std::move( targetAddress ) );
    params.push_back( std::move( udsSubFunctionValue ) );
    params.push_back( std::move( udsStatusMaskValue ) );
    params.push_back( std::move( dtcString ) );
    params.push_back( std::move( recordNumber ) );

    ASSERT_EQ( mUdsModule->DTC_QUERY( 0x1234, 0x03, params ), FetchErrorCode::UNSUPPORTED_PARAMETERS );
}

} // namespace IoTFleetWise
} // namespace Aws
