// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aws/iotfleetwise/CollectionInspectionAPITypes.h>
#include <aws/iotfleetwise/NamedSignalDataSource.h>
#include <aws/iotfleetwise/SignalTypes.h>
#include <aws/iotfleetwise/TimeTypes.h>
#include <memory>
#include <unordered_set>
#include <vector>

class CustomFunctionFileSize
{
public:
    CustomFunctionFileSize( std::shared_ptr<Aws::IoTFleetWise::NamedSignalDataSource> namedSignalDataSource );
    Aws::IoTFleetWise::CustomFunctionInvokeResult invoke( Aws::IoTFleetWise::CustomFunctionInvocationID invocationId,
                                                          const std::vector<Aws::IoTFleetWise::InspectionValue> &args );
    void conditionEnd( const std::unordered_set<Aws::IoTFleetWise::SignalID> &collectedSignalIds,
                       Aws::IoTFleetWise::Timestamp timestamp,
                       Aws::IoTFleetWise::CollectionInspectionEngineOutput &output );

private:
    std::shared_ptr<Aws::IoTFleetWise::NamedSignalDataSource> mNamedSignalDataSource;
    int mFileSize{ -1 };
};
