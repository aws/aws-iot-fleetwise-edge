// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "LastKnownStateTypes.h"
#include "state_templates.pb.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Setting a LastKnownState proto byte size limit for file received from Cloud
 */
constexpr size_t LAST_KNOWN_STATE_BYTE_SIZE_LIMIT = 128000000;

class LastKnownStateIngestion
{
public:
    LastKnownStateIngestion();

    virtual ~LastKnownStateIngestion();

    LastKnownStateIngestion( const LastKnownStateIngestion & ) = delete;
    LastKnownStateIngestion &operator=( const LastKnownStateIngestion & ) = delete;
    LastKnownStateIngestion( LastKnownStateIngestion && ) = delete;
    LastKnownStateIngestion &operator=( LastKnownStateIngestion && ) = delete;

    bool isReady() const;

    virtual bool build();

    virtual std::shared_ptr<const StateTemplatesDiff> getStateTemplatesDiff() const;

    bool copyData( const std::uint8_t *inputBuffer, const size_t size );

    virtual inline const std::vector<uint8_t> &
    getData() const
    {
        return mProtoBinaryData;
    }

private:
    /**
     * @brief The protobuf message that will hold the deserialized proto.
     */
    Schemas::LastKnownState::StateTemplates mProtoStateTemplates;

    /**
     * @brief This vector will store the binary data copied from the IReceiver callback.
     */
    std::vector<uint8_t> mProtoBinaryData;

    /**
     * @brief Flag which is true if proto binary data is processed into readable data structures.
     */
    bool mReady{ false };

    /**
     * @brief List of structs containing information about state templates
     */
    std::shared_ptr<StateTemplatesDiff> mStateTemplatesDiff;
};

} // namespace IoTFleetWise
} // namespace Aws
