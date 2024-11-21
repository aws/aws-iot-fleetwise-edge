// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CustomFunctionMath.h"
#include "CollectionInspectionAPITypes.h"
#include <gtest/gtest.h>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

TEST( CustomFunctionMathTest, abs )
{
    std::vector<InspectionValue> args;
    ASSERT_EQ( CustomFunctionMath::absFunc( 0, args ).error, ExpressionErrorCode::TYPE_MISMATCH );

    args.resize( 1 );
    auto result = CustomFunctionMath::absFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isUndefined() );

    args[0] = -1;
    result = CustomFunctionMath::absFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isBoolOrDouble() );
    ASSERT_EQ( result.value.asDouble(), 1.0 );
}

TEST( CustomFunctionMathTest, min )
{
    std::vector<InspectionValue> args;
    ASSERT_EQ( CustomFunctionMath::minFunc( 0, args ).error, ExpressionErrorCode::TYPE_MISMATCH );

    args.resize( 2 );
    auto result = CustomFunctionMath::minFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isUndefined() );

    args[0] = 1;
    args[1] = 2;
    result = CustomFunctionMath::minFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isBoolOrDouble() );
    ASSERT_EQ( result.value.asDouble(), 1.0 );
}

TEST( CustomFunctionMathTest, max )
{
    std::vector<InspectionValue> args;
    ASSERT_EQ( CustomFunctionMath::maxFunc( 0, args ).error, ExpressionErrorCode::TYPE_MISMATCH );

    args.resize( 2 );
    auto result = CustomFunctionMath::maxFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isUndefined() );

    args[0] = 1;
    args[1] = 2;
    result = CustomFunctionMath::maxFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isBoolOrDouble() );
    ASSERT_EQ( result.value.asDouble(), 2.0 );
}

TEST( CustomFunctionMathTest, pow )
{
    std::vector<InspectionValue> args;
    ASSERT_EQ( CustomFunctionMath::powFunc( 0, args ).error, ExpressionErrorCode::TYPE_MISMATCH );

    args.resize( 2 );
    auto result = CustomFunctionMath::powFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isUndefined() );

    // Square-root of -1:
    args[0] = -1;
    args[1] = 0.5;
    result = CustomFunctionMath::powFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isUndefined() );

    args[0] = 2;
    args[1] = 10;
    result = CustomFunctionMath::powFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isBoolOrDouble() );
    ASSERT_EQ( result.value.asDouble(), 1024.0 );
}

TEST( CustomFunctionMathTest, log )
{
    std::vector<InspectionValue> args;
    ASSERT_EQ( CustomFunctionMath::logFunc( 0, args ).error, ExpressionErrorCode::TYPE_MISMATCH );

    args.resize( 2 );
    auto result = CustomFunctionMath::logFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isUndefined() );

    args[0] = -1;
    args[1] = 100;
    result = CustomFunctionMath::logFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isUndefined() );

    args[0] = 10;
    args[1] = -1;
    result = CustomFunctionMath::logFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isUndefined() );

    args[0] = 10;
    args[1] = 100;
    result = CustomFunctionMath::logFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isBoolOrDouble() );
    ASSERT_EQ( result.value.asDouble(), 2.0 );
}

TEST( CustomFunctionMathTest, ceil )
{
    std::vector<InspectionValue> args;
    ASSERT_EQ( CustomFunctionMath::ceilFunc( 0, args ).error, ExpressionErrorCode::TYPE_MISMATCH );

    args.resize( 1 );
    auto result = CustomFunctionMath::ceilFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isUndefined() );

    args[0] = 0.001;
    result = CustomFunctionMath::ceilFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isBoolOrDouble() );
    ASSERT_EQ( result.value.asDouble(), 1.0 );
}

TEST( CustomFunctionMathTest, floor )
{
    std::vector<InspectionValue> args;
    ASSERT_EQ( CustomFunctionMath::floorFunc( 0, args ).error, ExpressionErrorCode::TYPE_MISMATCH );

    args.resize( 1 );
    auto result = CustomFunctionMath::floorFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isUndefined() );

    args[0] = 1.999;
    result = CustomFunctionMath::floorFunc( 0, args );
    ASSERT_EQ( result.error, ExpressionErrorCode::SUCCESSFUL );
    ASSERT_TRUE( result.value.isBoolOrDouble() );
    ASSERT_EQ( result.value.asDouble(), 1.0 );
}

} // namespace IoTFleetWise
} // namespace Aws
