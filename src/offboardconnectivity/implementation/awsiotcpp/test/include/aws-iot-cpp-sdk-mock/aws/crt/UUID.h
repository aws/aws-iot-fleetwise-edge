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