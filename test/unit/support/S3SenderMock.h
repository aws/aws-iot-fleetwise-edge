// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/S3Sender.h"
#include <functional>
#include <gmock/gmock.h>
#include <memory>
#include <streambuf>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{
namespace Testing
{

class S3SenderMock : public S3Sender
{
public:
    S3SenderMock()
        : S3Sender(
              []( Aws::Client::ClientConfiguration &,
                  Aws::Transfer::TransferManagerConfiguration & ) -> std::shared_ptr<TransferManagerWrapper> {
                  return nullptr;
              },
              0,
              3000 )
    {
    }

    MOCK_METHOD( void,
                 sendStream,
                 ( std::unique_ptr<StreambufBuilder> streambufBuilder,
                   const S3UploadMetadata &uploadMetadata,
                   const std::string &objectKey,
                   ResultCallback resultCallback ),
                 ( override ) );
};

} // namespace Testing
} // namespace IoTFleetWise
} // namespace Aws
