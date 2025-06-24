// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aws/iotfleetwise/CollectionInspectionAPITypes.h>
#include <vector>

Aws::IoTFleetWise::CustomFunctionInvokeResult customFunctionSin(
    Aws::IoTFleetWise::CustomFunctionInvocationID invocationId,
    const std::vector<Aws::IoTFleetWise::InspectionValue> &args );
