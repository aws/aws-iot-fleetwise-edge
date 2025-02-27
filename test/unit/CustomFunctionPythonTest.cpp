// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "RawDataBufferManagerSpy.h"
#include "TransferManagerWrapperMock.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/CustomFunctionScriptEngine.h"
#include "aws/iotfleetwise/IDecoderDictionary.h"
#include "aws/iotfleetwise/IDecoderManifest.h"
#include "aws/iotfleetwise/NamedSignalDataSource.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/VehicleDataSourceTypes.h"
#include <aws/transfer/TransferManager.h>
#include <boost/filesystem.hpp>
#include <boost/optional/optional.hpp>
#include <cstdlib>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef FWE_FEATURE_MICROPYTHON
#include "aws/iotfleetwise/CustomFunctionMicroPython.h"
#endif
#ifdef FWE_FEATURE_CPYTHON
#include "aws/iotfleetwise/CustomFunctionCPython.h"
#endif

namespace Aws
{
namespace IoTFleetWise
{

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrictMock;

template <typename T>
class CustomFunctionPythonTemplate : public ::testing::Test
{
public:
    CustomFunctionPythonTemplate()
        : mDictionary( std::make_shared<CustomDecoderDictionary>() )
        , mSignalBuffer( std::make_shared<SignalBuffer>( 100, "Signal Buffer" ) )
        , mNamedSignalDataSource( std::make_shared<NamedSignalDataSource>( "NAMED_SIGNAL", mSignalBufferDistributor ) )
        , mRawDataBufferManagerSpy( RawData::BufferManagerConfig::create().get() )
        , mTransferManagerWrapperMock( std::make_shared<StrictMock<TransferManagerWrapperMock>>() )
        , mDownloadDirectory( ( boost::filesystem::temp_directory_path() / boost::filesystem::unique_path() ).string() )
        , mScriptEngine( std::make_shared<CustomFunctionScriptEngine>(
              mNamedSignalDataSource,
              &mRawDataBufferManagerSpy,
              [this]() {
                  return mTransferManagerWrapperMock;
              },
              mDownloadDirectory,
              "dummy-bucket" ) )
        , mCustomFunctionPython( std::make_shared<T>( mScriptEngine ) )
    {
        mDictionary->customDecoderMethod["NAMED_SIGNAL"]["Vehicle.OutputSignal"] =
            CustomSignalDecoderFormat{ "NAMED_SIGNAL", "Vehicle.OutputSignal", 1, SignalType::STRING };
        mNamedSignalDataSource->onChangeOfActiveDictionary( mDictionary, VehicleDataSourceProtocol::CUSTOM_DECODING );
        mSignalBufferDistributor.registerQueue( mSignalBuffer );
        mRawDataBufferManagerSpy.updateConfig( { { 1, { 1, "", "" } } } );
    }
    void
    SetUp() override
    {
    }
    std::shared_ptr<CustomDecoderDictionary> mDictionary;
    std::shared_ptr<SignalBuffer> mSignalBuffer;
    SignalBufferDistributor mSignalBufferDistributor;
    std::shared_ptr<NamedSignalDataSource> mNamedSignalDataSource;
    NiceMock<Testing::RawDataBufferManagerSpy> mRawDataBufferManagerSpy;
    std::shared_ptr<StrictMock<TransferManagerWrapperMock>> mTransferManagerWrapperMock;
    std::string mDownloadDirectory;
    std::shared_ptr<CustomFunctionScriptEngine> mScriptEngine;
    std::shared_ptr<T> mCustomFunctionPython;
};

using TestTypes = ::testing::Types<
#ifdef FWE_FEATURE_MICROPYTHON
    CustomFunctionMicroPython
#endif
#ifdef FWE_FEATURE_CPYTHON
#if defined( FWE_FEATURE_MICROPYTHON )
    ,
#endif
    CustomFunctionCPython
#endif
    >;

class CustomFunctionPythonNameGenerator
{
public:
    template <typename T>
    static std::string
    GetName( int unused )
    {
        static_cast<void>( unused );
#ifdef FWE_FEATURE_MICROPYTHON
        if ( std::is_same<T, CustomFunctionMicroPython>::value )
        {
            return "MicroPython";
        }
#endif
#ifdef FWE_FEATURE_CPYTHON
        if ( std::is_same<T, CustomFunctionCPython>::value )
        {
            return "CPython";
        }
#endif
        return "";
    }
};

TYPED_TEST_SUITE( CustomFunctionPythonTemplate, TestTypes, CustomFunctionPythonNameGenerator );

TYPED_TEST( CustomFunctionPythonTemplate, wrongArgs0 )
{
    std::vector<InspectionValue> args;
    std::unordered_set<SignalID> collectedSignalIds = { 1 };
    CollectionInspectionEngineOutput collectedData;
    collectedData.triggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();

    // Wrong number of arguments:
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::TYPE_MISMATCH );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );
}

TYPED_TEST( CustomFunctionPythonTemplate, wrongArgs1 )
{
    std::vector<InspectionValue> args;
    std::unordered_set<SignalID> collectedSignalIds = { 1 };
    CollectionInspectionEngineOutput collectedData;
    collectedData.triggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();

    EXPECT_CALL( *this->mTransferManagerWrapperMock,
                 MockedDownloadToDirectory(
                     this->mDownloadDirectory + "/script_0000000000000001", "dummy-bucket", "dummy-prefix" ) );

    // Triggers download:
    args.resize( 1 );
    args[0] = "dummy-prefix";
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    auto transferHandle = std::make_shared<Aws::Transfer::TransferHandle>(
        "dummy-bucket",
        "dummy-prefix/test_module.py",
        this->mDownloadDirectory + "/script_0000000000000001/test_module.py" );
    this->mScriptEngine->transferInitiatedCallback( transferHandle );
    boost::filesystem::copy_file( "test_module.py",
                                  this->mDownloadDirectory + "/script_0000000000000001/test_module.py" );
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::COMPLETED );
    this->mScriptEngine->transferStatusUpdatedCallback( transferHandle );

    // Wrong number of arguments:
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::TYPE_MISMATCH );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Set some non-existant invocation ID, should log error
    this->mScriptEngine->setStatus( 2, CustomFunctionScriptEngine::ScriptStatus::RUNNING );
}

TYPED_TEST( CustomFunctionPythonTemplate, alreadyDownloaded )
{
    std::vector<InspectionValue> args;
    std::unordered_set<SignalID> collectedSignalIds = { 1 };
    CollectionInspectionEngineOutput collectedData;
    collectedData.triggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();

    boost::filesystem::create_directories( this->mDownloadDirectory + "/script_0000000000000001" );
    boost::filesystem::copy_file( "test_module.py",
                                  this->mDownloadDirectory + "/script_0000000000000001/test_module.py" );
    {
        std::ofstream file( this->mDownloadDirectory + "/download_complete_0000000000000001" );
        file.close();
    }

    args.resize( 3 );
    args[0] = "dummy-prefix";
    args[1] = "test_module";
    args[2] = 2000;
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 1 );
    auto &signal = collectedData.triggeredCollectionSchemeData->signals[0];
    auto loanedFrame = this->mRawDataBufferManagerSpy.borrowFrame( signal.signalID, signal.value.value.uint32Val );
    ASSERT_FALSE( loanedFrame.isNull() );
    std::string collectedStringVal;
    collectedStringVal.assign( reinterpret_cast<const char *>( loanedFrame.getData() ), loanedFrame.getSize() );
    EXPECT_EQ( collectedStringVal, "4000.0" );

    // Shouldn't delete anything
    this->mScriptEngine->shutdown();
}

TYPED_TEST( CustomFunctionPythonTemplate, badScripts )
{
    std::vector<InspectionValue> args;
    std::unordered_set<SignalID> collectedSignalIds = { 1 };
    CollectionInspectionEngineOutput collectedData;
    collectedData.triggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();

    // Bad import
    boost::filesystem::create_directories( this->mDownloadDirectory + "/script_0000000000000001" );
    {
        std::ofstream file( this->mDownloadDirectory + "/script_0000000000000001/test_module.py" );
        file << "import non_existant_module\n";
        file.close();
    }
    {
        std::ofstream file( this->mDownloadDirectory + "/download_complete_0000000000000001" );
        file.close();
    }

    // No invoke function
    boost::filesystem::create_directories( this->mDownloadDirectory + "/script_0000000000000002" );
    {
        std::ofstream file( this->mDownloadDirectory + "/script_0000000000000002/test_module.py" );
        file << "print('hello')";
        file.close();
    }
    {
        std::ofstream file( this->mDownloadDirectory + "/download_complete_0000000000000002" );
        file.close();
    }

    args.resize( 3 );
    args[0] = "dummy-prefix";
    args[1] = "test_module";
    args[2] = 2000;
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::TYPE_MISMATCH );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    ASSERT_EQ( this->mCustomFunctionPython->invoke( 2, args ).error, ExpressionErrorCode::TYPE_MISMATCH );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );
}

TYPED_TEST( CustomFunctionPythonTemplate, downloadError )
{
    std::vector<InspectionValue> args;
    std::unordered_set<SignalID> collectedSignalIds = { 1 };
    CollectionInspectionEngineOutput collectedData;
    collectedData.triggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();

    EXPECT_CALL( *this->mTransferManagerWrapperMock,
                 MockedDownloadToDirectory(
                     this->mDownloadDirectory + "/script_0000000000000001", "dummy-bucket", "dummy-prefix" ) );

    // Triggers download:
    args.resize( 3 );
    args[0] = "dummy-prefix";
    args[1] = "test_module";
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Erroneous directory name
    auto badTransferHandle = std::make_shared<Aws::Transfer::TransferHandle>(
        "dummy-bucket", "dummy-prefix/test_module.py", this->mDownloadDirectory + "/XXX" );
    badTransferHandle->UpdateStatus( Aws::Transfer::TransferStatus::COMPLETED );
    this->mScriptEngine->transferInitiatedCallback( badTransferHandle );
    this->mScriptEngine->transferStatusUpdatedCallback( badTransferHandle );
    this->mScriptEngine->transferErrorCallback( badTransferHandle, Aws::Client::AWSError<Aws::S3::S3Errors>() );

    auto otherTransferHandle = std::make_shared<Aws::Transfer::TransferHandle>(
        "dummy-bucket",
        "dummy-prefix/test_module.py",
        this->mDownloadDirectory + "/script_0000000000000002/test_module.py" );
    otherTransferHandle->UpdateStatus( Aws::Transfer::TransferStatus::COMPLETED );
    this->mScriptEngine->transferInitiatedCallback( otherTransferHandle );
    this->mScriptEngine->transferStatusUpdatedCallback( otherTransferHandle );
    this->mScriptEngine->transferErrorCallback( otherTransferHandle, Aws::Client::AWSError<Aws::S3::S3Errors>() );

    auto transferHandle = std::make_shared<Aws::Transfer::TransferHandle>(
        "dummy-bucket",
        "dummy-prefix/test_module.py",
        this->mDownloadDirectory + "/script_0000000000000001/test_module.py" );
    this->mScriptEngine->transferInitiatedCallback( transferHandle );
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::NOT_STARTED );
    this->mScriptEngine->transferStatusUpdatedCallback( transferHandle );
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::IN_PROGRESS );
    this->mScriptEngine->transferStatusUpdatedCallback( transferHandle );
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::ABORTED );
    this->mScriptEngine->transferStatusUpdatedCallback( transferHandle );
    this->mScriptEngine->transferErrorCallback( transferHandle, Aws::Client::AWSError<Aws::S3::S3Errors>() );
}

TYPED_TEST( CustomFunctionPythonTemplate, zipped )
{
    std::vector<InspectionValue> args;
    std::unordered_set<SignalID> collectedSignalIds = { 1 };
    CollectionInspectionEngineOutput collectedData;
    collectedData.triggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();

    auto transferHandle = std::make_shared<Aws::Transfer::TransferHandle>(
        "dummy-bucket", "dummy-prefix.tar.gz", this->mDownloadDirectory + "/script_0000000000000001.tar.gz" );
    EXPECT_CALL( *this->mTransferManagerWrapperMock,
                 MockedDownloadFile( "dummy-bucket",
                                     "dummy-prefix.tar.gz",
                                     this->mDownloadDirectory + "/script_0000000000000001.tar.gz",
                                     _,
                                     _ ) )
        .WillOnce( Return( transferHandle ) );
    EXPECT_CALL( *this->mTransferManagerWrapperMock, CancelAll() );
    EXPECT_CALL( *this->mTransferManagerWrapperMock, MockedWaitUntilAllFinished( _ ) );

    // Triggers download:
    args.resize( 3 );
    args[0] = "dummy-prefix.tar.gz";
    args[1] = "test_module";
    args[2] = 2000;
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    ASSERT_EQ(
        system( ( "tar -zcf " + this->mDownloadDirectory + "/script_0000000000000001.tar.gz test_module.py" ).c_str() ),
        0 );

    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::COMPLETED );
    this->mScriptEngine->transferStatusUpdatedCallback( transferHandle );

    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 1 );
    auto &signal = collectedData.triggeredCollectionSchemeData->signals[0];
    auto loanedFrame = this->mRawDataBufferManagerSpy.borrowFrame( signal.signalID, signal.value.value.uint32Val );
    ASSERT_FALSE( loanedFrame.isNull() );
    std::string collectedStringVal;
    collectedStringVal.assign( reinterpret_cast<const char *>( loanedFrame.getData() ), loanedFrame.getSize() );
    EXPECT_EQ( collectedStringVal, "4000.0" );

    // Should just delete the zipped file
    this->mScriptEngine->shutdown();
}

TYPED_TEST( CustomFunctionPythonTemplate, runScript )
{
    std::vector<InspectionValue> args;
    std::unordered_set<SignalID> collectedSignalIds = { 1 };
    CollectionInspectionEngineOutput collectedData;
    collectedData.triggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();

    EXPECT_CALL( *this->mTransferManagerWrapperMock,
                 MockedDownloadToDirectory(
                     this->mDownloadDirectory + "/script_0000000000000001", "dummy-bucket", "dummy-prefix" ) );

    // Triggers download:
    args.resize( 3 );
    args[0] = "dummy-prefix";
    args[1] = "test_module";
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    auto transferHandle = std::make_shared<Aws::Transfer::TransferHandle>(
        "dummy-bucket",
        "dummy-prefix/test_module.py",
        this->mDownloadDirectory + "/script_0000000000000001/test_module.py" );
    this->mScriptEngine->transferInitiatedCallback( transferHandle );
    boost::filesystem::copy_file( "test_module.py",
                                  this->mDownloadDirectory + "/script_0000000000000001/test_module.py" );
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::COMPLETED );
    this->mScriptEngine->transferStatusUpdatedCallback( transferHandle );

    // Next invoke passes None
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Next invoke passes bool
    args[2] = true;
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Next invoke passes string
    args[2] = "abcd";
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Next invoke passes 444 which triggers, but the collected signal is unknown
    args[2] = 444;
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Next invoke passes 555 which returns 0.0
    args[2] = 555;
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Next invoke passes value is less than 1000, which returns False and not triggered:
    args[2] = 123;
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Next invoke passes value is greater than 1000, which returns True and triggers, but collection is not active:
    args[2] = 1234.56;
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    collectedData.triggeredCollectionSchemeData = nullptr;
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    collectedData.triggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();

    // Next invoke passes value is greater than 1000, which returns True and triggers, but signal is not collected:
    args[2] = 1234.56;
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    collectedSignalIds = { 2 };
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    collectedSignalIds = { 1 };

    // Next invoke passes value is greater than 1000, which returns True and triggered:
    args[2] = 1234.56;
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 1 );
    auto &signal = collectedData.triggeredCollectionSchemeData->signals[0];
    auto loanedFrame = this->mRawDataBufferManagerSpy.borrowFrame( signal.signalID, signal.value.value.uint32Val );
    ASSERT_FALSE( loanedFrame.isNull() );
    std::string collectedStringVal;
    collectedStringVal.assign( reinterpret_cast<const char *>( loanedFrame.getData() ), loanedFrame.getSize() );
    EXPECT_EQ( collectedStringVal, "2469.12" );

    // Next invoke triggers an exception:
    args[2] = 666;
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::TYPE_MISMATCH );
    collectedData.triggeredCollectionSchemeData->signals.clear();
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Once in error state, invoke always returns TYPE_MISMATCH
    args[2] = 123;
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::TYPE_MISMATCH );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Reset error
    this->mScriptEngine->setStatus( 1, CustomFunctionScriptEngine::ScriptStatus::RUNNING );

    // Next invoke returns a tuple of size 1
    args[2] = 777;
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::TYPE_MISMATCH );
    collectedData.triggeredCollectionSchemeData->signals.clear();
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Reset error
    this->mScriptEngine->setStatus( 1, CustomFunctionScriptEngine::ScriptStatus::RUNNING );

    // Next invoke returns collected data not as a dict
    args[2] = 888;
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::TYPE_MISMATCH );
    collectedData.triggeredCollectionSchemeData->signals.clear();
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );
}

TYPED_TEST( CustomFunctionPythonTemplate, badCleanup )
{
    std::vector<InspectionValue> args;
    std::unordered_set<SignalID> collectedSignalIds = { 1 };
    CollectionInspectionEngineOutput collectedData;
    collectedData.triggeredCollectionSchemeData = std::make_shared<TriggeredCollectionSchemeData>();

    EXPECT_CALL( *this->mTransferManagerWrapperMock,
                 MockedDownloadToDirectory(
                     this->mDownloadDirectory + "/script_0000000000000001", "dummy-bucket", "dummy-prefix" ) );
    EXPECT_CALL( *this->mTransferManagerWrapperMock, CancelAll() );
    EXPECT_CALL( *this->mTransferManagerWrapperMock, MockedWaitUntilAllFinished( _ ) );

    // Triggers download:
    args.resize( 3 );
    args[0] = "dummy-prefix";
    args[1] = "test_module";
    args[2] = 999;
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    auto transferHandle = std::make_shared<Aws::Transfer::TransferHandle>(
        "dummy-bucket",
        "dummy-prefix/test_module.py",
        this->mDownloadDirectory + "/script_0000000000000001/test_module.py" );
    this->mScriptEngine->transferInitiatedCallback( transferHandle );
    boost::filesystem::copy_file( "test_module.py",
                                  this->mDownloadDirectory + "/script_0000000000000001/test_module.py" );
    transferHandle->UpdateStatus( Aws::Transfer::TransferStatus::COMPLETED );
    this->mScriptEngine->transferStatusUpdatedCallback( transferHandle );

    // Next invoke sets the bad_cleanup flag
    ASSERT_EQ( this->mCustomFunctionPython->invoke( 1, args ).error, ExpressionErrorCode::SUCCESSFUL );
    this->mScriptEngine->conditionEnd( collectedSignalIds, 0, collectedData );
    ASSERT_EQ( collectedData.triggeredCollectionSchemeData->signals.size(), 0 );

    // Cleanup twice, the second call should be ignored
    this->mCustomFunctionPython->cleanup( 1 );
    this->mCustomFunctionPython->cleanup( 1 );

    // Shutdown delete all, since invocation already cleaned up
    this->mScriptEngine->shutdown();
}

} // namespace IoTFleetWise
} // namespace Aws
