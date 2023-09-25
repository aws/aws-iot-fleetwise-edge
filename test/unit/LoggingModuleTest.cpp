
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "LoggingModule.h"
#include <cstdio>
#include <gtest/gtest.h>

namespace Aws
{
namespace IoTFleetWise
{

TEST( LoggingModuleTest, GetErrnoString )
{
    // Try to open a non existing file
    FILE *fp = fopen( "/tmp/fwe_strerror_test/2f1d6e5e5ab1", "rb" );
    (void)fp;
    ASSERT_NE( getErrnoString(), "" );
}

} // namespace IoTFleetWise
} // namespace Aws
