/**
 * Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
 * Licensed under the Amazon Software License (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 * http://aws.amazon.com/asl/
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
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
    SensorArtifactMetadata()
        : sourceID( 0 )
        , path( "" )
    {
    }
    uint32_t sourceID;
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