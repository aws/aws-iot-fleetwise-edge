// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "DataSenderTypes.h"
#include "ISender.h"
#include "PayloadManager.h"
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
    DataSenderManager( std::unordered_map<SenderDataType, std::shared_ptr<DataSender>> dataSenders,
                       std::shared_ptr<ISender> mqttSender,
                       std::shared_ptr<PayloadManager> payloadManager );

    virtual ~DataSenderManager() = default;

    /**
     * @brief Process collection scheme parameters and prepare data for upload
     */
    virtual void processData( std::shared_ptr<const DataToSend> data );

    /**
     * @brief Retrieve all the persisted data and hand it over to the correct sender
     */
    virtual void checkAndSendRetrievedData();

private:
    std::unordered_map<SenderDataType, std::shared_ptr<DataSender>> mDataSenders;
    std::shared_ptr<ISender> mMQTTSender;
    std::shared_ptr<PayloadManager> mPayloadManager;
};

} // namespace IoTFleetWise
} // namespace Aws
