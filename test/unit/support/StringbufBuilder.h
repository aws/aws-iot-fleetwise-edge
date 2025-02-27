// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/StreambufBuilder.h"
#include <gmock/gmock.h>
#include <memory>
#include <streambuf>
#include <string>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{
namespace Testing
{

class StringbufBuilder : public StreambufBuilder
{
public:
    StringbufBuilder( std::string data )
        : mStreambuf( std::make_unique<std::stringbuf>( std::move( data ) ) )
    {
    }

    StringbufBuilder( std::unique_ptr<std::streambuf> streambuf )
        : mStreambuf( std::move( streambuf ) )
    {
    }

    std::unique_ptr<std::streambuf>
    build() override
    {
        return std::move( mStreambuf );
    }

private:
    std::unique_ptr<std::streambuf> mStreambuf;
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
