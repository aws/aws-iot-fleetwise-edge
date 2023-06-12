// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes

#include "TraceModule.h"
#include "LoggingModule.h"
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace Platform
{
namespace Linux
{

void
TraceModule::sectionBegin( TraceSection section )
{
    auto index = toUType( section );
    if ( ( section < TraceSection::TRACE_SECTION_SIZE ) && ( index >= 0 ) )
    {
        auto time = std::chrono::high_resolution_clock::now();
        mSectionData[index].mLastStartTime = time;
        mSectionData[index].mCurrentlyActive = true;
        if ( mSectionData[index].mHitCounter > 0 )
        {
            double interval = std::chrono::duration<double>( ( time - mSectionData[index].mLastEndTime ) ).count();
            mSectionData[index].mIntervalSum += interval;
            mSectionData[index].mMaxInterval = std::max( mSectionData[index].mMaxInterval, interval );
        }
    }
}

void
TraceModule::sectionEnd( TraceSection section )
{
    auto index = toUType( section );
    if ( ( section < TraceSection::TRACE_SECTION_SIZE ) && ( index >= 0 ) && mSectionData[index].mCurrentlyActive )
    {
        auto time = std::chrono::high_resolution_clock::now();
        mSectionData[index].mLastEndTime = time;
        double spent = std::chrono::duration<double>( ( time - mSectionData[index].mLastStartTime ) ).count();
        mSectionData[index].mHitCounter++;
        mSectionData[index].mCurrentlyActive = false;
        mSectionData[index].mTimeSpentSum += spent;
        mSectionData[index].mMaxSpent = std::max( mSectionData[index].mMaxSpent, spent );
    }
}

/*
 * return the name that should be as short as possible but still meaningful
 * Used in metrics.md as reference
 */
const char *
TraceModule::getVariableName( TraceVariable variable )
{
    switch ( variable )
    {
    // The _idX suffix is to match the legacy naming, which should not be changed as they are used
    // in dashbaords.
    case TraceVariable::READ_SOCKET_FRAMES_0:
        return "RFrames0_id0";
    case TraceVariable::READ_SOCKET_FRAMES_1:
        return "RFrames1_id1";
    case TraceVariable::READ_SOCKET_FRAMES_2:
        return "RFrames2_id2";
    case TraceVariable::READ_SOCKET_FRAMES_3:
        return "RFrames3_id3";
    case TraceVariable::READ_SOCKET_FRAMES_4:
        return "RFrames4_id4";
    case TraceVariable::READ_SOCKET_FRAMES_5:
        return "RFrames5_id5";
    case TraceVariable::READ_SOCKET_FRAMES_6:
        return "RFrames6_id6";
    case TraceVariable::READ_SOCKET_FRAMES_7:
        return "RFrames7_id7";
    case TraceVariable::READ_SOCKET_FRAMES_8:
        return "RFrames8_id8";
    case TraceVariable::READ_SOCKET_FRAMES_9:
        return "RFrames9_id9";
    case TraceVariable::READ_SOCKET_FRAMES_10:
        return "RFrames10_id10";
    case TraceVariable::READ_SOCKET_FRAMES_11:
        return "RFrames11_id11";
    case TraceVariable::READ_SOCKET_FRAMES_12:
        return "RFrames12_id12";
    case TraceVariable::READ_SOCKET_FRAMES_13:
        return "RFrames13_id13";
    case TraceVariable::READ_SOCKET_FRAMES_14:
        return "RFrames14_id14";
    case TraceVariable::READ_SOCKET_FRAMES_15:
        return "RFrames15_id15";
    case TraceVariable::READ_SOCKET_FRAMES_16:
        return "RFrames16_id16";
    case TraceVariable::READ_SOCKET_FRAMES_17:
        return "RFrames17_id17";
    case TraceVariable::READ_SOCKET_FRAMES_18:
        return "RFrames18_id18";
    case TraceVariable::READ_SOCKET_FRAMES_19:
        return "RFrames19_id19";
    case TraceVariable::QUEUE_INSPECTION_TO_SENDER:
        return "QStS_id40";
    case TraceVariable::MAX_SYSTEMTIME_KERNELTIME_DIFF:
        return "SysKerTimeDiff_id41";
    case TraceVariable::PM_MEMORY_NULL:
        return "PmE0_id42";
    case TraceVariable::PM_MEMORY_INSUFFICIENT:
        return "PmE1_id43";
    case TraceVariable::PM_COMPRESS_ERROR:
        return "PmE2_id44";
    case TraceVariable::PM_STORE_ERROR:
        return "PmE3_id45";
    case TraceVariable::CE_TOO_MANY_CONDITIONS:
        return "CeE0_id46";
    case TraceVariable::CE_SIGNAL_ID_OUTBOUND:
        return "CeE1_id47";
    case TraceVariable::CE_SAMPLE_SIZE_ZERO:
        return "CeE2_id48";
    case TraceVariable::GE_COMPARE_PRECISION_ERROR:
        return "GeE0_id49";
    case TraceVariable::GE_EVALUATE_ERROR_LAT_LON:
        return "GeE1_id50";
    case TraceVariable::OBD_VIN_ERROR:
        return "ObdE0_id51";
    case TraceVariable::OBD_ENG_PID_REQ_ERROR:
        return "ObdE1_id52";
    case TraceVariable::OBD_TRA_PID_REQ_ERROR:
        return "ObdE2_id53";
    case TraceVariable::OBD_KEEP_ALIVE_ERROR:
        return "ObdE3_id54";
    case TraceVariable::DISCARDED_FRAMES:
        return "FrmE0_id55";
    case TraceVariable::CAN_POLLING_TIMESTAMP_COUNTER:
        return "CanPollTCnt_id56";
    case TraceVariable::CE_PROCESSED_SIGNALS:
        return "CeSCnt_id57";
    case TraceVariable::CE_PROCESSED_CAN_FRAMES:
        return "CeCCnt_id58";
    case TraceVariable::CE_TRIGGERS:
        return "CeTrgCnt_id59";
    case TraceVariable::OBD_POSSIBLE_PRECISION_LOSS_UINT64:
        return "ObdPrecU64_id60";
    case TraceVariable::OBD_POSSIBLE_PRECISION_LOSS_INT64:
        return "ObdPrecI64_id61";
    case TraceVariable::MQTT_SIGNAL_MESSAGES_SENT_OUT: // Can be multiple messages per event id
        return "MqttSignalMessages";
    case TraceVariable::MQTT_HEAP_USAGE:
        return "MqttHeapSize";
    case TraceVariable::SIGNAL_BUFFER_SIZE:
        return "SigBufSize";
    default:
        return nullptr;
    }
}

const char *
TraceModule::getAtomicVariableName( TraceAtomicVariable variable )
{
    switch ( variable )
    {
    // The _idX suffix is to match the legacy naming, which should not be changed as they are used
    // in dashbaords.
    case TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS:
        return "QUEUE_CONSUMER_TO_INSPECTION_SIGNALS_id0";
    case TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_CAN:
        return "QUEUE_CONSUMER_TO_INSPECTION_CAN_id1";
    case TraceAtomicVariable::NOT_TIME_MONOTONIC_FRAMES:
        return "nTime_id2";
    case TraceAtomicVariable::SUBSCRIBE_ERROR:
        return "SubErr_id3";
    case TraceAtomicVariable::SUBSCRIBE_REJECT:
        return "SubRej_id4";
    case TraceAtomicVariable::CONNECTION_FAILED:
        return "ConFail_id5";
    case TraceAtomicVariable::CONNECTION_REJECTED:
        return "ConRej_id6";
    case TraceAtomicVariable::CONNECTION_INTERRUPTED:
        return "ConInt_id7";
    case TraceAtomicVariable::CONNECTION_RESUMED:
        return "ConRes_id8";
    case TraceAtomicVariable::COLLECTION_SCHEME_ERROR:
        return "CampaignFailures";
    default:
        return nullptr;
    }
}

const char *
TraceModule::getSectionName( TraceSection section )
{
    switch ( section )
    {
    // The _idX suffix is to match the legacy naming, which should not be changed as they are used
    // in dashbaords.
    case TraceSection::BUILD_MQTT:
        return "BUILD_MQTT_id0";
    case TraceSection::FWE_STARTUP:
        return "FWE_STARTUP_id1";
    case TraceSection::FWE_SHUTDOWN:
        return "FWE_SHUTDOWN_id2";
    case TraceSection::MANAGER_DECODER_BUILD:
        return "DEC_BUILD_id3";
    case TraceSection::MANAGER_COLLECTION_BUILD:
        return "COL_BUILD_id4";
    case TraceSection::MANAGER_EXTRACTION:
        return "EXTRACT_id5";
    case TraceSection::CAN_DECODER_CYCLE_0:
        return "CD_0_id6";
    case TraceSection::CAN_DECODER_CYCLE_1:
        return "CD_1_id7";
    case TraceSection::CAN_DECODER_CYCLE_2:
        return "CD_2_id8";
    case TraceSection::CAN_DECODER_CYCLE_3:
        return "CD_3_id9";
    case TraceSection::CAN_DECODER_CYCLE_4:
        return "CD_4_id10";
    case TraceSection::CAN_DECODER_CYCLE_5:
        return "CD_5_id11";
    case TraceSection::CAN_DECODER_CYCLE_6:
        return "CD_6_id12";
    case TraceSection::CAN_DECODER_CYCLE_7:
        return "CD_7_id13";
    case TraceSection::CAN_DECODER_CYCLE_8:
        return "CD_8_id14";
    case TraceSection::CAN_DECODER_CYCLE_9:
        return "CD_9_id15";
    case TraceSection::CAN_DECODER_CYCLE_10:
        return "CD_10_id16";
    case TraceSection::CAN_DECODER_CYCLE_11:
        return "CD_11_id17";
    case TraceSection::CAN_DECODER_CYCLE_12:
        return "CD_12_id18";
    case TraceSection::CAN_DECODER_CYCLE_13:
        return "CD_13_id19";
    case TraceSection::CAN_DECODER_CYCLE_14:
        return "CD_14_id20";
    case TraceSection::CAN_DECODER_CYCLE_15:
        return "CD_15_id21";
    case TraceSection::CAN_DECODER_CYCLE_16:
        return "CD_16_id22";
    case TraceSection::CAN_DECODER_CYCLE_17:
        return "CD_17_id23";
    case TraceSection::CAN_DECODER_CYCLE_18:
        return "CD_18_id24";
    case TraceSection::CAN_DECODER_CYCLE_19:
        return "CD_19_id25";
    case TraceSection::COLLECTION_SCHEME_CHANGE_TO_FIRST_DATA:
        return "CampaignRxToDataTx";
    default:
        return nullptr;
    }
}

void
TraceModule::updateAllTimeData()
{
    for ( auto i = 0; i < toUType( TraceVariable::TRACE_VARIABLE_SIZE ); i++ )
    {
        auto &v = mVariableData[i];
        v.mMaxValueAllTime = std::max( v.mMaxValueAllTime, v.mMaxValue );
    }
    for ( auto i = 0; i < toUType( TraceAtomicVariable::TRACE_ATOMIC_VARIABLE_SIZE ); i++ )
    {
        auto &v = mAtomicVariableData[i];
        v.mMaxValueAllTime = std::max( v.mMaxValueAllTime, v.mMaxValue );
    }
    for ( auto i = 0; i < toUType( TraceSection::TRACE_SECTION_SIZE ); i++ )
    {
        auto &v = mSectionData[i];
        v.mMaxSpentAllTime = std::max( v.mMaxSpentAllTime, v.mMaxSpent );
        v.mMaxIntervalAllTime = std::max( v.mMaxIntervalAllTime, v.mMaxInterval );
    }
}

void
TraceModule::startNewObservationWindow( uint32_t minimumObservationWindowTime )
{
    if ( mTimeSinceLastObservationWindow.getElapsedMs().count() < minimumObservationWindowTime )
    {
        return;
    }
    mTimeSinceLastObservationWindow.reset();
    updateAllTimeData();
    for ( auto i = 0; i < toUType( TraceVariable::TRACE_VARIABLE_SIZE ); i++ )
    {
        auto &v = mVariableData[i];
        v.mMaxValue = 0;
    }
    for ( auto i = 0; i < toUType( TraceAtomicVariable::TRACE_ATOMIC_VARIABLE_SIZE ); i++ )
    {
        auto &v = mAtomicVariableData[i];
        v.mMaxValue = 0;
    }
    for ( auto i = 0; i < toUType( TraceSection::TRACE_SECTION_SIZE ); i++ )
    {
        auto &v = mSectionData[i];
        v.mMaxSpent = 0;
        v.mMaxInterval = 0;
    }
}

void
TraceModule::forwardAllMetricsToMetricsReceiver( IMetricsReceiver *profiler )
{
    if ( profiler == nullptr )
    {
        return;
    }

    updateAllTimeData();
    for ( auto i = 0; i < toUType( TraceVariable::TRACE_VARIABLE_SIZE ); i++ )
    {
        auto &v = mVariableData[i];
        auto variableNamePtr = getVariableName( static_cast<TraceVariable>( i ) );
        auto variableName =
            variableNamePtr != nullptr ? variableNamePtr : std::string( "UnknownVariable_id" ) + std::to_string( i );
        profiler->setMetric(
            std::string( "variableMaxSinceLast_" ) + variableName, static_cast<double>( v.mMaxValue ), "Count" );
        profiler->setMetric( std::string( "variableMaxSinceStartup_" ) + variableName,
                             static_cast<double>( v.mMaxValueAllTime ),
                             "Count" );
    }
    for ( auto i = 0; i < toUType( TraceAtomicVariable::TRACE_ATOMIC_VARIABLE_SIZE ); i++ )
    {
        auto &v = mAtomicVariableData[i];
        auto atomicVariableNamePtr = getAtomicVariableName( static_cast<TraceAtomicVariable>( i ) );
        auto atomicVariableName = atomicVariableNamePtr != nullptr
                                      ? atomicVariableNamePtr
                                      : std::string( "UnknownVariable_id" ) + std::to_string( i );
        profiler->setMetric( std::string( "variableMaxSinceStartup_atomic_" ) + atomicVariableName,
                             static_cast<double>( v.mMaxValueAllTime ),
                             "Count" );
        profiler->setMetric( std::string( "variableMaxSinceLast_atomic_" ) + atomicVariableName,
                             static_cast<double>( v.mMaxValue ),
                             "Count" );
    }
    for ( auto i = 0; i < toUType( TraceSection::TRACE_SECTION_SIZE ); i++ )
    {
        auto &v = mSectionData[i];
        auto sectionNamePtr = getSectionName( static_cast<TraceSection>( i ) );
        auto sectionName =
            sectionNamePtr != nullptr ? sectionNamePtr : std::string( "UnknownSection_id" ) + std::to_string( i );
        profiler->setMetric( std::string( "sectionAvgSinceStartup_" ) + sectionName,
                             ( v.mHitCounter == 0 ? 0 : v.mTimeSpentSum / v.mHitCounter ),
                             "Seconds" );
        profiler->setMetric( std::string( "sectionMaxSinceStartup_" ) + sectionName, v.mMaxSpentAllTime, "Seconds" );
        profiler->setMetric( std::string( "sectionMaxSinceLast_" ) + sectionName, v.mMaxSpent, "Seconds" );
        profiler->setMetric( std::string( "sectionCountSinceStartup_" ) + sectionName, v.mHitCounter, "Seconds" );
    }
}

void
TraceModule::print()
{
    updateAllTimeData();
    for ( auto i = 0; i < toUType( TraceVariable::TRACE_VARIABLE_SIZE ); i++ )
    {
        auto &v = mVariableData[i];
        auto variableNamePtr = getVariableName( static_cast<TraceVariable>( i ) );
        auto variableName =
            variableNamePtr != nullptr ? variableNamePtr : std::string( "UnknownVariable_id" ) + std::to_string( i );
        FWE_LOG_TRACE( std::string{ " TraceModule-ConsoleLogging-Variable '" } + variableName + "' [" +
                       std::to_string( i ) + "] current value: [" + std::to_string( v.mCurrentValue ) +
                       "] max value since last print: [" + std::to_string( v.mMaxValue ) + "] overall max value: [" +
                       std::to_string( v.mMaxValueAllTime ) + "]" );
    }
    for ( auto i = 0; i < toUType( TraceAtomicVariable::TRACE_ATOMIC_VARIABLE_SIZE ); i++ )
    {
        auto &v = mAtomicVariableData[i];
        auto atomicVariableNamePtr = getAtomicVariableName( static_cast<TraceAtomicVariable>( i ) );
        auto atomicVariableName = atomicVariableNamePtr != nullptr
                                      ? atomicVariableNamePtr
                                      : std::string( "UnknownVariable_id" ) + std::to_string( i );
        FWE_LOG_TRACE( std::string{ " TraceModule-ConsoleLogging-TraceAtomicVariable '" } + atomicVariableName + "' [" +
                       std::to_string( i ) + "] current value: [" + std::to_string( v.mCurrentValue.load() ) +
                       "] max value since last print: [" + std::to_string( v.mMaxValue ) + "] overall max value: [" +
                       std::to_string( v.mMaxValueAllTime ) + "]" );
    }
    for ( auto i = 0; i < toUType( TraceSection::TRACE_SECTION_SIZE ); i++ )
    {
        auto &v = mSectionData[i];
        auto currentHitCounter = v.mHitCounter - ( v.mCurrentlyActive ? 0 : 1 );
        auto sectionNamePtr = getSectionName( static_cast<TraceSection>( i ) );
        auto sectionName =
            sectionNamePtr != nullptr ? sectionNamePtr : std::string( "UnknownSection_id" ) + std::to_string( i );
        FWE_LOG_TRACE( std::string{ " TraceModule-ConsoleLogging-Section '" } + sectionName + "' [" +
                       std::to_string( i ) + "] times section executed: [" + std::to_string( v.mHitCounter ) +
                       "] avg execution time: [" +
                       std::to_string( ( v.mHitCounter == 0 ? 0 : v.mTimeSpentSum / v.mHitCounter ) ) +
                       "] max execution time since last print: [" + std::to_string( v.mMaxSpent ) + "] overall: [" +
                       std::to_string( v.mMaxSpentAllTime ) + "] avg interval between execution: [" +
                       std::to_string( ( currentHitCounter == 0 ? 0 : v.mIntervalSum / currentHitCounter ) ) +
                       "] max interval since last print: [" + std::to_string( v.mMaxInterval ) + "] overall: [" +
                       std::to_string( v.mMaxIntervalAllTime ) + "]" );
    }
}

} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws
