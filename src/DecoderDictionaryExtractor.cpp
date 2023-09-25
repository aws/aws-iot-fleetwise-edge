// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionSchemeManager.h" // IWYU pragma: associated
#include "ICollectionScheme.h"
#include "LoggingModule.h"
#include "MessageTypes.h"
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Aws
{
namespace IoTFleetWise
{

void
CollectionSchemeManager::decoderDictionaryExtractor(
    std::map<VehicleDataSourceProtocol, std::shared_ptr<DecoderDictionary>> &decoderDictionaryMap )
{
    // Initialize the dictionary map with nullptr for each protocol, so that protocols are disabled if
    // none of the collection schemes collect data for that protocol
    decoderDictionaryMap.clear();
    for ( auto protocol : SUPPORTED_NETWORK_PROTOCOL )
    {
        decoderDictionaryMap[protocol] = nullptr;
    }
    // Iterate through enabled collectionScheme lists to locate the signals and CAN frames to be collected
    for ( auto it = mEnabledCollectionSchemeMap.begin(); it != mEnabledCollectionSchemeMap.end(); ++it )
    {
        const auto &collectionSchemePtr = it->second;
        // first iterate through the signalID lists
        for ( const auto &signalInfo : collectionSchemePtr->getCollectSignals() )
        {
            // get the Network Protocol Type: CAN, OBD, SOMEIP, etc
            auto networkType = mDecoderManifest->getNetworkProtocol( signalInfo.signalID );
            if ( networkType == VehicleDataSourceProtocol::INVALID_PROTOCOL )
            {
                FWE_LOG_WARN( "Invalid protocol provided for signal : " + std::to_string( signalInfo.signalID ) );
                // This signal contains invalid network protocol, cannot include it onto decoder dictionary
                continue;
            }
            // Firstly we need to check if we already have dictionary created for this network
            if ( decoderDictionaryMap[networkType] == nullptr )
            {
                // Currently we don't have decoder dictionary for this type of network protocol, create one
                decoderDictionaryMap[networkType] = std::make_shared<CANDecoderDictionary>();
            }

            if ( networkType == VehicleDataSourceProtocol::RAW_SOCKET )
            {
                auto canRawFrameID = mDecoderManifest->getCANFrameAndInterfaceID( signalInfo.signalID ).first;
                auto interfaceId = mDecoderManifest->getCANFrameAndInterfaceID( signalInfo.signalID ).second;

                auto canDecoderDictionaryPtr =
                    std::dynamic_pointer_cast<CANDecoderDictionary>( decoderDictionaryMap[networkType] );
                auto canChannelID = mCANIDTranslator.getChannelNumericID( interfaceId );
                if ( canChannelID == INVALID_CAN_SOURCE_NUMERIC_ID )
                {
                    FWE_LOG_WARN( "Invalid Interface ID provided: " + interfaceId );
                }
                else if ( !canDecoderDictionaryPtr )
                {
                    FWE_LOG_WARN( "Can not cast dictionary to CANDecoderDictionary for CAN Signal ID: " +
                                  std::to_string( signalInfo.signalID ) );
                }
                else
                {
                    // Add signalID to the set of this decoder dictionary
                    canDecoderDictionaryPtr->signalIDsToCollect.insert( signalInfo.signalID );
                    // firstly check if we have canChannelID entry at dictionary top layer
                    if ( canDecoderDictionaryPtr->canMessageDecoderMethod.find( canChannelID ) ==
                         canDecoderDictionaryPtr->canMessageDecoderMethod.end() )
                    {
                        // create an entry for canChannelID if it's not existed yet
                        canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID] =
                            std::unordered_map<CANRawFrameID, CANMessageDecoderMethod>();
                    }
                    // check if this CAN Frame already exits in dictionary, if so, update if its a raw can decoder
                    // method.
                    // If not, we need to create an entry for this CAN Frame which will include decoder
                    // format for all signals defined in decoder manifest
                    if ( canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID].find( canRawFrameID ) ==
                         canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID].end() )
                    {
                        CANMessageDecoderMethod decoderMethod;
                        // We set the collect Type to DECODE at this stage. In the second half of this function, we will
                        // examine the CAN Frames. If there's any CAN Frame to have both signal and raw bytes to be
                        // collected, the type will be updated to RAW_AND_DECODE
                        decoderMethod.collectType = CANMessageCollectType::DECODE;
                        decoderMethod.format = mDecoderManifest->getCANMessageFormat( canRawFrameID, interfaceId );
                        canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID][canRawFrameID] = decoderMethod;
                    }
                    else if ( canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID][canRawFrameID]
                                  .collectType == CANMessageCollectType::RAW )
                    {
                        canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID][canRawFrameID].collectType =
                            CANMessageCollectType::RAW_AND_DECODE;
                        canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID][canRawFrameID].format =
                            mDecoderManifest->getCANMessageFormat( canRawFrameID, interfaceId );
                    }
                }
            }
            else if ( networkType == VehicleDataSourceProtocol::OBD )
            {
                auto pidDecoderFormat = mDecoderManifest->getPIDSignalDecoderFormat( signalInfo.signalID );
                // There's only one VehicleDataSourceProtocol::OBD Channel, this is just a place holder to maintain the
                // generic dictionary structure
                CANChannelNumericID canChannelID = 0;
                auto obdPidCanDecoderDictionaryPtr =
                    std::dynamic_pointer_cast<CANDecoderDictionary>( decoderDictionaryMap[networkType] );
                if ( !obdPidCanDecoderDictionaryPtr )
                {
                    FWE_LOG_WARN( "Can not cast dictionary to CANDecoderDictionary for OBD Signal ID: " +
                                  std::to_string( signalInfo.signalID ) );
                }
                else
                {
                    obdPidCanDecoderDictionaryPtr->signalIDsToCollect.insert( signalInfo.signalID );
                    obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.emplace(
                        canChannelID, std::unordered_map<CANRawFrameID, CANMessageDecoderMethod>() );
                    if ( obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.find( canChannelID ) ==
                         obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.end() )
                    {
                        // create an entry for canChannelID if it's not existed yet
                        obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID] =
                            std::unordered_map<CANRawFrameID, CANMessageDecoderMethod>();
                    }
                    if ( obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.at( canChannelID )
                             .find( pidDecoderFormat.mPID ) ==
                         obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.at( canChannelID ).end() )
                    {
                        // There's no Dictionary Entry created for this PID yet, create one
                        obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.at( canChannelID )
                            .emplace( pidDecoderFormat.mPID, CANMessageDecoderMethod() );
                        obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.at( canChannelID )
                            .at( pidDecoderFormat.mPID )
                            .format.mMessageID = pidDecoderFormat.mPID;
                        obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.at( canChannelID )
                            .at( pidDecoderFormat.mPID )
                            .format.mSizeInBytes = static_cast<uint8_t>( pidDecoderFormat.mPidResponseLength );
                    }
                    // Below is the OBD Signal format represented in generic Signal Format
                    CANSignalFormat format;
                    format.mSignalID = signalInfo.signalID;
                    format.mFirstBitPosition = static_cast<uint16_t>( pidDecoderFormat.mStartByte * BYTE_SIZE +
                                                                      pidDecoderFormat.mBitRightShift );
                    format.mSizeInBits = static_cast<uint16_t>( ( pidDecoderFormat.mByteLength - 1 ) * BYTE_SIZE +
                                                                pidDecoderFormat.mBitMaskLength );
                    format.mFactor = pidDecoderFormat.mScaling;
                    format.mOffset = pidDecoderFormat.mOffset;
                    obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.at( canChannelID )
                        .at( pidDecoderFormat.mPID )
                        .format.mSignals.emplace_back( format );
                }
            }
        }
        // Next let's iterate through the CAN Frames that collectionScheme wants to collect.
        // If some CAN Frame has signals to be decoded, we will set its collectType as RAW_AND_DECODE.
        if ( !collectionSchemePtr->getCollectRawCanFrames().empty() )
        {
            if ( decoderDictionaryMap[VehicleDataSourceProtocol::RAW_SOCKET] == nullptr )
            {
                // Currently we don't have decoder dictionary for this type of network protocol, create one
                decoderDictionaryMap[VehicleDataSourceProtocol::RAW_SOCKET] = std::make_shared<CANDecoderDictionary>();
            }
            auto canDecoderDictionaryPtr = std::dynamic_pointer_cast<CANDecoderDictionary>(
                decoderDictionaryMap[VehicleDataSourceProtocol::RAW_SOCKET] );
            if ( !canDecoderDictionaryPtr )
            {
                FWE_LOG_WARN( "Can not cast dictionary to CANDecoderDictionary for CAN RAW_SOCKET" );
            }
            else
            {
                for ( const auto &canFrameInfo : collectionSchemePtr->getCollectRawCanFrames() )
                {
                    auto canChannelID = mCANIDTranslator.getChannelNumericID( canFrameInfo.interfaceID );
                    if ( canChannelID == INVALID_CAN_SOURCE_NUMERIC_ID )
                    {
                        FWE_LOG_WARN( "Invalid Interface ID provided:" + canFrameInfo.interfaceID );
                    }
                    else
                    {
                        if ( canDecoderDictionaryPtr->canMessageDecoderMethod.find( canChannelID ) ==
                             canDecoderDictionaryPtr->canMessageDecoderMethod.end() )
                        {
                            // create an entry for canChannelID if the dictionary doesn't have one
                            canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID] =
                                std::unordered_map<CANRawFrameID, CANMessageDecoderMethod>();
                        }
                        // check if we already have entry for CAN Frame. If not, it means this CAN Frame doesn't contain
                        // any Signals to decode, hence the collectType will be RAW only.
                        auto decoderMethod =
                            canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID].find( canFrameInfo.frameID );
                        if ( decoderMethod == canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID].end() )
                        {
                            // there's entry for CANChannelNumericID but no corresponding canFrameID
                            CANMessageDecoderMethod canMessageDecoderMethod;
                            canMessageDecoderMethod.collectType = CANMessageCollectType::RAW;
                            canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID][canFrameInfo.frameID] =
                                canMessageDecoderMethod;
                        }
                        else
                        {
                            if ( decoderMethod->second.collectType == CANMessageCollectType::DECODE )
                            {
                                // This CAN Frame contains signal to be decoded. As we need to collect both CAN Frame
                                // and signal, set the collectType as RAW_AND_DECODE
                                decoderMethod->second.collectType = CANMessageCollectType::RAW_AND_DECODE;
                            }
                        }
                    }
                }
            }
        }
    }
}

void
CollectionSchemeManager::decoderDictionaryUpdater(
    std::map<VehicleDataSourceProtocol, std::shared_ptr<DecoderDictionary>> &decoderDictionaryMap )
{
    for ( auto const &dict : decoderDictionaryMap )
    {
        // Down cast the CAN Decoder Dictionary to base Decoder Dictionary. We will support more
        // types of Decoder Dictionary in later releases
        auto dictPtr = std::static_pointer_cast<const DecoderDictionary>( dict.second );
        notifyListeners<const std::shared_ptr<const DecoderDictionary> &>(
            &IActiveDecoderDictionaryListener::onChangeOfActiveDictionary, dictPtr, dict.first );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
