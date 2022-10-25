// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace VehicleNetwork
{

struct SensorArtifactMetadata
{
    uint32_t sourceID{ 0 };
    std::string path;
};

class SensorDataListener
{

public:
    virtual ~SensorDataListener() = default;

    /**
     * @brief This notification is intended to inform listeners that a Sensor Artifact
     * has been made available to the system. The location of the artifact and its
     * data format are specified in the Metadata Object. This is a fire and forget
     * notification.
     * @param artifactMetadata consiting of ( initially ):
     *   - path : path of the artifact in the file system
     *   - further other metadata can be added as we progress on dev.
     */
    virtual void onSensorArtifactAvailable( const SensorArtifactMetadata &artifactMetadata ) = 0;
};
} // namespace VehicleNetwork
} // namespace IoTFleetWise
} // namespace Aws