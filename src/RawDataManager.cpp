// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RawDataManager.h"
#include "Assert.h"
#include "LoggingModule.h"
#include "TraceModule.h"
#include <algorithm>
#include <numeric>
#include <set>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace RawData
{

BufferManager::BufferManager( const BufferManagerConfig &config )
    : mConfig( config )
    , mMaxOverallMemory( config.getMaxBytes() )
{
}

static std::string
storageStrategyType( const StorageStrategy &storageStrategy )
{
    switch ( storageStrategy )
    {
    case StorageStrategy::COPY_ON_INGEST_ASYNC:
        return "COPY ON INGEST ASYNC";
    case StorageStrategy::COPY_ON_INGEST_SYNC:
        return "COPY ON INGEST SYNC";
    case StorageStrategy::ZERO_COPY:
        return "ZERO COPY";
    case StorageStrategy::COMPRESS_ON_INGEST_ASYNC:
        return "COMPRESS ON INGEST ASYNC";
    case StorageStrategy::COMPRESS_ON_INGEST_SYNC:
        return "COMPRESS ON INGEST SYNC";
    case StorageStrategy::STORE_TO_FILE_ASYNC:
        return "STORE TO FILE ASYNC";
    case StorageStrategy::STORE_TO_FILE_SYNC:
        return "STORE TO FILE SYNC";
    default:
        return "NOT IMPLEMENTED";
    }
}

// coverity[autosar_cpp14_a12_8_1_violation] we need to modify other to prevent the raw data to be returned twice
LoanedFrame::LoanedFrame( LoanedFrame &&other ) noexcept
{
    *this = std::move( other );
}

LoanedFrame &
LoanedFrame::operator=( LoanedFrame &&other ) noexcept
{
    // We need to modify other to prevent the raw data to be returned twice
    // coverity[autosar_cpp14_a18_9_2_violation]
    // coverity[autosar_cpp14_a8_4_5_violation]
    swap( *this, other );

    return *this;
}

void
LoanedFrame::swap( LoanedFrame &lhs, LoanedFrame &rhs ) noexcept
{
    std::swap( lhs.mRawBufferManager, rhs.mRawBufferManager );
    std::swap( lhs.mTypeId, rhs.mTypeId );
    std::swap( lhs.mHandle, rhs.mHandle );
    std::swap( lhs.mData, rhs.mData );
    std::swap( lhs.mSize, rhs.mSize );
}

LoanedFrame::~LoanedFrame()
{
    if ( mRawBufferManager != nullptr )
    {
        mRawBufferManager->returnLoanedFrame( mTypeId, mHandle );
    }
}

BufferErrorCode
BufferManager::updateConfig( const std::unordered_map<BufferTypeId, SignalUpdateConfig> &updatedSignals )
{
    std::lock_guard<std::mutex> lock( mBufferManagerMutex );

    // Delete unused types from mTypeIDToBufferMap
    std::set<BufferTypeId> excludedTypesIds;
    for ( auto const &typeIDAndBuffer : mTypeIDToBufferMap )
    {
        if ( updatedSignals.find( typeIDAndBuffer.first ) == updatedSignals.end() )
        {
            excludedTypesIds.insert( typeIDAndBuffer.first );
        }
    }

    for ( auto excludedTypeId : excludedTypesIds )
    {
        auto &buffer = mTypeIDToBufferMap[excludedTypeId];
        deleteBufferFromStats( buffer );
        while ( buffer.deleteUnusedData() )
        {
            // Do nothing, just keep deleting it.
        }
        addBufferToStats( buffer );

        if ( buffer.mNumOfSamplesCurrentlyInMemory == 0 )
        {
            // delete type from map
            FWE_LOG_TRACE( "Deleting buffer for Signal ID " + std::to_string( excludedTypeId ) );
            deleteBufferFromStats( buffer );
            mTypeIDToBufferMap.erase( buffer.mTypeID );
        }
        else
        {
            // at least one frame of type still used, delete it later
            buffer.mDeleting = true;
        }
    }

    // Add/update signal config
    // Use config to set all the parameters for signals in the raw data buffer manager
    for ( const auto &updatedSignal : updatedSignals )
    {
        auto signalConfig = mConfig.getSignalConfig(
            updatedSignal.second.typeId, updatedSignal.second.interfaceId, updatedSignal.second.messageId );
        FWE_LOG_TRACE( "Adding Signal ID " + std::to_string( signalConfig.typeId ) +
                       " for raw data collection with Max Samples " + std::to_string( signalConfig.maxNumOfSamples ) +
                       ", Minimum Memory Allocation " + std::to_string( signalConfig.reservedBytes ) +
                       ", Maximum Memory Allocation " + std::to_string( signalConfig.maxOverallBytes ) +
                       ", Max Size Per Sample " + std::to_string( signalConfig.maxBytesPerSample ) +
                       " and Storage Strategy as " + storageStrategyType( signalConfig.storageStrategy ) );
        BufferErrorCode errorCode = addRawDataToBuffer( signalConfig );
        if ( errorCode != BufferErrorCode::SUCCESSFUL )
        {
            FWE_LOG_ERROR( "Failed to update the Raw Buffer Config" );
            return errorCode;
        }
    }

    FWE_LOG_TRACE( "Successfully updated the Raw Buffer Config" );
    return BufferErrorCode::SUCCESSFUL;
}

BufferHandle
BufferManager::push( uint8_t *data, size_t size, Timestamp receiveTimestamp, BufferTypeId typeId )
{
    std::lock_guard<std::mutex> lock( mBufferManagerMutex );

    auto bufferIterator = mTypeIDToBufferMap.find( typeId );
    if ( bufferIterator == mTypeIDToBufferMap.end() )
    {
        FWE_LOG_ERROR( "Signal ID " + std::to_string( typeId ) + " push requested but is not assigned for collection" );
        return INVALID_BUFFER_HANDLE;
    }
    auto &buffer = bufferIterator->second;

    if ( buffer.mDeleting )
    {
        FWE_LOG_ERROR( "Signal ID " + std::to_string( typeId ) +
                       " push requested but it is buffer is being deleted because it is not assigned for collection" );
        return INVALID_BUFFER_HANDLE;
    }

    if ( buffer.mStorageStrategy != StorageStrategy::COPY_ON_INGEST_SYNC )
    {
        FWE_LOG_ERROR( "Currently only COPY_ON_INGEST_SYNC is supported" );
        return INVALID_BUFFER_HANDLE;
    }
    mOverallNumOfSamplesReceived++;
    // Generate the Handle ID
    auto rawDataBufferHandle = generateHandleID( receiveTimestamp );

    auto availableFreeMemory = mMaxOverallMemory - mBytesInUseAndReserved;

    // Since some data could be deleted, we don't know how much to add to the stats variables.
    // So we subtract the current values related to the individual buffer first and then add the
    // values again after the operation.
    deleteBufferFromStats( buffer );
    bool successfullyAddedData =
        buffer.addData( data, size, receiveTimestamp, rawDataBufferHandle, availableFreeMemory );
    addBufferToStats( buffer );

    TraceModule::get().setVariable( TraceVariable::RAW_DATA_BUFFER_MANAGER_BYTES, mBytesInUse );
    if ( !successfullyAddedData )
    {
        FWE_LOG_ERROR( "Failed to store the Raw Data for Signal ID " + std::to_string( typeId ) );
        return INVALID_BUFFER_HANDLE;
    }

    return rawDataBufferHandle;
}

LoanedFrame
BufferManager::borrowFrame( BufferTypeId typeId, BufferHandle handle )
{
    std::lock_guard<std::mutex> lock( mBufferManagerMutex );

    auto result = findBufferAndFrame( typeId, handle );
    Buffer *rawDataBuffer = result.first;
    Frame *rawDataFrame = result.second;

    if ( ( rawDataBuffer == nullptr ) || ( rawDataFrame == nullptr ) )
    {
        return {};
    }

    const size_t dataSize = rawDataFrame->getSize();
    if ( dataSize == 0 )
    {
        FWE_LOG_ERROR( "Size of requested data is 0. That probably means that the data was moved or deleted "
                       "but the reference we found is outdated. Signal ID " +
                       std::to_string( typeId ) + " BufferHandle " + std::to_string( handle ) );

        return {};
    }

    if ( rawDataFrame->mDataInUseCounter == UINT8_MAX )
    {
        FWE_LOG_ERROR( "Too many references of data for Signal ID " + std::to_string( typeId ) + " BufferHandle " +
                       std::to_string( handle ) );
        return {};
    }

    mNumOfSamplesAccessedBySender++;
    rawDataBuffer->mNumOfSamplesAccessedBySender++;
    auto loanedRawDataFrame = LoanedFrame( this, typeId, handle, &rawDataFrame->mRawData[0], dataSize );
    rawDataFrame->mDataInUseCounter++;
    return loanedRawDataFrame;
}

bool
BufferManager::increaseHandleUsageHint( BufferTypeId typeId,
                                        BufferHandle handle,
                                        BufferHandleUsageStage handleUsageStage )
{
    std::lock_guard<std::mutex> lock( mBufferManagerMutex );

    auto result = findBufferAndFrame( typeId, handle );
    Buffer *rawDataBuffer = result.first;
    Frame *rawDataFrame = result.second;

    uint32_t stageIndex = static_cast<uint32_t>( handleUsageStage );

    if ( rawDataBuffer == nullptr )
    {
        FWE_LOG_WARN( "Stage index " + std::to_string( stageIndex ) + " for type id: " + std::to_string( typeId ) +
                      " could not be incremented because the buffer couldn't be found" );
        return false;
    }

    if ( rawDataFrame == nullptr )
    {
        FWE_LOG_WARN( "Stage index " + std::to_string( stageIndex ) + " for type id: " + std::to_string( typeId ) +
                      " could not be incremented because the frame couldn't be found" );
        return false;
    }

    if ( !isValidStageIndex( stageIndex ) )
    {
        FWE_LOG_ERROR( "Stage index " + std::to_string( stageIndex ) + " for type id: " + std::to_string( typeId ) +
                       " could not be incremented because the stage index is invalid" );
        return false;
    }

    if ( rawDataFrame->mUsageHintCountersPerStage[stageIndex] >= static_cast<uint8_t>( UINT8_MAX ) )
    {
        FWE_LOG_ERROR( "Stage index " + std::to_string( stageIndex ) + " for type id: " + std::to_string( typeId ) +
                       " could not be incremented because it is already at max value" );
        return false;
    }

    rawDataFrame->mUsageHintCountersPerStage[stageIndex]++;

    return true;
}

bool
BufferManager::decreaseHandleUsageHint( BufferTypeId typeId,
                                        BufferHandle handle,
                                        BufferHandleUsageStage handleUsageStage )
{
    std::lock_guard<std::mutex> lock( mBufferManagerMutex );

    auto result = findBufferAndFrame( typeId, handle );
    Buffer *rawDataBuffer = result.first;
    Frame *rawDataFrame = result.second;

    uint32_t stageIndex = static_cast<uint32_t>( handleUsageStage );

    if ( rawDataBuffer == nullptr )
    {
        FWE_LOG_WARN( "Stage index " + std::to_string( stageIndex ) + " for type id: " + std::to_string( typeId ) +
                      " could not be decremented because the buffer couldn't be found" );
        return false;
    }

    if ( rawDataFrame == nullptr )
    {
        FWE_LOG_WARN( "Stage index " + std::to_string( stageIndex ) + " for type id: " + std::to_string( typeId ) +
                      " could not be decremented because the frame couldn't be found" );
        return false;
    }

    if ( !isValidStageIndex( stageIndex ) )
    {
        FWE_LOG_ERROR( "Stage index " + std::to_string( stageIndex ) + " for type id: " + std::to_string( typeId ) +
                       " could not be decremented because the stage index is invalid" );
        return false;
    }

    if ( rawDataFrame->mUsageHintCountersPerStage[stageIndex] == 0U )
    {
        FWE_LOG_ERROR( "Stage index " + std::to_string( stageIndex ) + " for type id: " + std::to_string( typeId ) +
                       " could not be decremented because it is already 0" );
        return false;
    }

    rawDataFrame->mUsageHintCountersPerStage[stageIndex]--;

    deleteUnused( *rawDataBuffer, *rawDataFrame );

    return true;
}

void
BufferManager::resetUsageHintsForStage( BufferHandleUsageStage handleUsageStage )
{

    uint32_t stageIndex = static_cast<uint32_t>( handleUsageStage );
    if ( stageIndex >= static_cast<uint32_t>( BufferHandleUsageStage::STAGE_SIZE ) )
    {
        // Invalid Stage
        return;
    }

    std::lock_guard<std::mutex> lock( mBufferManagerMutex );

    std::vector<BufferTypeId> typeIds;
    for ( auto &typeBuffer : mTypeIDToBufferMap )
    {
        typeIds.emplace_back( typeBuffer.first );
    }

    for ( auto typeId : typeIds )
    {
        auto &typeBuffer = mTypeIDToBufferMap[typeId];
        auto initialSize = typeBuffer.mBuffer.size();
        // Since elements might be deleted we can't just iterate through the vector (and we can't
        // get an updated iterator because the erase call happens deeper down in the call stack).
        // So we need to do some math with the index.
        for ( size_t i = 0; i < initialSize; i++ )
        {
            auto index = i - ( initialSize - typeBuffer.mBuffer.size() );
            auto &frame = typeBuffer.mBuffer[index];
            frame.mUsageHintCountersPerStage[stageIndex] = 0;
            deleteUnused( typeBuffer, frame );
        }
    }
}

void
BufferManager::returnLoanedFrame( BufferTypeId typeId, BufferHandle handle )
{
    std::lock_guard<std::mutex> lock( mBufferManagerMutex );

    auto result = findBufferAndFrame( typeId, handle );
    Buffer *rawDataBuffer = result.first;
    Frame *rawDataFrame = result.second;

    if ( ( rawDataBuffer == nullptr ) || ( rawDataFrame == nullptr ) )
    {
        FWE_LOG_ERROR( "No Raw data entry for Signal ID " + std::to_string( typeId ) + " BufferHandle " +
                       std::to_string( handle ) );
        return;
    }

    mNumOfSamplesAccessedBySender--;
    rawDataBuffer->mNumOfSamplesAccessedBySender--;

    if ( rawDataFrame->mDataInUseCounter > 0 )
    {
        rawDataFrame->mDataInUseCounter--;
    }
    else if ( rawDataFrame->mDataInUseCounter == 0 )
    {
        // Not much we can do here besides logging
        FWE_LOG_ERROR( "Cannot decrement data reference counter for Signal ID" + std::to_string( typeId ) +
                       " BufferHandle " + std::to_string( handle ) );
    }

    deleteUnused( *rawDataBuffer, *rawDataFrame );
}

void
BufferManager::deleteUnused( Buffer &buffer, Frame &frame )
{
    if ( ( frame.mDataInUseCounter != 0 ) || ( frame.hasUsageHints() ) )
    {
        return;
    }
    BufferHandle handle = frame.mHandleID;

    // Since more data could be deleted, we don't know how much to add to the stats variables.
    // So we subtract the current values related to the individual buffer first and then add the
    // values again after the operation.
    deleteBufferFromStats( buffer );
    const auto deletedMemSize = buffer.deleteDataFromHandle( handle );
    addBufferToStats( buffer );
    TraceModule::get().setVariable( TraceVariable::RAW_DATA_BUFFER_MANAGER_BYTES, mBytesInUse );
    if ( deletedMemSize == 0 )
    {
        FWE_LOG_ERROR( "Could not delete data for Signal ID " + std::to_string( buffer.mTypeID ) + " BufferHandle " +
                       std::to_string( handle ) );
    }

    if ( ( buffer.mNumOfSamplesCurrentlyInMemory == 0 ) && ( buffer.mDeleting ) )
    {
        FWE_LOG_TRACE( "Deleting buffer for Signal ID " + std::to_string( buffer.mTypeID ) );
        deleteBufferFromStats( buffer );
        mTypeIDToBufferMap.erase( buffer.mTypeID );
    }
}

void
BufferManager::deleteBufferFromStats( Buffer &buffer )
{
    auto bytesInUseAndReserved = std::max( buffer.mBytesInUse, buffer.mReservedBytes );

    FWE_FATAL_ASSERT( ( mBytesInUse <= mMaxOverallMemory ), "" );
    FWE_FATAL_ASSERT( ( mBytesInUseAndReserved <= mMaxOverallMemory ), "" );
    FWE_FATAL_ASSERT( ( mBytesReserved <= mMaxOverallMemory ), "" );
    FWE_FATAL_ASSERT( ( mBytesInUse <= mBytesInUseAndReserved ), "" );
    FWE_FATAL_ASSERT( ( mBytesReserved <= mBytesInUseAndReserved ), "" );

    FWE_FATAL_ASSERT( mBytesInUse >= buffer.mBytesInUse, "" );
    FWE_FATAL_ASSERT( mBytesInUseAndReserved >= bytesInUseAndReserved, "" );
    FWE_FATAL_ASSERT( mNumOfSamplesCurrentlyInMemory >= buffer.mNumOfSamplesCurrentlyInMemory, "" );

    mBytesInUse -= buffer.mBytesInUse;
    mBytesInUseAndReserved -= bytesInUseAndReserved;
    mBytesReserved -= buffer.mReservedBytes;
    mNumOfSamplesCurrentlyInMemory -= buffer.mNumOfSamplesCurrentlyInMemory;

    FWE_FATAL_ASSERT( mBytesInUse <= mMaxOverallMemory, "" );
    FWE_FATAL_ASSERT( mBytesInUseAndReserved <= mMaxOverallMemory, "" );
    FWE_FATAL_ASSERT( mBytesReserved <= mMaxOverallMemory, "" );
    FWE_FATAL_ASSERT( mBytesInUse <= mBytesInUseAndReserved, "" );
    FWE_FATAL_ASSERT( mBytesReserved <= mBytesInUseAndReserved, "" );
}

void
BufferManager::addBufferToStats( Buffer &buffer )
{
    auto bytesInUseAndReserved = std::max( buffer.mBytesInUse, buffer.mReservedBytes );

    FWE_FATAL_ASSERT( mBytesInUse <= mMaxOverallMemory, "" );
    FWE_FATAL_ASSERT( mBytesInUseAndReserved <= mMaxOverallMemory, "" );
    FWE_FATAL_ASSERT( mBytesReserved <= mMaxOverallMemory, "" );
    FWE_FATAL_ASSERT( mBytesInUse <= mBytesInUseAndReserved, "" );
    FWE_FATAL_ASSERT( mBytesReserved <= mBytesInUseAndReserved, "" );

    mBytesInUse += buffer.mBytesInUse;
    mBytesInUseAndReserved += bytesInUseAndReserved;
    mBytesReserved += buffer.mReservedBytes;
    mNumOfSamplesCurrentlyInMemory += buffer.mNumOfSamplesCurrentlyInMemory;

    FWE_FATAL_ASSERT( mBytesInUse <= mMaxOverallMemory, "" );
    FWE_FATAL_ASSERT( mBytesInUseAndReserved <= mMaxOverallMemory, "" );
    FWE_FATAL_ASSERT( mBytesReserved <= mMaxOverallMemory, "" );
    FWE_FATAL_ASSERT( mBytesInUse <= mBytesInUseAndReserved, "" );
    FWE_FATAL_ASSERT( mBytesReserved <= mBytesInUseAndReserved, "" );
}

TypeStatistics
BufferManager::getStatistics()
{
    std::lock_guard<std::mutex> lock( mBufferManagerMutex );

    return TypeStatistics{
        mOverallNumOfSamplesReceived, mNumOfSamplesCurrentlyInMemory, mNumOfSamplesAccessedBySender };
}

TypeStatistics
BufferManager::getStatistics( BufferTypeId typeId )
{
    std::lock_guard<std::mutex> lock( mBufferManagerMutex );

    auto typeIDBuffer = mTypeIDToBufferMap.find( typeId );
    if ( typeIDBuffer == mTypeIDToBufferMap.end() )
    {
        return INVALID_TYPE_STATISTICS;
    }
    return typeIDBuffer->second.getStatistics();
}

BufferErrorCode
BufferManager::addRawDataToBuffer( const SignalConfig &signalIDCollection )
{
    // Allocate the RawData Buffer
    if ( mTypeIDToBufferMap.find( signalIDCollection.typeId ) == mTypeIDToBufferMap.end() )
    {
        // Creating a new buffer
        if ( !checkMemoryLimit( signalIDCollection.reservedBytes ) )
        {
            FWE_LOG_ERROR( "Max Memory limit reached for signal " + std::to_string( signalIDCollection.typeId ) +
                           " requesting " + std::to_string( signalIDCollection.reservedBytes ) + " bytes" +
                           " with current memory limit at " + std::to_string( mBytesReserved ) +
                           " and the maximum allowed memory is " + std::to_string( mMaxOverallMemory ) );
            return BufferErrorCode::OUTOFMEMORY;
        }
        mTypeIDToBufferMap[signalIDCollection.typeId] = Buffer( signalIDCollection.typeId,
                                                                signalIDCollection.maxNumOfSamples,
                                                                signalIDCollection.maxBytesPerSample,
                                                                signalIDCollection.maxOverallBytes,
                                                                signalIDCollection.reservedBytes,
                                                                signalIDCollection.storageStrategy );
        addBufferToStats( mTypeIDToBufferMap[signalIDCollection.typeId] );
    }
    else
    {
        // Buffer exists
        auto &currRawBuffer = mTypeIDToBufferMap[signalIDCollection.typeId];
        FWE_LOG_INFO( "signalID already exists: " + std::to_string( signalIDCollection.typeId ) +
                      " so no need to update its buffer." );

        // Since the config never changes we don't need to update anything besides making sure it is
        // not marked to be deleted.
        currRawBuffer.mDeleting = false;
    }
    return BufferErrorCode::SUCCESSFUL;
}

std::pair<BufferManager::Buffer *, Frame *>
BufferManager::findBufferAndFrame( BufferTypeId typeId, BufferHandle handle )
{
    auto rawDataBuffer = mTypeIDToBufferMap.find( typeId );
    if ( rawDataBuffer == mTypeIDToBufferMap.end() )
    {
        FWE_LOG_WARN( "Signal ID " + std::to_string( typeId ) + " raw data not found" );
        return { nullptr, nullptr };
    }
    auto rawDataFrame = std::find_if(
        rawDataBuffer->second.mBuffer.begin(), rawDataBuffer->second.mBuffer.end(), [&]( const Frame &frame ) -> bool {
            return frame.mHandleID == handle;
        } );
    if ( rawDataFrame == rawDataBuffer->second.mBuffer.end() )
    {
        FWE_LOG_WARN( "No Raw data entry for Signal ID " + std::to_string( typeId ) + " BufferHandle " +
                      std::to_string( handle ) );
        return { nullptr, nullptr };
    }

    return { &( rawDataBuffer->second ), &( *rawDataFrame ) };
}

bool
BufferManager::checkMemoryLimit( size_t memoryReq ) const
{
    if ( memoryReq > mMaxOverallMemory )
    {
        return false;
    }
    return ( ( mBytesReserved + memoryReq ) <= mMaxOverallMemory );
}

BufferHandle
BufferManager::generateHandleID( Timestamp timestamp )
{
    // Generate a BufferHandle as a combination of the raw message counter and a timestamp
    return static_cast<BufferHandle>( static_cast<Timestamp>( generateRawMsgCounter() ) | ( timestamp << 8 ) );
}

bool
BufferManager::Buffer::addData( const uint8_t *data,
                                size_t size,
                                Timestamp receiveTimestamp,
                                BufferHandle rawDataHandle,
                                size_t availableFreeMemory )
{
    auto bytesAvailableToUse = availableFreeMemory;
    if ( mBytesInUse < mReservedBytes )
    {
        bytesAvailableToUse += ( mReservedBytes - mBytesInUse );
    }

    // check memory
    size_t requiredBytes = size;
    if ( ( requiredBytes > mMaxBytesPerSample ) || ( requiredBytes > mMaxOverallBytes ) )
    {
        return false;
    }

    if ( mNumOfSamplesCurrentlyInMemory == mMaxNumOfSamples )
    {
        // Buffer is full; delete some unused data
        if ( !deleteUnusedData() )
        {
            return false;
        }
    }

    while ( mBytesInUse + requiredBytes > mMaxOverallBytes )
    {
        // Memory allocation exceeded; Delete some unused data
        if ( !deleteUnusedData() )
        {
            return false;
        }
    }

    while ( requiredBytes > bytesAvailableToUse )
    {
        // We didn't exceed any limits, but there is not enough available memory anyway, so delete
        // some unused data.
        auto bytesInUseBefore = mBytesInUse;
        if ( !deleteUnusedData() )
        {
            return false;
        }
        auto released = bytesInUseBefore - mBytesInUse;
        bytesAvailableToUse += released;
    }

    // Copy Data
    RawDataType rawData;
    rawData.assign( data, data + size );

    // Allocate
    mBuffer.emplace_back( rawDataHandle, receiveTimestamp, std::move( rawData ) );

    mNumOfSamplesReceived++;
    mNumOfSamplesCurrentlyInMemory++;
    TraceModule::get().setVariable( TraceVariable::RAW_DATA_BUFFER_ELEMENTS_PER_TYPE, mNumOfSamplesCurrentlyInMemory );
    mBytesInUse += requiredBytes;
    return true;
}

bool
BufferManager::Buffer::deleteUnusedData()
{
    auto unusedDataFrame = std::find_if( mBuffer.cbegin(), mBuffer.cend(), [&]( const Frame &rawDataFrame ) -> bool {
        return ( rawDataFrame.mDataInUseCounter == 0 ) && ( !rawDataFrame.hasUsageHints() );
    } );
    if ( unusedDataFrame == mBuffer.cend() )
    {
        // If we couldn't find any unused data with unused handle, then our next option is
        // to release unused data whose handle is in use. This will cause the user to get
        // a null when calling borrowFrame() to request the data.
        unusedDataFrame = std::find_if( mBuffer.cbegin(), mBuffer.cend(), [&]( const Frame &rawDataFrame ) -> bool {
            return ( rawDataFrame.mDataInUseCounter == 0 ) &&
                   ( rawDataFrame.getUsageHint( BufferHandleUsageStage::UPLOADING ) == 0 );
        } );
        if ( unusedDataFrame == mBuffer.cend() )
        {
            FWE_LOG_WARN( "Could not find any unused data to delete for Signal ID " + std::to_string( mTypeID ) );
            return false;
        }
        else
        {
            TraceModule::get().incrementVariable( TraceVariable::RAW_DATA_OVERWRITTEN_DATA_WITH_USED_HANDLE );
        }
    }
    FWE_LOG_TRACE( "Deleting data for Signal ID " + std::to_string( mTypeID ) + " BufferHandle " +
                   std::to_string( unusedDataFrame->mHandleID ) +
                   " with usage hints: " + std::to_string( unusedDataFrame->hasUsageHints() ) );
    auto deletedMemSize = unusedDataFrame->mRawData.size();
    mBytesInUse -= deletedMemSize;    // Subtract the memory
    mBuffer.erase( unusedDataFrame ); // Delete the data
    mNumOfSamplesCurrentlyInMemory--;
    TraceModule::get().setVariable( TraceVariable::RAW_DATA_BUFFER_ELEMENTS_PER_TYPE, mNumOfSamplesCurrentlyInMemory );
    return true;
}

size_t
BufferManager::Buffer::deleteDataFromHandle( const BufferHandle handle )
{
    FWE_LOG_TRACE( "Deleting data for Signal ID " + std::to_string( mTypeID ) + " BufferHandle " +
                   std::to_string( handle ) );
    auto unusedDataFrame = std::find_if( mBuffer.cbegin(), mBuffer.cend(), [&]( const Frame &rawDataFrame ) -> bool {
        return rawDataFrame.mHandleID == handle;
    } );
    if ( unusedDataFrame == mBuffer.cend() )
    {
        FWE_LOG_TRACE( "Could not find data for Signal ID " + std::to_string( mTypeID ) + " BufferHandle " +
                       std::to_string( handle ) );
        return 0;
    }
    auto deletedMemSize = unusedDataFrame->mRawData.size();
    mBytesInUse -= deletedMemSize;    // Subtract the memory
    mBuffer.erase( unusedDataFrame ); // Delete the data
    mNumOfSamplesCurrentlyInMemory--;
    TraceModule::get().setVariable( TraceVariable::RAW_DATA_BUFFER_ELEMENTS_PER_TYPE, mNumOfSamplesCurrentlyInMemory );
    return deletedMemSize;
}

FrameTimestamp
BufferManager::Buffer::getAvgTimeInMemory() const
{
    const auto bufferSize = getSize();
    if ( bufferSize == 0 )
    {
        return 0;
    }
    FrameTimestamp avgTimeInMemory = 0;
    const auto currTime = mClock->systemTimeSinceEpochMs();
    auto calCurrTimeInMemory = [&]( FrameTimestamp avgTime, const Frame &rawDataSample ) -> FrameTimestamp {
        return avgTime + ( currTime - rawDataSample.mTimestamp );
    };
    FrameTimestamp mSumTimeStamp =
        std::accumulate( mBuffer.begin(), mBuffer.end(), avgTimeInMemory, calCurrTimeInMemory );
    return mSumTimeStamp / bufferSize;
}

TypeStatistics
BufferManager::Buffer::getStatistics() const
{
    return TypeStatistics{ mNumOfSamplesReceived,
                           mNumOfSamplesCurrentlyInMemory,
                           mNumOfSamplesAccessedBySender,
                           getMaxTimeInMemory(),
                           getAvgTimeInMemory(),
                           getMinTimeInMemory() };
}

boost::optional<BufferManagerConfig>
BufferManagerConfig::create()
{
    std::vector<SignalBufferOverrides> overridesPerSignal;
    return create( {}, {}, {}, {}, {}, overridesPerSignal );
}

boost::optional<BufferManagerConfig>
BufferManagerConfig::create( boost::optional<size_t> maxBytes,
                             boost::optional<size_t> reservedBytesPerSignal,
                             boost::optional<size_t> maxNumOfSamplesPerSignal,
                             boost::optional<size_t> maxBytesPerSample,
                             boost::optional<size_t> maxBytesPerSignal,
                             std::vector<SignalBufferOverrides> &overridesPerSignal )
{
    BufferManagerConfig config;

    config.mMaxBytes = maxBytes.get_value_or( 1 * 1024 * 1024 * 1024 );
    if ( config.mMaxBytes == 0 )
    {
        FWE_LOG_ERROR( "Invalid buffer config. Max overall buffer size can't be zero" );
        return {};
    }

    config.mMaxNumOfSamplesPerSignal = maxNumOfSamplesPerSignal.get_value_or( SIZE_MAX );
    if ( config.mMaxNumOfSamplesPerSignal == 0 )
    {
        FWE_LOG_ERROR( "Invalid buffer config. Max number of samples per signal can't be zero." );
        return {};
    }

    config.mMaxBytesPerSignal = maxBytesPerSignal.get_value_or( config.mMaxBytes );
    if ( config.mMaxBytesPerSignal == 0 )
    {
        FWE_LOG_ERROR( "Invalid buffer config. Max bytes per signal can't be zero." );
        return {};
    }
    if ( config.mMaxBytesPerSignal > config.mMaxBytes )
    {
        FWE_LOG_ERROR( "Invalid buffer config. Max bytes per signal " + std::to_string( config.mMaxBytesPerSignal ) +
                       " can't be larger than max overall buffer size " + std::to_string( config.mMaxBytes ) );
        return {};
    }

    config.mMaxBytesPerSample = maxBytesPerSample.get_value_or( config.mMaxBytesPerSignal );
    if ( config.mMaxBytesPerSample == 0 )
    {
        FWE_LOG_ERROR( "Invalid buffer config. Max bytes per sample can't be zero." );
        return {};
    }
    if ( config.mMaxBytesPerSample > config.mMaxBytes )
    {
        FWE_LOG_ERROR( "Invalid buffer config. Max bytes per sample " + std::to_string( config.mMaxBytesPerSample ) +
                       " can't be larger than max overall buffer size " + std::to_string( config.mMaxBytes ) );
        return {};
    }
    if ( config.mMaxBytesPerSample > config.mMaxBytesPerSignal )
    {
        FWE_LOG_ERROR( "Invalid buffer config. Max bytes per sample " + std::to_string( config.mMaxBytesPerSample ) +
                       " can't be larger than max bytes per signal " + std::to_string( config.mMaxBytesPerSignal ) );
        return {};
    }

    config.mReservedBytesPerSignal = reservedBytesPerSignal.get_value_or( 0 );
    if ( config.mReservedBytesPerSignal > config.mMaxBytes )
    {
        FWE_LOG_ERROR( "Invalid buffer config. Reserved bytes per signal " +
                       std::to_string( config.mReservedBytesPerSignal ) +
                       " can't be larger than max overall buffer size " + std::to_string( config.mMaxBytes ) );
        return {};
    }
    if ( config.mReservedBytesPerSignal > config.mMaxBytesPerSignal )
    {
        FWE_LOG_ERROR( "Invalid buffer config. Reserved bytes per signal " +
                       std::to_string( config.mReservedBytesPerSignal ) +
                       " can't be larger than max bytes per signal " + std::to_string( config.mMaxBytesPerSignal ) );
        return {};
    }

    for ( auto signalOverride : overridesPerSignal )
    {
        if ( ( config.mOverridesPerSignal.find( signalOverride.interfaceId ) != config.mOverridesPerSignal.end() ) &&
             ( config.mOverridesPerSignal[signalOverride.interfaceId].find( signalOverride.messageId ) !=
               config.mOverridesPerSignal[signalOverride.interfaceId].end() ) )
        {
            FWE_LOG_ERROR( "Duplicate buffer config override for interfaceId '" + signalOverride.interfaceId +
                           "' and messageId '" + signalOverride.messageId + "'" );
            return {};
        }

        auto maxBytesCur = config.mMaxBytes;
        if ( signalOverride.maxBytes.has_value() )
        {
            maxBytesCur = signalOverride.maxBytes.get();
            if ( maxBytesCur > config.mMaxBytes )
            {
                FWE_LOG_ERROR( "Invalid buffer config override for interfaceId '" + signalOverride.interfaceId +
                               "' and messageId '" + signalOverride.messageId + "'. Max bytes for this signal " +
                               std::to_string( maxBytesCur ) + " can't be larger than max overall buffer size " +
                               std::to_string( config.mMaxBytes ) );
                return {};
            }
            // In case the size per sample is not set, it should be capped to the max size allowed for this signal.
            if ( !signalOverride.maxBytesPerSample.has_value() )
            {
                signalOverride.maxBytesPerSample = maxBytesCur;
            }
        }

        if ( signalOverride.maxNumOfSamples.get_value_or( SIZE_MAX ) == 0 )
        {
            FWE_LOG_ERROR( "Invalid buffer config override for interfaceId '" + signalOverride.interfaceId +
                           "' and messageId '" + signalOverride.messageId +
                           "'. Max number of samples for this signal can't be zero" );
            return {};
        }

        auto maxBytesPerSampleCur = signalOverride.maxBytesPerSample.get_value_or( config.mMaxBytesPerSample );
        if ( maxBytesPerSampleCur == 0 )
        {
            FWE_LOG_ERROR( "Invalid buffer config override for interfaceId '" + signalOverride.interfaceId +
                           "' and messageId '" + signalOverride.messageId +
                           "'. Max bytes per sample for this signal can't be zero" );
            return {};
        }
        if ( maxBytesPerSampleCur > maxBytesCur )
        {
            FWE_LOG_ERROR( "Invalid buffer config override for interfaceId '" + signalOverride.interfaceId +
                           "' and messageId '" + signalOverride.messageId + "'. Max bytes per sample for this signal " +
                           std::to_string( maxBytesPerSampleCur ) + " can't be larger than max bytes for this signal " +
                           std::to_string( maxBytesCur ) );
            return {};
        }

        auto reservedBytes = signalOverride.reservedBytes.get_value_or( config.mReservedBytesPerSignal );
        if ( reservedBytes > maxBytesCur )
        {
            FWE_LOG_ERROR( "Invalid buffer config override for interfaceId '" + signalOverride.interfaceId +
                           "' and messageId '" + signalOverride.messageId + "'. Reserved bytes for this signal " +
                           std::to_string( reservedBytes ) + " can't be larger than max bytes for this signal " +
                           std::to_string( maxBytesCur ) );
            return {};
        }

        FWE_LOG_TRACE( "Adding override for interfaceId '" + signalOverride.interfaceId + "' and messageId '" +
                       signalOverride.messageId + "'" );

        config.mOverridesPerSignal[signalOverride.interfaceId][signalOverride.messageId] = signalOverride;
    }

    return config;
}

SignalConfig
BufferManagerConfig::getSignalConfig( BufferTypeId typeId,
                                      const InterfaceID &interfaceId,
                                      const std::string &messageId ) const
{
    SignalConfig signalConfig;
    signalConfig.typeId = typeId;
    signalConfig.reservedBytes = mReservedBytesPerSignal;
    signalConfig.maxNumOfSamples = mMaxNumOfSamplesPerSignal;
    signalConfig.maxBytesPerSample = mMaxBytesPerSample;
    signalConfig.maxOverallBytes = mMaxBytesPerSignal;

    auto overrideForInterfaceId = mOverridesPerSignal.find( interfaceId );
    if ( overrideForInterfaceId == mOverridesPerSignal.end() )
    {
        FWE_LOG_TRACE( "Could not find any signal config override for interfaceId '" + interfaceId +
                       "' and messageId '" + messageId + "'" );
        return signalConfig;
    }

    auto overrideForMessageId = overrideForInterfaceId->second.find( messageId );
    if ( overrideForMessageId == overrideForInterfaceId->second.end() )
    {
        FWE_LOG_TRACE( "Could not find any signal config override for interfaceId '" + interfaceId +
                       "' and messageId '" + messageId + "'" );
        return signalConfig;
    }

    FWE_LOG_TRACE( "Using signal config overrides for interfaceId '" + interfaceId + "' and messageId '" + messageId +
                   "'" );
    signalConfig.reservedBytes = overrideForMessageId->second.reservedBytes.get_value_or( signalConfig.reservedBytes );
    signalConfig.maxNumOfSamples =
        overrideForMessageId->second.maxNumOfSamples.get_value_or( signalConfig.maxNumOfSamples );
    signalConfig.maxBytesPerSample =
        overrideForMessageId->second.maxBytesPerSample.get_value_or( signalConfig.maxBytesPerSample );
    signalConfig.maxOverallBytes = overrideForMessageId->second.maxBytes.get_value_or( signalConfig.maxOverallBytes );

    return signalConfig;
}

} // namespace RawData
} // namespace IoTFleetWise
} // namespace Aws
