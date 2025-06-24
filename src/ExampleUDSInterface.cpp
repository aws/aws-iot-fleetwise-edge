// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/ExampleUDSInterface.h"
#include "aws/iotfleetwise/IRemoteDiagnostics.h"
#include "aws/iotfleetwise/ISOTPOverCANOptions.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <algorithm>
#include <atomic>
#include <fstream> // IWYU pragma: keep
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

static std::unique_ptr<ISOTPOverCANSenderReceiver>
openCANChannelPort( const EcuConfig &ecuConfig, bool isFunctional )
{
    ISOTPOverCANSenderReceiverOptions optionsECU;
    optionsECU.mSourceCANId = isFunctional ? ecuConfig.functionalAddress : ecuConfig.physicalRequestID;
    optionsECU.mSocketCanIFName = ecuConfig.canBus;
    optionsECU.mDestinationCANId = isFunctional ? 0 : ecuConfig.physicalResponseID;
    optionsECU.mP2TimeoutMs = 10000;
    auto receiver = std::make_unique<ISOTPOverCANSenderReceiver>( optionsECU );
    if ( !receiver->connect() )
    {
        FWE_LOG_ERROR(
            "Failed to initialize the ECU with Req CAN id: " + std::to_string( ecuConfig.physicalRequestID ) +
            " Resp CAN id: " + std::to_string( ecuConfig.physicalResponseID ) );
        return nullptr;
    }

    FWE_LOG_TRACE( "Successfully initialized ECU with Req CAN id: " + std::to_string( ecuConfig.physicalRequestID ) +
                   " Resp CAN id: " + std::to_string( ecuConfig.physicalResponseID ) );
    return receiver;
}

ExampleUDSInterface::ExampleUDSInterface( std::vector<EcuConfig> ecuConfigs )
    : mEcuConfig( std::move( ecuConfigs ) )
{
}

void
ExampleUDSInterface::readDTCInfo( int32_t targetAddress,
                                  UDSSubFunction subfn,
                                  UDSStatusMask mask,
                                  UDSResponseCallback callback,
                                  const std::string &token )
{
    std::vector<uint8_t> sendPDU = { 0x19, static_cast<uint8_t>( subfn ), static_cast<uint8_t>( mask ) };
    Aws::IoTFleetWise::UdsDtcRequest udsDTCrequest;
    udsDTCrequest.targetAddress = targetAddress;
    udsDTCrequest.sendPDU = std::move( sendPDU );
    udsDTCrequest.token = token;
    udsDTCrequest.callback = std::move( callback );
    addUdsDtcRequest( udsDTCrequest );
}

void
ExampleUDSInterface::readDTCInfoByDTCAndRecordNumber( int32_t targetAddress,
                                                      UDSSubFunction subfn,
                                                      uint32_t dtc,
                                                      uint8_t recordNumber,
                                                      UDSResponseCallback callback,
                                                      const std::string &token )
{
    std::vector<uint8_t> sendPDU;
    sendPDU.push_back( 0x19 );
    sendPDU.push_back( static_cast<uint8_t>( subfn ) );
    sendPDU.push_back( static_cast<uint8_t>( ( dtc >> 16 ) & 0xFF ) );
    sendPDU.push_back( static_cast<uint8_t>( ( dtc >> 8 ) & 0xFF ) );
    sendPDU.push_back( static_cast<uint8_t>( dtc & 0xFF ) );
    sendPDU.push_back( recordNumber );

    Aws::IoTFleetWise::UdsDtcRequest udsDTCrequest;
    udsDTCrequest.targetAddress = targetAddress;
    udsDTCrequest.sendPDU = std::move( sendPDU );
    udsDTCrequest.token = token;
    udsDTCrequest.callback = std::move( callback );

    addUdsDtcRequest( udsDTCrequest );
}

bool
ExampleUDSInterface::findTargetAddress( int target, EcuConfig &out )
{
    for ( auto &config : mEcuConfig )
    {
        if ( config.targetAddress == target )
        {
            out = config;
            return true;
        }
    }
    return false;
}

static void
openConnection( const EcuConfig &ecu, std::vector<EcuConnectionInfo> &connectionInfo, bool isFunctional )
{
    if ( isFunctional )
    {
        if ( std::none_of(
                 connectionInfo.begin(), connectionInfo.end(), [&ecu]( const EcuConnectionInfo &existing ) -> bool {
                     return existing.communicationParams.physicalRequestID == ecu.physicalRequestID;
                 } ) )
        {
            auto receiver = openCANChannelPort( ecu, isFunctional );
            if ( receiver == nullptr )
            {
                FWE_LOG_ERROR( "Could not open CAN channel" );
                return;
            }
            connectionInfo.emplace_back( EcuConnectionInfo{ ecu, std::move( receiver ), {} } );
        }
    }
    else
    {
        if ( std::none_of(
                 connectionInfo.begin(), connectionInfo.end(), [&ecu]( const EcuConnectionInfo &existing ) -> bool {
                     return ( existing.communicationParams.physicalRequestID == ecu.physicalRequestID ) &&
                            ( existing.communicationParams.physicalResponseID == ecu.physicalResponseID );
                 } ) )
        {
            auto receiver = openCANChannelPort( ecu, isFunctional );
            if ( receiver == nullptr )
            {
                FWE_LOG_ERROR( "Could not open CAN channel" );
                return;
            }
            connectionInfo.emplace_back( EcuConnectionInfo{ ecu, std::move( receiver ), {} } );
        }
    }
}

bool
ExampleUDSInterface::executeRequest( std::vector<uint8_t> &sendPDU, int32_t targetAddress, DTCResponse &response )
{
    if ( shouldStop() )
    {
        return false;
    }
    if ( targetAddress == -1 )
    {
        std::vector<struct EcuConnectionInfo> openFunctionalConnection;
        std::vector<struct EcuConnectionInfo> openPhysicalConnection;
        for ( const auto &ecu : mEcuConfig )
        {
            openConnection( ecu, openFunctionalConnection, true );
            openConnection( ecu, openPhysicalConnection, false );
        }

        if ( openPhysicalConnection.empty() )
        {
            FWE_LOG_ERROR( "No CAN Connection found" );
            return false;
        }

        for ( auto &connection : openFunctionalConnection )
        {
            if ( !connection.isotpSenderReceiver->sendPDU( sendPDU ) )
            {
                FWE_LOG_ERROR( "Send PDU failed for Functional Address " +
                               std::to_string( connection.communicationParams.physicalRequestID ) );
            }
            else
            {
                FWE_LOG_TRACE( "Successfully Sent PDU for Functional Address " +
                               std::to_string( connection.communicationParams.physicalRequestID ) );
            }
            connection.isotpSenderReceiver->disconnect();
        }

        for ( auto &connection : openPhysicalConnection )
        {
            if ( connection.isotpSenderReceiver->receivePDU( connection.data ) == true )
            {
                FWE_LOG_TRACE( "Received data: " + getStringFromBytes( connection.data ) );
                UDSDTCInfo dtcInfo;
                dtcInfo.targetAddress = connection.communicationParams.targetAddress;
                for ( uint32_t i = 2; i < connection.data.size(); i++ )
                {
                    dtcInfo.dtcBuffer.push_back( connection.data[i] );
                }
                // Only send back dtc info if data was received
                if ( !dtcInfo.dtcBuffer.empty() )
                {
                    response.dtcInfo.push_back( std::move( dtcInfo ) );
                    response.result = 1;
                }
            }
            connection.isotpSenderReceiver->disconnect();
        }
        if ( response.dtcInfo.empty() )
        {
            return false;
        }
        return true;
    }
    else
    {
        EcuConfig ecu;
        if ( !findTargetAddress( targetAddress, ecu ) )
        {
            FWE_LOG_ERROR( "Unable to find address " + std::to_string( targetAddress ) );
            return false;
        }

        bool isFunctional = false;
        auto receiver = openCANChannelPort( ecu, isFunctional );
        if ( receiver == nullptr )
        {
            FWE_LOG_ERROR( "Could not open CAN channel" );
            return false;
        }

        EcuConnectionInfo phyInfo{ std::move( ecu ), std::move( receiver ), {} };

        if ( !phyInfo.isotpSenderReceiver->sendPDU( sendPDU ) )
        {
            FWE_LOG_ERROR( "Unable to send PDU" );
            return false;
        }

        if ( !phyInfo.isotpSenderReceiver->receivePDU( phyInfo.data ) )
        {
            FWE_LOG_ERROR( "Failed to receive PDU for Physical Address " +
                           std::to_string( phyInfo.communicationParams.physicalRequestID ) );
            return false;
        }

        phyInfo.isotpSenderReceiver->disconnect();
        FWE_LOG_TRACE( "Received data: " + getStringFromBytes( phyInfo.data ) );
        UDSDTCInfo dtcInfo;
        dtcInfo.targetAddress = targetAddress;
        for ( uint32_t i = 2; i < phyInfo.data.size(); i++ )
        {
            dtcInfo.dtcBuffer.push_back( phyInfo.data[i] );
        }
        // Only send back dtc info if data was received
        if ( !dtcInfo.dtcBuffer.empty() )
        {
            response.dtcInfo.push_back( std::move( dtcInfo ) );
            response.result = 1;
        }
        return true;
    }
}

void
ExampleUDSInterface::addUdsDtcRequest( const UdsDtcRequest &request )
{
    std::lock_guard<std::mutex> lock( mQueryMutex );
    mDtcRequestQueue.push( request );
    mWait.notify();
}

void
ExampleUDSInterface::doWork()
{
    while ( !shouldStop() )
    {
        mWait.wait( Signal::WaitWithPredicate );

        {
            std::lock_guard<std::mutex> lock( mQueryMutex );
            while ( ( !mDtcRequestQueue.empty() ) && ( !shouldStop() ) )
            {
                auto query = mDtcRequestQueue.front();
                DTCResponse response;
                response.token = query.token;
                if ( executeRequest( query.sendPDU, query.targetAddress, response ) )
                {
                    UdsDtcResponse responseToSend;
                    responseToSend.callback = query.callback;
                    responseToSend.response = std::move( response );
                    // Unblock request queue for incoming requests from callbacks
                    mDtcResponseQueue.push( responseToSend );
                }
                mDtcRequestQueue.pop();
            }
        }

        while ( ( !mDtcResponseQueue.empty() ) && ( !shouldStop() ) )
        {
            const auto &query = mDtcResponseQueue.front();
            query.callback( query.response );
            mDtcResponseQueue.pop();
        }
    }
}

bool
ExampleUDSInterface::start()
{
    if ( mEcuConfig.empty() )
    {
        FWE_LOG_ERROR( "ECU configuration can not be empty" );
        return false;
    }

    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( false );
    if ( !mThread.create( [this]() {
             this->doWork();
         } ) )
    {
        FWE_LOG_TRACE( "ExampleUDSInterface Module Thread failed to start" );
    }
    else
    {
        FWE_LOG_TRACE( "ExampleUDSInterface Module Thread started" );
        mThread.setThreadName( "fwDIExampleUDSInterface" );
    }

    return mThread.isActive() && mThread.isValid();
}

bool
ExampleUDSInterface::stop()
{
    if ( ( !mThread.isValid() ) || ( !mThread.isActive() ) )
    {
        return true;
    }

    std::lock_guard<std::mutex> lock( mThreadMutex );
    mShouldStop.store( true, std::memory_order_relaxed );
    FWE_LOG_TRACE( "ExampleUDSInterface  Module Thread requested to stop" );
    mWait.notify();
    mThread.release();
    mShouldStop.store( false, std::memory_order_relaxed );
    FWE_LOG_TRACE( "Thread stopped" );
    return !mThread.isActive();
}

bool
ExampleUDSInterface::shouldStop() const
{
    return mShouldStop.load( std::memory_order_relaxed );
}

bool
ExampleUDSInterface::isAlive()
{
    if ( ( !mThread.isValid() ) || ( !mThread.isActive() ) )
    {
        return false;
    }

    return true;
}

ExampleUDSInterface::~ExampleUDSInterface()
{
    // To make sure the thread stops during teardown of tests.
    if ( isAlive() )
    {
        stop();
    }
}

} // namespace IoTFleetWise
} // namespace Aws
