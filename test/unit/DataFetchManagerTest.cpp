// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataFetchManager.h"
#include "CollectionInspectionAPITypes.h"
#include "DataFetchManagerAPITypes.h"
#include "SignalTypes.h"
#include "WaitUntil.h"
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
        mDataFetchManager = std::make_unique<DataFetchManager>();
    }

    void
    TearDown() override
    {
        mDataFetchManager.reset();
    }

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

    mDataFetchManager->onFetchRequest( 1, true );
    ASSERT_FALSE( functionCalled );
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

    mDataFetchManager->onFetchRequest( 1, true );

    ASSERT_FALSE( functionCalled );
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
    mDataFetchManager->onFetchRequest( 1, true );

    ASSERT_FALSE( functionCalled );
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

    mDataFetchManager->onFetchRequest( 2, true );

    ASSERT_FALSE( functionCalled );
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

    mDataFetchManager->onFetchRequest( 1, true );

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
        100, 3, 1000 }; // execute every 100ms, max 3 executions, reset every 1000ms
    mDataFetchManager->onChangeFetchMatrix( fetchMatrix );
    // TODO: max executions and reset interval parameters are not yet supported by the cloud and are ignored on edge
    // Expect function calls every 100ms
    std::this_thread::sleep_for( std::chrono::milliseconds( 450 ) );
    ASSERT_EQ( callCount, 5 );
    mDataFetchManager->stop();
}

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
