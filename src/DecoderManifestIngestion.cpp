// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DecoderManifestIngestion.h"
#include "CANDataTypes.h"
#include "EnumUtility.h"
#include "LoggingModule.h"
#include "OBDDataTypes.h"
#include <google/protobuf/message.h>

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include <boost/variant.hpp>
#endif

namespace Aws
{
namespace IoTFleetWise
{

DecoderManifestIngestion::~DecoderManifestIngestion()
{
    // delete any global objects that were allocated by the Protocol Buffer library
    google::protobuf::ShutdownProtobufLibrary();
}

std::string
DecoderManifestIngestion::getID() const
{
    if ( !mReady )
    {
        // Return empty string
        return std::string();
    }

    return mProtoDecoderManifest.sync_id();
}

bool
DecoderManifestIngestion::isReady() const
{
    return mReady;
}

const CANMessageFormat &
DecoderManifestIngestion::getCANMessageFormat( CANRawFrameID canID, InterfaceID interfaceID ) const
{
    if ( !mReady )
    {
        return INVALID_CAN_MESSAGE_FORMAT;
    }

    // Check if the message for this CANRawFrameID and interfaceID exists
    if ( ( mCANMessageFormatDictionary.count( interfaceID ) > 0 ) &&
         ( mCANMessageFormatDictionary.at( interfaceID ).count( canID ) > 0 ) )
    {
        // It exists, so return it
        return mCANMessageFormatDictionary.at( interfaceID ).at( canID );
    }

    // It does not exist
    return INVALID_CAN_MESSAGE_FORMAT;
}

std::pair<CANRawFrameID, InterfaceID>
DecoderManifestIngestion::getCANFrameAndInterfaceID( SignalID signalID ) const
{
    if ( !mReady )
    {
        return std::make_pair( INVALID_CAN_FRAME_ID, INVALID_INTERFACE_ID );
    }

    // Check to see if entry exists
    if ( mSignalToCANRawFrameIDAndInterfaceIDDictionary.count( signalID ) > 0 )
    {
        // Entry exists, return it
        return mSignalToCANRawFrameIDAndInterfaceIDDictionary.at( signalID );
    }

    // Information for signal does not exist.
    return std::make_pair( INVALID_CAN_FRAME_ID, INVALID_INTERFACE_ID );
}

VehicleDataSourceProtocol
DecoderManifestIngestion::getNetworkProtocol( SignalID signalID ) const
{
    if ( !mReady )
    {
        return VehicleDataSourceProtocol::INVALID_PROTOCOL;
    }

    if ( mSignalToVehicleDataSourceProtocol.count( signalID ) > 0 )
    {
        // It exists, so return it
        return mSignalToVehicleDataSourceProtocol.at( signalID );
    }
    return VehicleDataSourceProtocol::INVALID_PROTOCOL;
}

PIDSignalDecoderFormat
DecoderManifestIngestion::getPIDSignalDecoderFormat( SignalID signalID ) const
{
    if ( !mReady )
    {
        return NOT_READY_PID_DECODER_FORMAT;
    }

    // Check if this signal exist in OBD PID Dictionary
    if ( mSignalToPIDDictionary.count( signalID ) > 0 )
    {
        // It exists, so return it
        return mSignalToPIDDictionary.at( signalID );
    }

    // It does not exist
    return NOT_FOUND_PID_DECODER_FORMAT;
}

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
ComplexSignalDecoderFormat
DecoderManifestIngestion::getComplexSignalDecoderFormat( SignalID signalID ) const
{

    auto iterator = mSignalToComplexDecoderFormat.find( signalID );
    if ( ( !mReady ) || ( iterator == mSignalToComplexDecoderFormat.end() ) )
    {
        return ComplexSignalDecoderFormat();
    }
    return iterator->second;
}

ComplexDataElement
DecoderManifestIngestion::getComplexDataType( ComplexDataTypeId typeId ) const
{
    auto iterator = mComplexTypeMap.find( typeId );
    if ( ( !mReady ) || ( iterator == mComplexTypeMap.end() ) )
    {
        return ComplexDataElement( InvalidComplexVariant() );
    }
    else
    {
        return iterator->second;
    }
}
#endif

bool
DecoderManifestIngestion::copyData( const std::uint8_t *inputBuffer, const size_t size )
{
    // check for a null input buffer or size set to 0
    if ( ( inputBuffer == nullptr ) || ( size == 0 ) )
    {
        FWE_LOG_ERROR( "Input buffer invalid" );
        return false;
    }

    // We have to guard against document sizes that are too large
    if ( size > DECODER_MANIFEST_BYTE_SIZE_LIMIT )
    {
        FWE_LOG_ERROR( "Decoder Manifest binary too big. Size: " + std::to_string( size ) +
                       " limit: " + std::to_string( DECODER_MANIFEST_BYTE_SIZE_LIMIT ) );
        return false;
    }

    // Copy the data of the inputBuffer to mProtoBinaryData
    mProtoBinaryData.assign( inputBuffer, inputBuffer + size );

    // Check to make sure the vector size is the same as our input size
    if ( mProtoBinaryData.size() != size )
    {
        FWE_LOG_ERROR( "Copied data not the same size as input data" );
        return false;
    }

    // Set the ready flag to false, as we have new data that needs to be parsed
    mReady = false;

    FWE_LOG_TRACE( "Copy of DecoderManifest data success" );
    return true;
}

bool
DecoderManifestIngestion::build()
{
    // In this function we parse the protobuffer binary and build internal datastructures required to support the
    // IDecoderManifest API.

    // Verify we have not accidentally linked against a version of the library which is incompatible with the version of
    // the headers we compiled with.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // Ensure that we have data to parse
    if ( mProtoBinaryData.empty() )
    {
        FWE_LOG_ERROR( "Failed to build due to an empty Decoder Manifest" );
        // Error, input buffer empty or invalid
        return false;
    }

    // Try to parse the binary data into our mProtoDecoderManifest member variable
    if ( !mProtoDecoderManifest.ParseFromArray( mProtoBinaryData.data(), static_cast<int>( mProtoBinaryData.size() ) ) )
    {
        FWE_LOG_ERROR( "Failed to parse DecoderManifest proto" );
        // Error parsing proto binary
        return false;
    }

    // Do some validation of the DecoderManifest. Either CAN or OBD or complex signals should at least be specified.
    if ( ( mProtoDecoderManifest.can_signals_size() == 0 ) && ( mProtoDecoderManifest.obd_pid_signals_size() == 0 )
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
         && ( mProtoDecoderManifest.complex_signals_size() == 0 )
#endif
    )
    {
        // Error, missing required decoding information in the Decoder mProtoDecoderManifest
        FWE_LOG_ERROR(
            "CAN Nodes or CAN Signal array or OBD PID Signal array is empty. Failed to build Decoder Manifest" );
        return false;
    }

    FWE_LOG_INFO( "Building Decoder Manifest with Sync ID: " + mProtoDecoderManifest.sync_id() );

    // Iterate over CAN Signals and build the mCANSignalFormatDictionary
    for ( int i = 0; i < mProtoDecoderManifest.can_signals_size(); i++ )
    {
        // Get a reference to the CAN signal in the protobuf
        const Schemas::DecoderManifestMsg::CANSignal &canSignal = mProtoDecoderManifest.can_signals( i );
        mSignalToVehicleDataSourceProtocol[canSignal.signal_id()] = VehicleDataSourceProtocol::RAW_SOCKET;

        // Add an entry to the Signal to CANRawFrameID and NodeID dictionary
        mSignalToCANRawFrameIDAndInterfaceIDDictionary.insert( std::make_pair(
            canSignal.signal_id(), std::make_pair( canSignal.message_id(), canSignal.interface_id() ) ) );

        // Create a container to hold the InterfaceManagement::CANSignal we will build
        CANSignalFormat canSignalFormat;

        canSignalFormat.mSignalID = canSignal.signal_id();
        canSignalFormat.mIsBigEndian = canSignal.is_big_endian();
        canSignalFormat.mIsSigned = canSignal.is_signed();
        canSignalFormat.mFirstBitPosition = static_cast<uint16_t>( canSignal.start_bit() );
        canSignalFormat.mSizeInBits = static_cast<uint16_t>( canSignal.length() );
        canSignalFormat.mOffset = canSignal.offset();
        canSignalFormat.mFactor = canSignal.factor();

        // TODO :: Update the datatype from the DM after the schema update for the datatype support
        mSignalIDToTypeMap[canSignal.signal_id()] = SignalType::DOUBLE; // using double as default

        canSignalFormat.mIsMultiplexorSignal = false;
        canSignalFormat.mMultiplexorValue = 0;

        FWE_LOG_TRACE( "Adding CAN Signal Format for Signal ID: " + std::to_string( canSignalFormat.mSignalID ) );

        // Each CANMessageFormat object contains an array of signal decoding rules for each signal it contains. Cloud
        // sends us a set of Signal IDs so we need to iterate through them and either create new CANMessageFormat
        // objects when they don't exist yet for the specified CAN frame id, or add the signal decoding rule to an
        // existing one.

        // First check if CANMessageFormat exists for this message id and node id
        if ( ( mCANMessageFormatDictionary.count( canSignal.interface_id() ) == 1 ) &&
             ( mCANMessageFormatDictionary[canSignal.interface_id()].count( canSignal.message_id() ) == 1 ) )
        {
            // CANMessageFormat exists for a given node id and message id. Add this signal format to it
            mCANMessageFormatDictionary[canSignal.interface_id()][canSignal.message_id()].mSignals.emplace_back(
                canSignalFormat );
        }
        else
        {
            // The CANMessageFormat for this CANRawFrameID in this Node was not found. We have to create it

            // First check if we need to create a node entry
            if ( mCANMessageFormatDictionary.count( canSignal.interface_id() ) == 0 )
            {
                // Create an empty CAN Frame ID -> CAN Message Format map for this node
                mCANMessageFormatDictionary.insert(
                    std::make_pair( canSignal.interface_id(), CANFrameToMessageMap() ) );
            }

            // Create the CANMessageFormat object
            CANMessageFormat newCANMessageFormat;
            newCANMessageFormat.mMessageID = canSignal.message_id();

            newCANMessageFormat.mIsMultiplexed = false;

            newCANMessageFormat.mSizeInBytes = MAX_CAN_FRAME_BYTE_SIZE;

            // Insert the CAN Signal Format in the newly created CAN Message Format
            newCANMessageFormat.mSignals.emplace_back( canSignalFormat );

            // Insert the newly created CANMessageFormat containing the Signal Format in the CAN Message Format
            // Dictionary
            mCANMessageFormatDictionary.at( canSignal.interface_id() )
                .insert( std::make_pair( canSignal.message_id(), newCANMessageFormat ) );
        }
    }

    // Reserve Map memory upfront as program already know the number of signals.
    // This optimization can avoid multiple rehashes and improve overall build performance
    mSignalToPIDDictionary.reserve( static_cast<size_t>( mProtoDecoderManifest.obd_pid_signals_size() ) );
    // Iterate over OBD-II PID Signals and build the obdPIDSignalDecoderFormat
    for ( int i = 0; i < mProtoDecoderManifest.obd_pid_signals_size(); i++ )
    {
        // Get a reference to the OBD PID signal in the protobuf
        const Schemas::DecoderManifestMsg::OBDPIDSignal &pidSignal = mProtoDecoderManifest.obd_pid_signals( i );
        if ( ( pidSignal.service_mode() >= toUType( SID::MAX ) ) || ( pidSignal.pid() > UINT8_MAX ) ||
             ( pidSignal.bit_right_shift() > UINT8_MAX ) || ( pidSignal.bit_mask_length() > UINT8_MAX ) )
        {
            FWE_LOG_WARN( "Invalid OBD PID signal" );
            continue;
        }
        mSignalToVehicleDataSourceProtocol[pidSignal.signal_id()] = VehicleDataSourceProtocol::OBD;
        PIDSignalDecoderFormat obdPIDSignalDecoderFormat = PIDSignalDecoderFormat(
            pidSignal.pid_response_length(),
            // coverity[autosar_cpp14_a7_2_1_violation] The if-statement above checks the correct range
            static_cast<SID>( pidSignal.service_mode() ),
            static_cast<PID>( pidSignal.pid() ),
            pidSignal.scaling(),
            pidSignal.offset(),
            pidSignal.start_byte(),
            pidSignal.byte_length(),
            static_cast<uint8_t>( pidSignal.bit_right_shift() ),
            static_cast<uint8_t>( pidSignal.bit_mask_length() ) );
        mSignalToPIDDictionary[pidSignal.signal_id()] = obdPIDSignalDecoderFormat;
        // TODO :: Update the datatype from the DM after the schema update for the datatype support
        mSignalIDToTypeMap[pidSignal.signal_id()] = SignalType::DOUBLE; // using double as default
    }

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    for ( int i = 0; i < mProtoDecoderManifest.complex_types_size(); i++ )
    {
        const Schemas::DecoderManifestMsg::ComplexType &complexType = mProtoDecoderManifest.complex_types( i );
        if ( ( complexType.type_id() == RESERVED_UTF8_UINT8_TYPE_ID ) ||
             ( complexType.type_id() == RESERVED_UTF16_UINT32_TYPE_ID ) )
        {
            FWE_LOG_WARN( "Complex type id:" + std::to_string( complexType.type_id() ) +
                          " is reserved and can not be used" );
            continue;
        }
        if ( mComplexTypeMap.find( complexType.type_id() ) != mComplexTypeMap.end() )
        {
            FWE_LOG_WARN( "Complex type with same type id already exists id:" +
                          std::to_string( complexType.type_id() ) );
            continue;
        }
        if ( complexType.variant_case() == Schemas::DecoderManifestMsg::ComplexType::kPrimitiveData )
        {
            auto &primitiveData = complexType.primitive_data();
            auto scaling = primitiveData.scaling();
            scaling = ( scaling == 0.0 ? 1.0 : scaling ); // If scaling is not set in protobuf or set to invalid 0
                                                          // replace it by default scaling: 1
            auto convertedType = convertPrimitiveTypeToSignalType( primitiveData.primitive_type() );
            mComplexTypeMap[complexType.type_id()] =
                ComplexDataElement( PrimitiveData{ convertedType, scaling, primitiveData.offset() } );
            FWE_LOG_TRACE( "Adding PrimitiveData with complex type id: " + std::to_string( complexType.type_id() ) +
                           " of type:" + std::to_string( static_cast<int>( convertedType ) ) );
        }
        else if ( complexType.variant_case() == Schemas::DecoderManifestMsg::ComplexType::kStruct )
        {
            ComplexStruct newStruct;
            for ( auto &complexStructMember : complexType.struct_().members() )
            {
                newStruct.mOrderedTypeIds.emplace_back( complexStructMember.type_id() );
            }
            mComplexTypeMap[complexType.type_id()] = ComplexDataElement( newStruct );
            FWE_LOG_TRACE( "Adding struct with complex type id: " + std::to_string( complexType.type_id() ) + " with " +
                           std::to_string( newStruct.mOrderedTypeIds.size() ) + " members" );
        }
        else if ( complexType.variant_case() == Schemas::DecoderManifestMsg::ComplexType::kArray )
        {
            auto &complexArray = complexType.array();
            mComplexTypeMap[complexType.type_id()] =
                ComplexDataElement( ComplexArray{ complexArray.size(), complexArray.type_id() } );
            FWE_LOG_TRACE( "Adding array with complex type id: " + std::to_string( complexType.type_id() ) + " with " +
                           std::to_string( complexArray.size() ) +
                           " members of type: " + std::to_string( complexArray.type_id() ) );
        }
        else if ( complexType.variant_case() == Schemas::DecoderManifestMsg::ComplexType::kStringData )
        {
            auto &complexStringData = complexType.string_data();
            if ( ( complexStringData.encoding() != Schemas::DecoderManifestMsg::StringEncoding::UTF_16 ) &&
                 ( complexStringData.encoding() != Schemas::DecoderManifestMsg::StringEncoding::UTF_8 ) )
            {
                FWE_LOG_WARN( "String data with type id " + std::to_string( complexType.type_id() ) +
                              " has invalid encoding: " +
                              std::to_string( static_cast<uint32_t>( complexStringData.encoding() ) ) );
                continue;
            }
            ComplexDataTypeId characterType =
                complexStringData.encoding() == Schemas::DecoderManifestMsg::StringEncoding::UTF_16
                    ? RESERVED_UTF16_UINT32_TYPE_ID
                    : RESERVED_UTF8_UINT8_TYPE_ID;
            if ( mComplexTypeMap.find( characterType ) == mComplexTypeMap.end() )
            {
                SignalType signalType =
                    complexStringData.encoding() == Schemas::DecoderManifestMsg::StringEncoding::UTF_16
                        ? SignalType::UINT32 // ROS2 implementation uses uint32 for utf-16 (wstring) code units
                        : SignalType::UINT8;
                mComplexTypeMap[characterType] = PrimitiveData{ signalType, 1.0, 0.0 };
            }
            mComplexTypeMap[complexType.type_id()] =
                ComplexDataElement( ComplexArray{ complexStringData.size(), characterType } );
            FWE_LOG_TRACE( "Adding string as array with complex type id: " + std::to_string( complexType.type_id() ) +
                           " with " + std::to_string( complexStringData.size() ) +
                           " members of type: " + std::to_string( characterType ) );
        }
    }

    for ( int i = 0; i < mProtoDecoderManifest.complex_signals_size(); i++ )
    {
        const Schemas::DecoderManifestMsg::ComplexSignal &complexSignal = mProtoDecoderManifest.complex_signals( i );
        mSignalToVehicleDataSourceProtocol[complexSignal.signal_id()] = VehicleDataSourceProtocol::COMPLEX_DATA;
        if ( complexSignal.interface_id() == INVALID_INTERFACE_ID )
        {
            FWE_LOG_WARN( "Complex Signal with empty interface_id and signal id:" +
                          std::to_string( complexSignal.signal_id() ) );
        }
        else
        {
            mSignalToComplexDecoderFormat[complexSignal.signal_id()] = ComplexSignalDecoderFormat{
                complexSignal.interface_id(), complexSignal.message_id(), complexSignal.root_type_id() };

            mSignalIDToTypeMap[complexSignal.signal_id()] =
                SignalType::RAW_DATA_BUFFER_HANDLE; // handle top level signals always as raw data handles
            FWE_LOG_TRACE( "Adding complex signal with id: " + std::to_string( complexSignal.signal_id() ) +
                           " with interface ID: '" + complexSignal.interface_id() + "' message  ID: '" +
                           complexSignal.message_id() +
                           "' and root complex type id: " + std::to_string( complexSignal.root_type_id() ) );
        }
    }
#endif

    FWE_LOG_TRACE( "Decoder Manifest build succeeded" );
    // Set our ready flag to true
    mReady = true;
    return true;
}

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
SignalType
DecoderManifestIngestion::convertPrimitiveTypeToSignalType( Schemas::DecoderManifestMsg::PrimitiveType primitiveType )
{
    switch ( primitiveType )
    {
    case Schemas::DecoderManifestMsg::PrimitiveType::BOOL:
        return SignalType::BOOLEAN;
    case Schemas::DecoderManifestMsg::PrimitiveType::UINT8:
        return SignalType::UINT8;
    case Schemas::DecoderManifestMsg::PrimitiveType::UINT16:
        return SignalType::UINT16;
    case Schemas::DecoderManifestMsg::PrimitiveType::UINT32:
        return SignalType::UINT32;
    case Schemas::DecoderManifestMsg::PrimitiveType::UINT64:
        return SignalType::UINT64;
    case Schemas::DecoderManifestMsg::PrimitiveType::INT8:
        return SignalType::INT8;
    case Schemas::DecoderManifestMsg::PrimitiveType::INT16:
        return SignalType::INT16;
    case Schemas::DecoderManifestMsg::PrimitiveType::INT32:
        return SignalType::INT32;
    case Schemas::DecoderManifestMsg::PrimitiveType::INT64:
        return SignalType::INT64;
    case Schemas::DecoderManifestMsg::PrimitiveType::FLOAT32:
        return SignalType::FLOAT;
    case Schemas::DecoderManifestMsg::PrimitiveType::FLOAT64:
        return SignalType::DOUBLE;
    default:
        FWE_LOG_WARN( "Currently PrimitiveType " + std::to_string( primitiveType ) + " is not supported" );
        break;
    }
    return SignalType::UINT8; // default to uint8
}
#endif

} // namespace IoTFleetWise
} // namespace Aws
