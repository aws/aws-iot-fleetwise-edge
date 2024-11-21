// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionInspectionAPITypes.h"
#include "IDecoderDictionary.h"
#include "SignalTypes.h"
#include "VehicleDataSourceTypes.h"
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

class AaosVhalSource
{
public:
    /**
     * @param interfaceId Interface identifier
     * @param signalBufferDistributor Signal buffer distributor
     */
    AaosVhalSource( InterfaceID interfaceId, SignalBufferDistributorPtr signalBufferDistributor );
    ~AaosVhalSource() = default;

    AaosVhalSource( const AaosVhalSource & ) = delete;
    AaosVhalSource &operator=( const AaosVhalSource & ) = delete;
    AaosVhalSource( AaosVhalSource && ) = delete;
    AaosVhalSource &operator=( AaosVhalSource && ) = delete;

    void onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                     VehicleDataSourceProtocol networkProtocol );

    /**
     * Returns a vector of vehicle property info
     *
     * @return Vehicle property info, with each member containing an array with 4 values:
     * - Vehicle property ID
     * - Area index
     * - Result index
     * - Signal ID
     */
    std::vector<std::array<uint32_t, 4>> getVehiclePropertyInfo();

    /**
     * Set Vehicle property value
     *
     * @param signalId Signal ID
     * @param value Property value
     */
    void setVehicleProperty( SignalID signalId, const DecodedSignalValue &value );

private:
    InterfaceID mInterfaceId;
    SignalBufferDistributorPtr mSignalBufferDistributor;
    std::unordered_map<SignalID, SignalType> mSignalIdToSignalType;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    std::mutex mDecoderDictionaryUpdateMutex;
    std::vector<std::array<uint32_t, 4>> mVehiclePropertyInfo;
};

} // namespace IoTFleetWise
} // namespace Aws
