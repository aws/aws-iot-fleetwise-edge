// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/CustomFunctionScriptEngine.h"
#include "aws/iotfleetwise/LoggingModule.h"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <cstdint>
#include <cstdlib>
#include <fstream> // IWYU pragma: keep
#include <sstream> // IWYU pragma: keep
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

static const std::string SCRIPT_NAME_PREFIX = "script_";
static const std::string DOWNLOAD_COMPLETE_FILENAME_PREFIX = "download_complete_";

CustomFunctionScriptEngine::CustomFunctionScriptEngine(
    std::shared_ptr<NamedSignalDataSource> namedSignalDataSource,
    RawData::BufferManager *rawDataBufferManager,
    std::function<std::shared_ptr<TransferManagerWrapper>()> createTransferManagerWrapper,
    std::string downloadDirectory,
    std::string bucketName )
    : mNamedSignalDataSource( std::move( namedSignalDataSource ) )
    , mRawDataBufferManager( rawDataBufferManager )
    , mCreateTransferManagerWrapper( std::move( createTransferManagerWrapper ) )
    , mDownloadDirectory( std::move( downloadDirectory ) )
    , mBucketName( std::move( bucketName ) )
{
}

void
CustomFunctionScriptEngine::shutdown()
{
    if ( mTransferManagerWrapper )
    {
        FWE_LOG_TRACE( "Cancelling all ongoing transfers and waiting for them to finish" );
        mTransferManagerWrapper->CancelAll();
        mTransferManagerWrapper->WaitUntilAllFinished();
    }
    try
    {
        // coverity[misra_cpp_2008_rule_14_8_2_violation] - boost filesystem path header defines both template and and
        // non-template function
        for ( boost::filesystem::directory_iterator it( mDownloadDirectory );
              it != boost::filesystem::directory_iterator();
              ++it )
        {
            deleteIfNotInUse( it->path().string() );
        }
    }
    catch ( const boost::filesystem::filesystem_error &err )
    {
        FWE_LOG_ERROR( "Error during clean up: " + std::string( err.what() ) );
    }
}

void
CustomFunctionScriptEngine::deleteIfNotInUse( const std::string &filePath )
{
    for ( const auto &invocationState : mInvocationStates )
    {
        if ( ( filePath == getScriptDirectory( invocationState.first ) ) ||
             ( filePath == getDownloadCompleteFilename( invocationState.first ) ) )
        {
            // In use, so don't clean up
            return;
        }
    }
    try
    {
        boost::filesystem::remove_all( filePath );
        FWE_LOG_TRACE( "Deleted " + filePath );
    }
    catch ( ... )
    {
        FWE_LOG_ERROR( "Error deleting " + filePath );
    }
}

std::string
CustomFunctionScriptEngine::getScriptName( CustomFunctionInvocationID invocationId )
{
    return SCRIPT_NAME_PREFIX + customFunctionInvocationIdToHexString( invocationId );
}

std::string
CustomFunctionScriptEngine::getDownloadCompleteFilename( CustomFunctionInvocationID invocationId )
{
    return mDownloadDirectory + "/" + DOWNLOAD_COMPLETE_FILENAME_PREFIX +
           customFunctionInvocationIdToHexString( invocationId );
}

std::string
CustomFunctionScriptEngine::getScriptDirectory( CustomFunctionInvocationID invocationId )
{
    return mDownloadDirectory + "/" + getScriptName( invocationId );
}

CustomFunctionInvocationID
CustomFunctionScriptEngine::getInvocationIDFromFilePath( std::string filePath )
{
    try
    {
        return static_cast<CustomFunctionInvocationID>(
            std::stoull( filePath.substr( filePath.find( "_", mDownloadDirectory.size() ) + 1 ), nullptr, 16 ) );
    }
    catch ( ... )
    {
        FWE_LOG_ERROR( "Could not extract invocation ID: " + filePath );
        return 0;
    }
}

void
// coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
CustomFunctionScriptEngine::transferStatusUpdatedCallback(
    const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle )
{
    if ( ( transferHandle->GetStatus() == Aws::Transfer::TransferStatus::NOT_STARTED ) ||
         ( transferHandle->GetStatus() == Aws::Transfer::TransferStatus::IN_PROGRESS ) )
    {
        return;
    }
    auto invocationId = getInvocationIDFromFilePath( transferHandle->GetTargetFilePath() );
    if ( invocationId == 0 )
    {
        return;
    }
    auto downloadComplete = false;
    {
        std::lock_guard<std::recursive_mutex> lock( mInvocationStateMutex );
        auto stateIt = mInvocationStates.find( invocationId );
        if ( stateIt == mInvocationStates.end() )
        {
            return; // Already cleaned up
        }
        if ( transferHandle->GetStatus() != Aws::Transfer::TransferStatus::COMPLETED )
        {
            FWE_LOG_ERROR( "Transfer error for invocation ID " + customFunctionInvocationIdToHexString( invocationId ) +
                           ", object " + transferHandle->GetKey() );
            stateIt->second.status = ScriptStatus::ERROR;
            return;
        }
        FWE_LOG_TRACE( "Download complete for invocation ID " + customFunctionInvocationIdToHexString( invocationId ) +
                       ", object " + transferHandle->GetKey() );
        stateIt->second.transferIds.erase( transferHandle->GetId() );
        if ( stateIt->second.transferIds.empty() && ( stateIt->second.status == ScriptStatus::DOWNLOADING ) )
        {
            FWE_LOG_INFO( "All downloads complete for invocation ID " +
                          customFunctionInvocationIdToHexString( invocationId ) );
            downloadComplete = true;
        }
    }

    if ( downloadComplete )
    {
        auto error = false;
        auto scriptDirectory = getScriptDirectory( invocationId );
        // If a tar.gz exists, extract it:
        auto archiveFilename = scriptDirectory + ".tar.gz";
        if ( boost::filesystem::exists( archiveFilename ) )
        {
            FWE_LOG_TRACE( "Extracting archive " + archiveFilename );
            // coverity[misra_cpp_2008_rule_18_0_3_violation] Calling tar on filepaths that are not externally defined
            // coverity[autosar_cpp14_m18_0_3_violation ] Calling tar on filepaths that are not externally defined
            auto res = system( ( "tar -zxf " + archiveFilename + " -C " + scriptDirectory ).c_str() ); // NOLINT
            if ( res != 0 )
            {
                FWE_LOG_ERROR( "Error extracting archive" );
                error = true;
            }
        }

        if ( !error )
        {
            // Mark the script as download complete:
            std::ofstream file( getDownloadCompleteFilename( invocationId ) );
            file.close();
        }

        {
            std::lock_guard<std::recursive_mutex> lock( mInvocationStateMutex );
            auto stateIt = mInvocationStates.find( invocationId );
            if ( stateIt == mInvocationStates.end() )
            {
                return; // Already cleaned up
            }
            stateIt->second.status = error ? ScriptStatus::ERROR : ScriptStatus::RUNNING;
        }
    }
}

void
// coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
CustomFunctionScriptEngine::transferErrorCallback(
    const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle,
    const Aws::Client::AWSError<Aws::S3::S3Errors> &error )
{
    auto invocationId = getInvocationIDFromFilePath( transferHandle->GetTargetFilePath() );
    if ( invocationId == 0 )
    {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock( mInvocationStateMutex );
    auto stateIt = mInvocationStates.find( invocationId );
    if ( stateIt == mInvocationStates.end() )
    {
        return; // Already cleaned up
    }
    std::stringstream ss;
    ss << error;
    FWE_LOG_ERROR( "Transfer error for invocation ID " + customFunctionInvocationIdToHexString( invocationId ) +
                   ", object " + transferHandle->GetKey() + ": " + ss.str() );
    stateIt->second.status = ScriptStatus::ERROR;
}

void
// coverity[autosar_cpp14_a8_4_11_violation] smart pointer needed to match the expected signature
CustomFunctionScriptEngine::transferInitiatedCallback(
    const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle )
{
    auto invocationId = getInvocationIDFromFilePath( transferHandle->GetTargetFilePath() );
    if ( invocationId == 0 )
    {
        return;
    }
    std::lock_guard<std::recursive_mutex> lock( mInvocationStateMutex );
    auto stateIt = mInvocationStates.find( invocationId );
    if ( stateIt == mInvocationStates.end() )
    {
        return; // Already cleaned up
    }
    FWE_LOG_TRACE( "Starting file download for invocation ID " + customFunctionInvocationIdToHexString( invocationId ) +
                   ", object " + transferHandle->GetKey() );
    stateIt->second.transferIds.emplace( transferHandle->GetId() );
}

CustomFunctionScriptEngine::ScriptStatus
CustomFunctionScriptEngine::setup( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args )
{
    std::lock_guard<std::recursive_mutex> lock( mInvocationStateMutex );
    auto stateIt = mInvocationStates.find( invocationId );
    if ( stateIt == mInvocationStates.end() ) // First invocation
    {
        stateIt = mInvocationStates.emplace( invocationId, InvocationState{} ).first;
        auto &state = stateIt->second;
        if ( args.empty() || ( !args[0].isString() ) )
        {
            FWE_LOG_ERROR( "Unexpected number of arguments or type" );
            state.status = ScriptStatus::ERROR;
            return state.status;
        }
        if ( boost::filesystem::exists( getDownloadCompleteFilename( invocationId ) ) )
        {
            FWE_LOG_TRACE( "Skipping download for invocation ID " +
                           customFunctionInvocationIdToHexString( invocationId ) );
            state.status = ScriptStatus::RUNNING;
            return state.status;
        }
        auto scriptDirectory = getScriptDirectory( invocationId );
        try
        {
            boost::filesystem::remove_all( scriptDirectory );
            boost::filesystem::create_directories( scriptDirectory );
        }
        catch ( ... )
        {
            FWE_LOG_ERROR( "Error deleting or creating directory " + scriptDirectory );
            state.status = ScriptStatus::ERROR;
            return state.status;
        }
        if ( !mTransferManagerWrapper )
        {
            mTransferManagerWrapper = mCreateTransferManagerWrapper();
        }
        const auto &s3Prefix = *args[0].stringVal;
        if ( boost::ends_with( s3Prefix, ".tar.gz" ) )
        {
            transferInitiatedCallback(
                mTransferManagerWrapper->DownloadFile( mBucketName, s3Prefix, scriptDirectory + ".tar.gz" ) );
        }
        else
        {
            FWE_LOG_TRACE( "Starting directory download for invocation ID " +
                           customFunctionInvocationIdToHexString( invocationId ) + " prefix " + s3Prefix );
            mTransferManagerWrapper->DownloadToDirectory( scriptDirectory, mBucketName, s3Prefix );
        }
    }
    auto &state = stateIt->second;
    return state.status;
}

void
CustomFunctionScriptEngine::conditionEnd( const std::unordered_set<SignalID> &collectedSignalIds,
                                          Timestamp timestamp,
                                          CollectionInspectionEngineOutput &output )
{
    if ( mCollectedData.empty() )
    {
        return;
    }
    auto collectedData = std::move( mCollectedData );
    if ( !output.triggeredCollectionSchemeData )
    {
        return;
    }
    if ( ( mRawDataBufferManager == nullptr ) || ( mNamedSignalDataSource == nullptr ) )
    {
        FWE_LOG_WARN( "namedSignalInterface missing from config or raw buffer manager disabled" );
        return;
    }
    for ( const auto &signal : collectedData )
    {
        auto signalId = mNamedSignalDataSource->getNamedSignalID( signal.first );
        if ( signalId == INVALID_SIGNAL_ID )
        {
            FWE_LOG_WARN( signal.first + " not present in decoder manifest" );
            continue;
        }
        if ( collectedSignalIds.find( signalId ) == collectedSignalIds.end() )
        {
            continue;
        }
        auto bufferHandle = mRawDataBufferManager->push(
            reinterpret_cast<const uint8_t *>( signal.second.data() ), signal.second.size(), timestamp, signalId );
        if ( bufferHandle == RawData::INVALID_BUFFER_HANDLE )
        {
            continue;
        }
        // immediately set usage hint so buffer handle does not get directly deleted again
        mRawDataBufferManager->increaseHandleUsageHint(
            signalId, bufferHandle, RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_SELECTED_FOR_UPLOAD );
        output.triggeredCollectionSchemeData->signals.emplace_back(
            signalId, timestamp, bufferHandle, SignalType::STRING );
    }
}

void
CustomFunctionScriptEngine::cleanup( CustomFunctionInvocationID invocationId )
{
    std::lock_guard<std::recursive_mutex> lock( mInvocationStateMutex );
    mInvocationStates.erase( invocationId );
}

void
CustomFunctionScriptEngine::setStatus( CustomFunctionInvocationID invocationId, ScriptStatus status )
{
    std::lock_guard<std::recursive_mutex> lock( mInvocationStateMutex );
    auto stateIt = mInvocationStates.find( invocationId );
    if ( stateIt == mInvocationStates.end() )
    {
        FWE_LOG_ERROR( "Setting status for non-existent invocation ID " +
                       customFunctionInvocationIdToHexString( invocationId ) );
        return;
    }
    stateIt->second.status = status;
}

} // namespace IoTFleetWise
} // namespace Aws
