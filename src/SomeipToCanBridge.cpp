// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "SomeipToCanBridge.h"
#include "EnumUtility.h"
#include "LoggingModule.h"
#include "Thread.h"
#include "TraceModule.h"
#include <endian.h>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

SomeipToCanBridge::SomeipToCanBridge(
    uint16_t someipServiceId,
    uint16_t someipInstanceId,
    uint16_t someipEventId,
    uint16_t someipEventGroupId,
    std::string someipApplicationName,
    CANChannelNumericID canChannelId,
    CANDataConsumer &canDataConsumer,
    std::function<std::shared_ptr<vsomeip::application>( std::string )> createSomeipApplication,
    std::function<void( std::string )> removeSomeipApplication )
    : mSomeipServiceId( someipServiceId )
    , mSomeipInstanceId( someipInstanceId )
    , mSomeipEventId( someipEventId )
    , mSomeipEventGroupId( someipEventGroupId )
    , mSomeipApplicationName( std::move( someipApplicationName ) )
    , mCanChannelId( canChannelId )
    , mCanDataConsumer( canDataConsumer )
    , mCreateSomeipApplication( std::move( createSomeipApplication ) )
    , mRemoveSomeipApplication( std::move( removeSomeipApplication ) )
{
}

bool
SomeipToCanBridge::init()
{
    mSomeipApplication = mCreateSomeipApplication( mSomeipApplicationName );
    if ( !mSomeipApplication->init() )
    {
        FWE_LOG_ERROR( "Couldn't initialize the someip application" );
        mRemoveSomeipApplication( mSomeipApplicationName );
        return false;
    }
    mSomeipApplication->register_sd_acceptance_handler( []( const vsomeip::remote_info_t &remoteInfo ) -> bool {
        FWE_LOG_INFO(
            "Accepting service discovery requests: first: " + std::to_string( remoteInfo.first_ ) + " last: " +
            std::to_string( remoteInfo.last_ ) + " ip: " + std::to_string( remoteInfo.ip_.address_.v4_[0] ) + "." +
            std::to_string( remoteInfo.ip_.address_.v4_[1] ) + "." + std::to_string( remoteInfo.ip_.address_.v4_[2] ) +
            "." + std::to_string( remoteInfo.ip_.address_.v4_[3] ) + " is_range: " +
            std::to_string( remoteInfo.is_range_ ) + " is_reliable: " + std::to_string( remoteInfo.is_reliable_ ) );
        return true;
    } );
    mSomeipApplication->register_availability_handler(
        mSomeipServiceId,
        mSomeipInstanceId,
        []( vsomeip::service_t service, vsomeip::instance_t instance, bool isAvailable ) {
            std::stringstream stream;
            stream << "Service [" << std::setw( 4 ) << std::setfill( '0' ) << std::hex << service << "." << instance
                   << "] is " << ( isAvailable ? "available" : "NOT available" );
            FWE_LOG_INFO( stream.str() );
        } );
    mSomeipApplication->request_service( mSomeipServiceId, mSomeipInstanceId );
    mSomeipApplication->register_message_handler(
        mSomeipServiceId,
        mSomeipInstanceId,
        mSomeipEventId,
        [this]( const std::shared_ptr<vsomeip::message> &response ) {
            auto payload = response->get_payload();
            if ( payload->get_length() < HEADER_SIZE )
            {
                FWE_LOG_ERROR( "Someip event message is too short" );
                return;
            }
            uint32_t canMessageId = be32toh( reinterpret_cast<uint32_t *>( payload->get_data() )[0] );
            Timestamp timestamp = be64toh( reinterpret_cast<uint64_t *>( payload->get_data() + 4 )[0] );
            if ( timestamp == 0 )
            {
                TraceModule::get().incrementVariable( TraceVariable::POLLING_TIMESTAMP_COUNTER );
                timestamp = mClock->systemTimeSinceEpochMs();
            }
            else
            {
                timestamp /= 1000;
            }
            if ( timestamp < mLastFrameTime )
            {
                TraceModule::get().incrementAtomicVariable( TraceAtomicVariable::NOT_TIME_MONOTONIC_FRAMES );
            }
            mLastFrameTime = timestamp;
            unsigned traceFrames = mCanChannelId + toUType( TraceVariable::READ_SOCKET_FRAMES_0 );
            TraceModule::get().incrementVariable(
                ( traceFrames < static_cast<unsigned>( toUType( TraceVariable::READ_SOCKET_FRAMES_19 ) ) )
                    ? static_cast<TraceVariable>( traceFrames )
                    : TraceVariable::READ_SOCKET_FRAMES_19 );
            std::lock_guard<std::mutex> lock( mDecoderDictMutex );
            mCanDataConsumer.processMessage( mCanChannelId,
                                             mDecoderDictionary,
                                             canMessageId,
                                             payload->get_data() + HEADER_SIZE,
                                             payload->get_length() - HEADER_SIZE,
                                             timestamp );
        } );
    std::set<vsomeip::eventgroup_t> eventGroupSet;
    eventGroupSet.insert( mSomeipEventGroupId );
    mSomeipApplication->request_event( mSomeipServiceId, mSomeipInstanceId, mSomeipEventId, eventGroupSet );
    mSomeipApplication->subscribe( mSomeipServiceId, mSomeipInstanceId, mSomeipEventGroupId );
    mSomeipThread = std::thread( [this]() {
        Thread::setCurrentThreadName( "SomeipBridge" + std::to_string( mCanChannelId ) );
        mSomeipApplication->start();
    } );
    return true;
}

void
SomeipToCanBridge::disconnect()
{
    if ( mSomeipThread.joinable() )
    {
        mSomeipApplication->stop();
        mSomeipThread.join();
        mRemoveSomeipApplication( mSomeipApplicationName );
    }
}

void
SomeipToCanBridge::onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                               VehicleDataSourceProtocol networkProtocol )
{
    if ( networkProtocol != VehicleDataSourceProtocol::RAW_SOCKET )
    {
        return;
    }
    std::lock_guard<std::mutex> lock( mDecoderDictMutex );
    mDecoderDictionary = std::dynamic_pointer_cast<const CANDecoderDictionary>( dictionary );
    if ( dictionary == nullptr )
    {
        FWE_LOG_TRACE( "Decoder dictionary removed" );
    }
    else
    {
        FWE_LOG_TRACE( "Decoder dictionary updated" );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
