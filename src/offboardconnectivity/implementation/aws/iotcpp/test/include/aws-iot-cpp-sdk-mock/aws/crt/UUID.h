// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Api.h"
#include "AwsIotSdkMock.h"
#include <string>
namespace Aws
{
namespace Crt
{

class UUID final
{
public:
    inline UUID() noexcept {};

    inline String
    ToString() const
    {
        return getSdkMock()->UUIDToString();
    }
};
} // namespace Crt
} // namespace Aws
