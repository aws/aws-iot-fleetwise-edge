// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes

#include "TraceModule.h"
#include <cstdio>
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
    if ( section < TraceSection::TRACE_SECTION_SIZE )
    {
        auto time = std::chrono::high_resolution_clock::now();
        mSectionData[toUType( section )].mLastStartTime = time;
        mSectionData[toUType( section )].mCurrentlyActive = true;
        if ( mSectionData[toUType( section )].mHitCounter > 0 )
        {
            double interval =
                std::chrono::duration<double>( ( time - mSectionData[toUType( section )].mLastEndTime ) ).count();
            mSectionData[toUType( section )].mIntervalSum += interval;
            mSectionData[toUType( section )].mMaxInterval =
                std::max( mSectionData[toUType( section )].mMaxInterval, interval );
        }
    }
}

void
TraceModule::sectionEnd( TraceSection section )
{
    if ( section < TraceSection::TRACE_SECTION_SIZE && mSectionData[toUType( section )].mCurrentlyActive )
    {
        auto time = std::chrono::high_resolution_clock::now();
        mSectionData[toUType( section )].mLastEndTime = time;
        double spent =
            std::chrono::duration<double>( ( time - mSectionData[toUType( section )].mLastStartTime ) ).count();
        mSectionData[toUType( section )].mHitCounter++;
        mSectionData[toUType( section )].mCurrentlyActive = false;
        mSectionData[toUType( section )].mTimeSpentSum += spent;
        mSectionData[toUType( section )].mMaxSpent = std::max( mSectionData[toUType( section )].mMaxSpent, spent );
    }
}

/*
 * return the name that should be as short as possible but still meaningful
 */
const char *
TraceModule::getVariableName( TraceVariable variable )
{
    switch ( variable )
    {
    case TraceVariable::READ_SOCKET_FRAMES_0:
        return "RFrames0";
    case TraceVariable::READ_SOCKET_FRAMES_1:
        return "RFrames1";
    case TraceVariable::READ_SOCKET_FRAMES_2:
        return "RFrames2";
    case TraceVariable::READ_SOCKET_FRAMES_3:
        return "RFrames3";
    case TraceVariable::READ_SOCKET_FRAMES_4:
        return "RFrames4";
    case TraceVariable::READ_SOCKET_FRAMES_5:
        return "RFrames5";
    case TraceVariable::READ_SOCKET_FRAMES_6:
        return "RFrames6";
    case TraceVariable::READ_SOCKET_FRAMES_7:
        return "RFrames7";
    case TraceVariable::READ_SOCKET_FRAMES_8:
        return "RFrames8";
    case TraceVariable::READ_SOCKET_FRAMES_9:
        return "RFrames9";
    case TraceVariable::READ_SOCKET_FRAMES_10:
        return "RFrames10";
    case TraceVariable::READ_SOCKET_FRAMES_11:
        return "RFrames11";
    case TraceVariable::READ_SOCKET_FRAMES_12:
        return "RFrames12";
    case TraceVariable::READ_SOCKET_FRAMES_13:
        return "RFrames13";
    case TraceVariable::READ_SOCKET_FRAMES_14:
        return "RFrames14";
    case TraceVariable::READ_SOCKET_FRAMES_15:
        return "RFrames15";
    case TraceVariable::READ_SOCKET_FRAMES_16:
        return "RFrames16";
    case TraceVariable::READ_SOCKET_FRAMES_17:
        return "RFrames17";
    case TraceVariable::READ_SOCKET_FRAMES_18:
        return "RFrames18";
    case TraceVariable::READ_SOCKET_FRAMES_19:
        return "RFrames19";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_0:
        return "QStC0";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_1:
        return "QStC1";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_2:
        return "QStC2";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_3:
        return "QStC3";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_4:
        return "QStC4";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_5:
        return "QStC5";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_6:
        return "QStC6";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_7:
        return "QStC7";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_8:
        return "QStC8";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_9:
        return "QStC9";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_10:
        return "QStC10";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_11:
        return "QStC11";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_12:
        return "QStC12";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_13:
        return "QStC13";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_14:
        return "QStC14";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_15:
        return "QStC15";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_16:
        return "QStC16";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_17:
        return "QStC17";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_18:
        return "QStC18";
    case TraceVariable::QUEUE_SOCKET_TO_CONSUMER_19:
        return "QStC19";
    case TraceVariable::QUEUE_INSPECTION_TO_SENDER:
        return "QStS";
    case TraceVariable::MAX_SYSTEMTIME_KERNELTIME_DIFF:
        return "SysKerTimeDiff";
    case TraceVariable::PM_MEMORY_NULL:
        return "PmE0";
    case TraceVariable::PM_MEMORY_INSUFFICIENT:
        return "PmE1";
    case TraceVariable::PM_COMPRESS_ERROR:
        return "PmE2";
    case TraceVariable::PM_STORE_ERROR:
        return "PmE3";
    case TraceVariable::CE_TOO_MANY_CONDITIONS:
        return "CeE0";
    case TraceVariable::CE_SIGNAL_ID_OUTBOUND:
        return "CeE1";
    case TraceVariable::CE_SAMPLE_SIZE_ZERO:
        return "CeE2";
    case TraceVariable::GE_COMPARE_PRECISION_ERROR:
        return "GeE0";
    case TraceVariable::GE_EVALUATE_ERROR_LAT_LON:
        return "GeE1";
    case TraceVariable::OBD_VIN_ERROR:
        return "ObdE0";
    case TraceVariable::OBD_ENG_PID_REQ_ERROR:
        return "ObdE1";
    case TraceVariable::OBD_TRA_PID_REQ_ERROR:
        return "ObdE2";
    case TraceVariable::OBD_KEEP_ALIVE_ERROR:
        return "ObdE3";
    case TraceVariable::DISCARDED_FRAMES:
        return "FrmE0";
    case TraceVariable::CAN_POLLING_TIMESTAMP_COUNTER:
        return "CanPollTCnt";
    default:
        return "UNKNOWN";
    }
}

const char *
TraceModule::getAtomicVariableName( TraceAtomicVariable variable )
{
    switch ( variable )
    {
    case TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_SIGNALS:
        return "QUEUE_CONSUMER_TO_INSPECTION_SIGNALS";
    case TraceAtomicVariable::QUEUE_CONSUMER_TO_INSPECTION_CAN:
        return "QUEUE_CONSUMER_TO_INSPECTION_CAN";
    case TraceAtomicVariable::NOT_TIME_MONOTONIC_FRAMES:
        return "nTime";
    case TraceAtomicVariable::SUBSCRIBE_ERROR:
        return "SubErr";
    case TraceAtomicVariable::SUBSCRIBE_REJECT:
        return "SubRej";
    case TraceAtomicVariable::CONNECTION_FAILED:
        return "ConFail";
    case TraceAtomicVariable::CONNECTION_REJECTED:
        return "ConRej";
    case TraceAtomicVariable::CONNECTION_INTERRUPTED:
        return "ConInt";
    case TraceAtomicVariable::CONNECTION_RESUMED:
        return "ConRes";
    default:
        return "UNKNOWN";
    }
}

const char *
TraceModule::getSectionName( TraceSection section )
{
    switch ( section )
    {
    case TraceSection::BUILD_MQTT:
        return "BUILD_MQTT";
    case TraceSection::FWE_STARTUP:
        return "FWE_STARTUP";
    case TraceSection::FWE_SHUTDOWN:
        return "FWE_SHUTDOWN";
    case TraceSection::MANAGER_DECODER_BUILD:
        return "DEC_BUILD";
    case TraceSection::MANAGER_COLLECTION_BUILD:
        return "COL_BUILD";
    case TraceSection::MANAGER_EXTRACTION:
        return "EXTRACT";
    default:
        return "UNKNOWN";
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
TraceModule::startNewObservationWindow()
{
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
        auto variableName = variableNamePtr != nullptr ? variableNamePtr : "Unknown variable name";
        profiler->setMetric( std::string( "variableMaxSinceStartup_" ) + variableName + std::string( "_id" ) +
                                 std::to_string( i ),
                             static_cast<double>( v.mMaxValue ),
                             "Count" );
        profiler->setMetric( std::string( "variableMaxSinceLast_" ) + variableName + std::string( "_id" ) +
                                 std::to_string( i ),
                             static_cast<double>( v.mMaxValueAllTime ),
                             "Count" );
    }
    for ( auto i = 0; i < toUType( TraceAtomicVariable::TRACE_ATOMIC_VARIABLE_SIZE ); i++ )
    {
        auto &v = mAtomicVariableData[i];
        auto atomicVariableNamePtr = getAtomicVariableName( static_cast<TraceAtomicVariable>( i ) );
        auto atomicVariableName =
            atomicVariableNamePtr != nullptr ? atomicVariableNamePtr : "Unknown atomic variable name";
        profiler->setMetric( std::string( "variableMaxSinceStartup_atomic_" ) + atomicVariableName +
                                 std::string( "_id" ) + std::to_string( i ),
                             static_cast<double>( v.mMaxValueAllTime ),
                             "Count" );
        profiler->setMetric( std::string( "variableMaxSinceLast_atomic_" ) + atomicVariableName + std::string( "_id" ) +
                                 std::to_string( i ),
                             static_cast<double>( v.mMaxValue ),
                             "Count" );
    }
    for ( auto i = 0; i < toUType( TraceSection::TRACE_SECTION_SIZE ); i++ )
    {
        auto &v = mSectionData[i];
        auto sectionNamePtr = getSectionName( static_cast<TraceSection>( i ) );
        auto sectionName = sectionNamePtr != nullptr ? sectionNamePtr : "Unknown section name";
        profiler->setMetric( std::string( "sectionAvgSinceStartup_" ) + sectionName + std::string( "_id" ) +
                                 std::to_string( i ),
                             ( v.mHitCounter == 0 ? 0 : v.mTimeSpentSum / v.mHitCounter ),
                             "Seconds" );
        profiler->setMetric( std::string( "sectionMaxSinceStartup_" ) + sectionName + std::string( "_id" ) +
                                 std::to_string( i ),
                             v.mMaxSpentAllTime,
                             "Seconds" );
        profiler->setMetric( std::string( "sectionMaxSinceLast_" ) + sectionName + std::string( "_id" ) +
                                 std::to_string( i ),
                             v.mMaxSpent,
                             "Seconds" );
        profiler->setMetric( std::string( "sectionCountSinceStartup_" ) + sectionName + std::string( "_id" ) +
                                 std::to_string( i ),
                             v.mHitCounter,
                             "Seconds" );
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
        auto variableName = variableNamePtr != nullptr ? variableNamePtr : "Unknown variable name";
        mLogger.trace( "TraceModule::print",
                       std::string{ " TraceModule-ConsoleLogging-Variable '" } + variableName + "' [" +
                           std::to_string( i ) + "] current value: [" + std::to_string( v.mCurrentValue ) +
                           "] max value since last print: "
                           "[" +
                           std::to_string( v.mMaxValue ) + "] overall max value: [" +
                           std::to_string( v.mMaxValueAllTime ) + "]" );
    }
    for ( auto i = 0; i < toUType( TraceAtomicVariable::TRACE_ATOMIC_VARIABLE_SIZE ); i++ )
    {
        auto &v = mAtomicVariableData[i];
        auto atomicVariableNamePtr = getAtomicVariableName( static_cast<TraceAtomicVariable>( i ) );
        auto atomicVariableName =
            atomicVariableNamePtr != nullptr ? atomicVariableNamePtr : "Unknown atomic variable name";
        mLogger.trace( "TraceModule::print",
                       std::string{ " TraceModule-ConsoleLogging-TraceAtomicVariable '" } + atomicVariableName + "' [" +
                           std::to_string( i ) + "] current value: [" + std::to_string( v.mCurrentValue.load() ) +
                           "] max value since "
                           "last print: [" +
                           std::to_string( v.mMaxValue ) + "] overall max value: [" +
                           std::to_string( v.mMaxValueAllTime ) + "]" );
    }
    for ( auto i = 0; i < toUType( TraceSection::TRACE_SECTION_SIZE ); i++ )
    {
        auto &v = mSectionData[i];
        auto currentHitCounter = v.mHitCounter - ( v.mCurrentlyActive ? 0 : 1 );
        auto sectionNamePtr = getSectionName( static_cast<TraceSection>( i ) );
        auto sectionName = sectionNamePtr != nullptr ? sectionNamePtr : "Unknown section name";
        mLogger.trace( "TraceModule::print",
                       std::string{ " TraceModule-ConsoleLogging-Section '" } + sectionName + "' [" +
                           std::to_string( i ) + "] times section executed: [" + std::to_string( v.mHitCounter ) +
                           "] avg execution time: "
                           "[" +
                           std::to_string( ( v.mHitCounter == 0 ? 0 : v.mTimeSpentSum / v.mHitCounter ) ) +
                           "] max execution time since last print: [" + std::to_string( v.mMaxSpent ) + "] overall: [" +
                           std::to_string( v.mMaxSpentAllTime ) +
                           "] avg interval between execution: "
                           "[" +
                           std::to_string( ( currentHitCounter == 0 ? 0 : v.mIntervalSum / currentHitCounter ) ) +
                           "] max interval since last print: [" + std::to_string( v.mMaxInterval ) + "] overall: [" +
                           std::to_string( v.mMaxIntervalAllTime ) + "]" );
    }

    std::fflush( stdout );
}

} // namespace Linux
} // namespace Platform
} // namespace IoTFleetWise
} // namespace Aws