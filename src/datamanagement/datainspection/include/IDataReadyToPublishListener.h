// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{

class IDataReadyToPublishListener
{
public:
    virtual ~IDataReadyToPublishListener() = default;

    /**
     * @brief New data in CollectedDataReadyToPublish queue that can be published to cloud
     */
    virtual void onDataReadyToPublish() = 0;
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws