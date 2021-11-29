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

#include "IDecoderManifest.h"
#include "LoggingModule.h"
#include "decoder_manifest.pb.h"
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{

using namespace Aws::IoTFleetWise::Platform;
using namespace Aws::IoTFleetWise::Schemas;

// A dictionary type that maps CANRawFrameID's to their associated CANMessageFormats
using CANFrameToMessageMap = std::unordered_map<CANRawFrameID, CANMessageFormat>;

/**
 * @brief Setting a Decoder Manifest proto byte size limit for file received from Cloud
 */
constexpr size_t DECODER_MANIFEST_BYTE_SIZE_LIMIT = 128000000;

/**
 * @brief DecoderManifestIngestion (PI = Schema) is the implementation of IDecoderManifest used by
 * CollectionSchemeIngestion.
 */
class DecoderManifestIngestion : public IDecoderManifest
{
public:
    // Comments for child class are inherited from base case by Doxygen:
    // https://www.doxygen.nl/manual/config.html#cfg_inherit_docs

    DecoderManifestIngestion();

    ~DecoderManifestIngestion();

    bool isReady() const override;

    bool build() override;

    std::string getID() const override;

    const CANMessageFormat &getCANMessageFormat( CANRawFrameID canID, CANInterfaceID interfaceID ) const override;

    std::pair<CANRawFrameID, CANInterfaceID> getCANFrameAndInterfaceID( SignalID signalID ) const override;

    NetworkChannelProtocol getNetworkProtocol( SignalID signalID ) const override;

    PIDSignalDecoderFormat getPIDSignalDecoderFormat( SignalID signalID ) const override;

    bool copyData( const std::uint8_t *inputBuffer, const size_t size ) override;

    inline const std::vector<uint8_t> &
    getData() const override
    {
        return mProtoBinaryData;
    }

private:
    /**
     * @brief The DecoderManifest message that will hold the deserialized proto.
     */
    DecoderManifestMsg::DecoderManifest mProtoDecoderManifest;

    /**
     * @brief This vector will store the binary data copied from the IReceiver callback.
     */
    std::vector<uint8_t> mProtoBinaryData;

    /**
     * @brief Flag which is true if proto binary data is processed into readable data structures.
     */
    bool mReady;

    /**
     * @brief A dictionary used internally that allows the retrieval of a CANMessageFormat per CanChannelId and
     * CANRawFrameID Key: CANRawFrameID Value: CANMessageFormat
     */
    std::unordered_map<CANInterfaceID, CANFrameToMessageMap> mCANMessageFormatDictionary;

    /**
     * @brief A dictionary used internally that allows lookup of what CANRawFrameID and NodeID a Signal is found on
     * Key: Signal ID Value:pair of CANRawFrameID and NodeID
     */
    std::unordered_map<SignalID, std::pair<CANRawFrameID, CANInterfaceID>>
        mSignalToCANRawFrameIDAndInterfaceIDDictionary;

    /**
     * @brief A dictionary used internally that allows lookup of what type of network protocol is this signal
     * Key: Signal ID Value: Network Channel Protocol
     */
    std::unordered_map<SignalID, NetworkChannelProtocol> mSignalToNetworkChannelProtocol;

    /**
     * @brief A dictionary used to read the Decoder Format for OBD PID Signal
     * Key: Signal ID Value: OBD-II PID Signal Decoder Format
     */
    std::unordered_map<SignalID, PIDSignalDecoderFormat> mSignalToPIDDictionary;

    /**
     * @brief Logging module used to output to logs
     */
    LoggingModule mLogger;
};
} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
