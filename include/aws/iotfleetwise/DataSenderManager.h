// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/PayloadManager.h"
#include <memory>
#include <unordered_map>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Class that implements data sender logic: data preprocessing and upload
 *
 * This class is not thread-safe so the caller needs to ensure that the different functions
 * are called only from one thread. This class will be instantiated and used from the Data Sender
 * Manager Worker thread
 */
class DataSenderManager
{

public:
    DataSenderManager( const std::unordered_map<SenderDataType, std::unique_ptr<DataSender>> &dataSenders,
                       PayloadManager *payloadManager );

    virtual ~DataSenderManager() = default;

    /**
     * @brief Process collection scheme parameters and prepare data for upload
     */
    virtual void processData( const DataToSend &data );

    /**
     * @brief Retrieve all the persisted data and hand it over to the correct sender
     */
    virtual void checkAndSendRetrievedData();

private:
    const std::unordered_map<SenderDataType, std::unique_ptr<DataSender>> &mDataSenders;
    PayloadManager *mPayloadManager;
};

} // namespace IoTFleetWise
} // namespace Aws
