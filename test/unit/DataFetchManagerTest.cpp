// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws/iotfleetwise/DataFetchManager.h"
#include "WaitUntil.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/DataFetchManagerAPITypes.h"
#include "aws/iotfleetwise/QueueTypes.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include <chrono>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace Testing
{

class DataFetchManagerTest : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        mTestFetchQueue = std::make_shared<FetchRequestQueue>( 1000, "Test Fetch Request Queue" );
        mDataFetchManager = std::make_unique<DataFetchManager>( mTestFetchQueue );
    }

    void
    TearDown() override
    {
        mTestFetchQueue.reset();
        mDataFetchManager.reset();
    }

    std::shared_ptr<FetchRequestQueue> mTestFetchQueue;
    std::unique_ptr<DataFetchManager> mDataFetchManager;
};

TEST_F( DataFetchManagerTest, TestNoFetchMatrix )
{
    mDataFetchManager->start();

    bool functionCalled = false;
    mDataFetchManager->registerCustomFetchFunction(
        "testFunction", [&functionCalled]( SignalID, FetchRequestID, const std::vector<InspectionValue> & ) {
            functionCalled = true;
            return FetchErrorCode::SUCCESSFUL;
        } );

    ASSERT_TRUE( mTestFetchQueue->push( 1 ) );
    WAIT_ASSERT_FALSE( functionCalled );
    mDataFetchManager->stop();
}

TEST_F( DataFetchManagerTest, TestUnknownFetchFunction )
{
    mDataFetchManager->start();

    bool functionCalled = false;
    mDataFetchManager->registerCustomFetchFunction(
        "testFunction", [&functionCalled]( SignalID, FetchRequestID, const std::vector<InspectionValue> & ) {
            functionCalled = true;
            return FetchErrorCode::SUCCESSFUL;
        } );

    auto fetchMatrix = std::make_shared<FetchMatrix>();
    std::vector<FetchRequest> &fetchRequests =
        fetchMatrix->fetchRequests.emplace( 1, std::vector<FetchRequest>() ).first->second;

    fetchRequests.emplace_back();
    auto &fetchRequest = fetchRequests.back();
    fetchRequest.signalID = 1;
    fetchRequest.functionName = "unknownFunction";
    fetchRequest.args.emplace_back();
    mDataFetchManager->onChangeFetchMatrix( fetchMatrix );

    ASSERT_TRUE( mTestFetchQueue->push( 1 ) );
    WAIT_ASSERT_FALSE( functionCalled );
    mDataFetchManager->stop();
}

TEST_F( DataFetchManagerTest, TestNoActionsSet )
{
    mDataFetchManager->start();

    bool functionCalled = false;
    mDataFetchManager->registerCustomFetchFunction(
        "testFunction", [&functionCalled]( SignalID, FetchRequestID, const std::vector<InspectionValue> & ) {
            functionCalled = true;
            return FetchErrorCode::SUCCESSFUL;
        } );

    auto fetchMatrix = std::make_shared<FetchMatrix>();
    fetchMatrix->fetchRequests.emplace( 1, std::vector<FetchRequest>() ).first->second;

    mDataFetchManager->onChangeFetchMatrix( fetchMatrix );
    ASSERT_TRUE( mTestFetchQueue->push( 1 ) );
    WAIT_ASSERT_FALSE( functionCalled );
    mDataFetchManager->stop();
}

TEST_F( DataFetchManagerTest, TestUnknownFetchRequestID )
{
    mDataFetchManager->start();

    bool functionCalled = false;
    mDataFetchManager->registerCustomFetchFunction(
        "testFunction", [&functionCalled]( SignalID, FetchRequestID, const std::vector<InspectionValue> & ) {
            functionCalled = true;
            return FetchErrorCode::SUCCESSFUL;
        } );

    auto fetchMatrix = std::make_shared<FetchMatrix>();
    std::vector<FetchRequest> &fetchRequests =
        fetchMatrix->fetchRequests.emplace( 1, std::vector<FetchRequest>() ).first->second;
    fetchRequests.emplace_back();
    auto &fetchRequest = fetchRequests.back();
    fetchRequest.signalID = 1;
    fetchRequest.functionName = "testFunction";
    fetchRequest.args.emplace_back();
    mDataFetchManager->onChangeFetchMatrix( fetchMatrix );

    ASSERT_TRUE( mTestFetchQueue->push( 2 ) );
    WAIT_ASSERT_FALSE( functionCalled );
    mDataFetchManager->stop();
}

TEST_F( DataFetchManagerTest, TestRegisterCustomFetchFunction )
{
    mDataFetchManager->start();

    bool functionCalled = false;
    mDataFetchManager->registerCustomFetchFunction(
        "testFunction", [&functionCalled]( SignalID, FetchRequestID, const std::vector<InspectionValue> & ) {
            functionCalled = true;
            return FetchErrorCode::SUCCESSFUL;
        } );

    auto fetchMatrix = std::make_shared<FetchMatrix>();
    std::vector<FetchRequest> &fetchRequests =
        fetchMatrix->fetchRequests.emplace( 1, std::vector<FetchRequest>() ).first->second;
    fetchRequests.emplace_back();
    auto &fetchRequest = fetchRequests.back();
    fetchRequest.signalID = 1;
    fetchRequest.functionName = "testFunction";
    fetchRequest.args.emplace_back();
    mDataFetchManager->onChangeFetchMatrix( fetchMatrix );

    ASSERT_TRUE( mTestFetchQueue->push( 1 ) );
    WAIT_ASSERT_TRUE( functionCalled );
    mDataFetchManager->stop();
}

TEST_F( DataFetchManagerTest, TestPeriodicFetchRequest )
{
    mDataFetchManager->start();

    int callCount = 0;
    mDataFetchManager->registerCustomFetchFunction(
        "testFunction", [&callCount]( SignalID, FetchRequestID, const std::vector<InspectionValue> & ) {
            callCount++;
            return FetchErrorCode::SUCCESSFUL;
        } );
    auto fetchMatrix = std::make_shared<FetchMatrix>();
    std::vector<FetchRequest> &fetchRequests =
        fetchMatrix->fetchRequests.emplace( 1, std::vector<FetchRequest>() ).first->second;
    fetchRequests.emplace_back();
    auto &fetchRequest = fetchRequests.back();
    fetchRequest.signalID = 1;
    fetchRequest.functionName = "testFunction";
    fetchRequest.args.emplace_back();
    fetchMatrix->periodicalFetchRequestSetup[1] = {
        500, 3, 5000 }; // execute every 500ms, max 3 executions, reset every 5000ms
    mDataFetchManager->onChangeFetchMatrix( fetchMatrix );
    // TODO: max executions and reset interval parameters are not yet supported by the cloud and are ignored on edge
    // Expect function calls every 500ms
    std::this_thread::sleep_for( std::chrono::milliseconds( 2250 ) );
    ASSERT_EQ( callCount, 5 );
    mDataFetchManager->stop();
}

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
