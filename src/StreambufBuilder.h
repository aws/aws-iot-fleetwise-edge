// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <streambuf>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief A wrapper for deferring the creation of a streambuf until the moment it is really needed
 **/
class StreambufBuilder
{
public:
    virtual ~StreambufBuilder() = default;

    virtual std::unique_ptr<std::streambuf> build() = 0;
};

} // namespace IoTFleetWise
} // namespace Aws
