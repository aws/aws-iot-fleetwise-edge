// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/CANInterfaceIDTranslator.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "vehicle_data.pb.h"
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

class DataSenderProtoReader
{
public:
    DataSenderProtoReader( CANInterfaceIDTranslator &canIDTranslator );
    ~DataSenderProtoReader();

    DataSenderProtoReader( const DataSenderProtoReader & ) = delete;
    DataSenderProtoReader &operator=( const DataSenderProtoReader & ) = delete;
    DataSenderProtoReader( DataSenderProtoReader && ) = delete;
    DataSenderProtoReader &operator=( DataSenderProtoReader && ) = delete;

    bool setupVehicleData( const std::string &data );
    bool deserializeVehicleData( TriggeredCollectionSchemeData &out );

private:
    Schemas::VehicleDataMsg::VehicleData mVehicleData{};
    CANInterfaceIDTranslator mIDTranslator;
};

} // namespace IoTFleetWise
} // namespace Aws
