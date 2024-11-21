// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "IDecoderManifest.h"
#include "LoggingModule.h"
#include "MessageTypes.h"
#include "SignalTypes.h"
#include "VehicleDataSourceTypes.h"
#include "decoder_manifest.pb.h"
#include <boost/optional/optional.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

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

    DecoderManifestIngestion() = default;

    ~DecoderManifestIngestion() override;

    DecoderManifestIngestion( const DecoderManifestIngestion & ) = delete;
    DecoderManifestIngestion &operator=( const DecoderManifestIngestion & ) = delete;
    DecoderManifestIngestion( DecoderManifestIngestion && ) = delete;
    DecoderManifestIngestion &operator=( DecoderManifestIngestion && ) = delete;

    bool isReady() const override;

    bool build() override;

    SyncID getID() const override;

    const CANMessageFormat &getCANMessageFormat( CANRawFrameID canID, InterfaceID interfaceID ) const override;

    std::pair<CANRawFrameID, InterfaceID> getCANFrameAndInterfaceID( SignalID signalID ) const override;

    VehicleDataSourceProtocol getNetworkProtocol( SignalID signalID ) const override;

    PIDSignalDecoderFormat getPIDSignalDecoderFormat( SignalID signalID ) const override;

    CustomSignalDecoderFormat getCustomSignalDecoderFormat( SignalID signalID ) const override;

    SignalIDToCustomSignalDecoderFormatMapPtr getSignalIDToCustomSignalDecoderFormatMap() const override;

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    ComplexSignalDecoderFormat getComplexSignalDecoderFormat( SignalID signalID ) const override;

    ComplexDataElement getComplexDataType( ComplexDataTypeId typeId ) const override;
#endif

    bool copyData( const std::uint8_t *inputBuffer, const size_t size ) override;

    inline const std::vector<uint8_t> &
    getData() const override
    {
        return mProtoBinaryData;
    }

    inline SignalType
    getSignalType( const SignalID signalID ) const override
    {
        if ( mSignalIDToTypeMap.find( signalID ) == mSignalIDToTypeMap.end() )
        {
            FWE_LOG_WARN( "Signal Type not found for requested SignalID:" + std::to_string( signalID ) +
                          ", using type as unknown" );
            return SignalType::UNKNOWN;
        }
        return mSignalIDToTypeMap.at( signalID );
    }

private:
    /**
     * @brief The DecoderManifest message that will hold the deserialized proto.
     */
    Schemas::DecoderManifestMsg::DecoderManifest mProtoDecoderManifest;

    /**
     * @brief This vector will store the binary data copied from the IReceiver callback.
     */
    std::vector<uint8_t> mProtoBinaryData;

    /**
     * @brief Flag which is true if proto binary data is processed into readable data structures.
     */
    bool mReady{ false };

    /**
     * @brief A dictionary used internally that allows the retrieval of a CANMessageFormat per CanChannelId and
     * CANRawFrameID Key: CANRawFrameID Value: CANMessageFormat
     */
    std::unordered_map<InterfaceID, CANFrameToMessageMap> mCANMessageFormatDictionary;

    /**
     * @brief A dictionary used internally that allows lookup of what CANRawFrameID and NodeID a Signal is found on
     * Key: Signal ID Value:pair of CANRawFrameID and NodeID
     */
    std::unordered_map<SignalID, std::pair<CANRawFrameID, InterfaceID>> mSignalToCANRawFrameIDAndInterfaceIDDictionary;

    /**
     * @brief A dictionary used internally that allows lookup of what type of network protocol is this signal
     * Key: Signal ID Value: Vehicle Data Source Protocol
     */
    std::unordered_map<SignalID, VehicleDataSourceProtocol> mSignalToVehicleDataSourceProtocol;

    /**
     * @brief A dictionary used to read the Decoder Format for OBD PID Signal
     * Key: Signal ID Value: OBD-II PID Signal Decoder Format
     */
    std::unordered_map<SignalID, PIDSignalDecoderFormat> mSignalToPIDDictionary;

    using SignalIDToTypeMap = std::unordered_map<SignalID, SignalType>;
    SignalIDToTypeMap mSignalIDToTypeMap;

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    /**
     * @brief stores all the ComplexSignalDecoderFormat for SignalID. All SignalID are set by cloud so no
     * PartialSignalID
     */
    std::unordered_map<SignalID, ComplexSignalDecoderFormat> mSignalToComplexDecoderFormat;

    /**
     * @brief complex types can consist of other complex types. Then the ComplexDataElement has a ComplexDataTypeId
     * that can be looked up again in this map.
     */
    std::unordered_map<ComplexDataTypeId, ComplexDataElement> mComplexTypeMap;
#endif

    /**
     * @brief In the protobuf a different enum if used to represent the different types like uint8, uint16 etc.
     * @param primitiveType the enum type of the protobuf
     *
     * @return the type C++ enum used by the DecoderDictionary
     */
    static boost::optional<SignalType> convertPrimitiveTypeToSignalType(
        Schemas::DecoderManifestMsg::PrimitiveType primitiveType );

    /**
     * @brief A dictionary used to obtain the custom decoder for a given signal
     */
    SignalIDToCustomSignalDecoderFormatMapPtr mSignalToCustomDecoder;
};

} // namespace IoTFleetWise
} // namespace Aws
