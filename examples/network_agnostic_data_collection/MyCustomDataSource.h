// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <atomic>
#include <aws/iotfleetwise/Clock.h>
#include <aws/iotfleetwise/ClockHandler.h>
#include <aws/iotfleetwise/CollectionInspectionAPITypes.h>
#include <aws/iotfleetwise/IDecoderDictionary.h>
#include <aws/iotfleetwise/SignalTypes.h>
#include <aws/iotfleetwise/VehicleDataSourceTypes.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// In this example the signal decoder string is treated as a CSV with delimiter ':' containing 3
// uint32_t values being: a key (e.g. message ID) and two additional params (e.g. byte offset and
// scaling factor of the signal in the message). You could however interpret the decoder string in a
// different format according to your own custom data source requirements.
struct MyCustomDecodingInfo
{
    uint32_t exampleParam1ByteOffset;
    uint32_t exampleParam2ScalingFactor;
    Aws::IoTFleetWise::SignalID signalId;
};

class MyCustomDataSource
{
public:
    /**
     * @param interfaceId Interface identifier
     * @param signalBufferDistributor Signal buffer distributor
     */
    MyCustomDataSource( Aws::IoTFleetWise::InterfaceID interfaceId,
                        Aws::IoTFleetWise::SignalBufferDistributor &signalBufferDistributor );
    ~MyCustomDataSource();

    MyCustomDataSource( const MyCustomDataSource & ) = delete;
    MyCustomDataSource &operator=( const MyCustomDataSource & ) = delete;
    MyCustomDataSource( MyCustomDataSource && ) = delete;
    MyCustomDataSource &operator=( MyCustomDataSource && ) = delete;

    void onChangeOfActiveDictionary( Aws::IoTFleetWise::ConstDecoderDictionaryConstPtr &dictionary,
                                     Aws::IoTFleetWise::VehicleDataSourceProtocol networkProtocol );

private:
    Aws::IoTFleetWise::InterfaceID mInterfaceId;
    Aws::IoTFleetWise::SignalBufferDistributor &mSignalBufferDistributor;
    std::unordered_map<Aws::IoTFleetWise::SignalID, Aws::IoTFleetWise::SignalType> mSignalIdToSignalType;
    std::shared_ptr<const Aws::IoTFleetWise::Clock> mClock = Aws::IoTFleetWise::ClockHandler::getClock();
    std::mutex mDecoderDictionaryUpdateMutex;
    std::unordered_map<uint32_t, MyCustomDecodingInfo> mDecodingInfoTable;
    std::thread mThread;
    std::atomic_bool mThreadShouldStop{ false };

    static bool popU32FromString( std::string &decoder, uint32_t &val );

    bool requestSignalValue( uint32_t key, const MyCustomDecodingInfo &decoder, double &value );

    void registerReceptionCallback( std::function<void( const std::vector<uint8_t> data )> callback );
};
