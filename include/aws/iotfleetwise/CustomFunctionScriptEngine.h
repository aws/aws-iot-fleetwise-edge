// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/NamedSignalDataSource.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "aws/iotfleetwise/TransferManagerWrapper.h"
#include <aws/transfer/TransferManager.h>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * Custom function script engine.
 * Provides helper functions for:
 * - Downloading scripts from the configured S3 bucket under a given prefix
 * - Holding the error state of each invocation ID, to only log about an error message once
 * - Holding any collected data from the invocation and provide it back via `conditionEnd`
 */
class CustomFunctionScriptEngine
{
public:
    CustomFunctionScriptEngine( std::shared_ptr<NamedSignalDataSource> namedSignalDataSource,
                                RawData::BufferManager *rawDataBufferManager,
                                std::function<std::shared_ptr<TransferManagerWrapper>()> createTransferManagerWrapper,
                                std::string downloadDirectory,
                                std::string bucketName );
    ~CustomFunctionScriptEngine() = default;

    CustomFunctionScriptEngine( const CustomFunctionScriptEngine & ) = delete;
    CustomFunctionScriptEngine &operator=( const CustomFunctionScriptEngine & ) = delete;
    CustomFunctionScriptEngine( CustomFunctionScriptEngine && ) = delete;
    CustomFunctionScriptEngine &operator=( CustomFunctionScriptEngine && ) = delete;

    enum class ScriptStatus
    {
        DOWNLOADING,
        RUNNING,
        ERROR
    };

    /**
     * @param invocationId Custom function invocation ID
     * @param args Custom function arguments. The first argument must be either a S3 prefix to download
     * the scripts from. It can also be an S3 key of a `.tar.gz` file, which will be extracted after download.
     * @returns Script status
     */
    ScriptStatus setup( CustomFunctionInvocationID invocationId, const std::vector<InspectionValue> &args );

    void conditionEnd( const std::unordered_set<SignalID> &collectedSignalIds,
                       Timestamp timestamp,
                       CollectionInspectionEngineOutput &output );

    void cleanup( CustomFunctionInvocationID invocationId );

    void transferStatusUpdatedCallback( const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle );
    void transferErrorCallback( const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle,
                                const Aws::Client::AWSError<Aws::S3::S3Errors> &error );
    void transferInitiatedCallback( const std::shared_ptr<const Aws::Transfer::TransferHandle> &transferHandle );

    static std::string getScriptName( CustomFunctionInvocationID invocationId );
    std::string getScriptDirectory( CustomFunctionInvocationID invocationId );
    std::string
    getDownloadDirectory()
    {
        return mDownloadDirectory;
    }

    /**
     * Sets the status of the given invocation. Normally only used to set to `ERROR`, meaning `setup`
     * will subsequently return `ERROR`. Can be used by unit testing code to reset status.
     * @param invocationId Custom function invocation ID
     * @param status Status
     */
    void setStatus( CustomFunctionInvocationID invocationId, ScriptStatus status );

    /**
     * Collected data map. Can be written by custom functions to return collected data via `conditionEnd`.
     * The key is the fully-qualified-name of the signal. Only string signals accessible via `namedSignalDataSource`
     * can be returned.
     */
    std::unordered_map<std::string, std::string> mCollectedData;

    /**
     * Stops all ongoing downloads and cleans up unused scripts
     */
    void shutdown();

private:
    struct InvocationState
    {
        ScriptStatus status{ ScriptStatus::DOWNLOADING };
        std::set<std::string> transferIds;
    };
    std::unordered_map<CustomFunctionInvocationID, InvocationState> mInvocationStates;
    std::shared_ptr<NamedSignalDataSource> mNamedSignalDataSource;
    RawData::BufferManager *mRawDataBufferManager;
    std::shared_ptr<TransferManagerWrapper> mTransferManagerWrapper;
    std::function<std::shared_ptr<TransferManagerWrapper>()> mCreateTransferManagerWrapper;
    std::string mDownloadDirectory;
    std::string mBucketName;
    std::recursive_mutex mInvocationStateMutex;

    CustomFunctionInvocationID getInvocationIDFromFilePath( std::string filePath );
    void deleteIfNotInUse( const std::string &filePath );
    std::string getDownloadCompleteFilename( CustomFunctionInvocationID invocationId );
};

} // namespace IoTFleetWise
} // namespace Aws
