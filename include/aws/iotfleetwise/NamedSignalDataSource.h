// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionInspectionAPITypes.h"
#include "aws/iotfleetwise/IDecoderDictionary.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include "aws/iotfleetwise/VehicleDataSourceTypes.h"
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Named signal data source. This data source uses Custom Signal Decoding, where the decoder is the name of the
 * signal, which can for example be the fully-qualified name of the signal from the signal catalog.
 */
class NamedSignalDataSource
{
public:
    /** @brief Construct named signal data source
     * @param interfaceId Interface identifier
     * @param signalBufferDistributor Signal buffer distributor */
    NamedSignalDataSource( InterfaceID interfaceId, SignalBufferDistributor &signalBufferDistributor );
    ~NamedSignalDataSource() = default;

    NamedSignalDataSource( const NamedSignalDataSource & ) = delete;
    NamedSignalDataSource &operator=( const NamedSignalDataSource & ) = delete;
    NamedSignalDataSource( NamedSignalDataSource && ) = delete;
    NamedSignalDataSource &operator=( NamedSignalDataSource && ) = delete;

    /** @brief Ingest signal value by name
     * @param timestamp Timestamp of signal value in milliseconds since epoch, or zero if unknown.
     * @param name Signal name
     * @param value Signal value
     * @param fetchRequestID contains fetch request IDs associated with the signal for the given campaign
     */
    void ingestSignalValue( Timestamp timestamp,
                            const std::string &name,
                            const DecodedSignalValue &value,
                            FetchRequestID fetchRequestID = DEFAULT_FETCH_REQUEST_ID );

    /** @brief Ingest multiple signal values by name
     * @param timestamp Timestamp of signal values in milliseconds since epoch, or zero if unknown.
     * @param values Signal values
     * @param fetchRequestID contains fetch request IDs associated with the signal for the given campaign
     */
    void ingestMultipleSignalValues( Timestamp timestamp,
                                     const std::vector<std::pair<std::string, DecodedSignalValue>> &values,
                                     FetchRequestID fetchRequestID = DEFAULT_FETCH_REQUEST_ID );

    void onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                     VehicleDataSourceProtocol networkProtocol );

    /** @brief Gets signal id for named signal from the decoder dictionary
     * @param name signal name
     * @return signal id
     */
    SignalID getNamedSignalID( const std::string &name );

private:
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    InterfaceID mInterfaceId;
    SignalBufferDistributor &mSignalBufferDistributor;
    std::mutex mDecoderDictMutex;
    std::shared_ptr<const CustomDecoderDictionary> mDecoderDictionary;
    Timestamp mLastTimestamp{};
};

} // namespace IoTFleetWise
} // namespace Aws
