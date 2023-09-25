// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DecoderManifestIngestion.h"
#include "CANDataTypes.h"
#include "EnumUtility.h"
#include "LoggingModule.h"
#include "OBDDataTypes.h"
#include <google/protobuf/message.h>

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
DecoderManifestIngestion::getCANMessageFormat( CANRawFrameID canID, CANInterfaceID interfaceID ) const
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

std::pair<CANRawFrameID, CANInterfaceID>
DecoderManifestIngestion::getCANFrameAndInterfaceID( SignalID signalID ) const
{
    if ( !mReady )
    {
        return std::make_pair( INVALID_CAN_FRAME_ID, INVALID_CAN_INTERFACE_ID );
    }

    // Check to see if entry exists
    if ( mSignalToCANRawFrameIDAndInterfaceIDDictionary.count( signalID ) > 0 )
    {
        // Entry exists, return it
        return mSignalToCANRawFrameIDAndInterfaceIDDictionary.at( signalID );
    }

    // Information for signal does not exist.
    return std::make_pair( INVALID_CAN_FRAME_ID, INVALID_CAN_INTERFACE_ID );
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

    // Do some validation of the DecoderManifest. Either CAN or OBD or both should be specified.
    if ( ( mProtoDecoderManifest.can_signals_size() == 0 ) && ( mProtoDecoderManifest.obd_pid_signals_size() == 0 ) )
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

    FWE_LOG_TRACE( "Decoder Manifest build succeeded" );
    // Set our ready flag to true
    mReady = true;
    return true;
}

} // namespace IoTFleetWise
} // namespace Aws
