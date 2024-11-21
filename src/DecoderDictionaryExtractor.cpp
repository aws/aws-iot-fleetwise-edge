// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "CollectionSchemeManager.h" // IWYU pragma: associated
#include "EnumUtility.h"
#include "ICollectionScheme.h"
#include "LoggingModule.h"
#include "MessageTypes.h"
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include <algorithm>
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>
#include <boost/variant.hpp>
#include <stack>
#endif

namespace Aws
{
namespace IoTFleetWise
{

void
CollectionSchemeManager::addSignalToDecoderDictionaryMap(
    SignalID signalId,
    std::map<VehicleDataSourceProtocol, std::shared_ptr<DecoderDictionary>> &decoderDictionaryMap
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    ,
    std::unordered_map<SignalID, SignalType> &partialSignalTypes,
    SignalID topLevelSignalId,
    SignalPath signalPath
#endif
)
{
    // get the Network Protocol Type: CAN, OBD, SOMEIP, etc
    auto networkType = mDecoderManifest->getNetworkProtocol( signalId );
    if ( networkType == VehicleDataSourceProtocol::INVALID_PROTOCOL )
    {
        FWE_LOG_WARN( "Invalid protocol provided for signal : " + std::to_string( signalId ) );
        // This signal contains invalid network protocol, cannot include it onto decoder dictionary
        return;
    }
    // Firstly we need to check if we already have dictionary created for this network
    if ( decoderDictionaryMap[networkType] == nullptr )
    {
        if ( ( networkType == VehicleDataSourceProtocol::RAW_SOCKET ) ||
             ( networkType == VehicleDataSourceProtocol::OBD ) )
        {
            decoderDictionaryMap[networkType] = std::make_shared<CANDecoderDictionary>();
        }
        else if ( networkType == VehicleDataSourceProtocol::CUSTOM_DECODING )
        {
            decoderDictionaryMap[networkType] = std::make_shared<CustomDecoderDictionary>();
        }
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
        // Currently we don't have decoder dictionary for this type of network protocol, create one
        else if ( networkType == VehicleDataSourceProtocol::COMPLEX_DATA )
        {
            decoderDictionaryMap[networkType] = std::make_shared<ComplexDataDecoderDictionary>();
        }
#endif
        else
        {
            FWE_LOG_ERROR( "Unknown network type: " + std::to_string( toUType( networkType ) ) +
                           " for signalID: " + std::to_string( signalId ) );
            return;
        }
    }

    if ( networkType == VehicleDataSourceProtocol::RAW_SOCKET )
    {
        auto canRawFrameID = mDecoderManifest->getCANFrameAndInterfaceID( signalId ).first;
        auto interfaceId = mDecoderManifest->getCANFrameAndInterfaceID( signalId ).second;

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
                          std::to_string( signalId ) );
        }
        else
        {
            // Add signalID to the set of this decoder dictionary
            canDecoderDictionaryPtr->signalIDsToCollect.insert( signalId );
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
            else if ( canDecoderDictionaryPtr->canMessageDecoderMethod[canChannelID][canRawFrameID].collectType ==
                      CANMessageCollectType::RAW )
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
        auto pidDecoderFormat = mDecoderManifest->getPIDSignalDecoderFormat( signalId );
        // There's only one VehicleDataSourceProtocol::OBD Channel, this is just a place holder to maintain the
        // generic dictionary structure
        CANChannelNumericID canChannelID = 0;
        auto obdPidCanDecoderDictionaryPtr =
            std::dynamic_pointer_cast<CANDecoderDictionary>( decoderDictionaryMap[networkType] );
        if ( !obdPidCanDecoderDictionaryPtr )
        {
            FWE_LOG_WARN( "Can not cast dictionary to CANDecoderDictionary for OBD Signal ID: " +
                          std::to_string( signalId ) );
        }
        else
        {
            obdPidCanDecoderDictionaryPtr->signalIDsToCollect.insert( signalId );
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
            format.mSignalID = signalId;
            format.mSignalType = pidDecoderFormat.mSignalType;
            format.mFirstBitPosition =
                static_cast<uint16_t>( pidDecoderFormat.mStartByte * BYTE_SIZE + pidDecoderFormat.mBitRightShift );
            format.mSizeInBits = static_cast<uint16_t>( ( pidDecoderFormat.mByteLength - 1 ) * BYTE_SIZE +
                                                        pidDecoderFormat.mBitMaskLength );
            format.mFactor = pidDecoderFormat.mScaling;
            format.mOffset = pidDecoderFormat.mOffset;
            obdPidCanDecoderDictionaryPtr->canMessageDecoderMethod.at( canChannelID )
                .at( pidDecoderFormat.mPID )
                .format.mSignals.emplace_back( format );
        }
    }
    else if ( networkType == VehicleDataSourceProtocol::CUSTOM_DECODING )
    {
        auto customDecoderDictionaryPtr = std::dynamic_pointer_cast<CustomDecoderDictionary>(
            decoderDictionaryMap[VehicleDataSourceProtocol::CUSTOM_DECODING] );
        auto customSignalDecoderFormat = mDecoderManifest->getCustomSignalDecoderFormat( signalId );
        if ( !customDecoderDictionaryPtr )
        {
            FWE_LOG_WARN( "Can not cast dictionary to CustomDecoderDictionary for Custom Decoded Signal ID: " +
                          std::to_string( signalId ) );
        }
        else if ( customSignalDecoderFormat.mInterfaceId.empty() )
        {
            FWE_LOG_WARN( "Custom Decoded signal ID has empty interfaceID: " + std::to_string( signalId ) );
        }
        else
        {
            customDecoderDictionaryPtr
                ->customDecoderMethod[customSignalDecoderFormat.mInterfaceId][customSignalDecoderFormat.mDecoder] =
                customSignalDecoderFormat;
            FWE_LOG_TRACE( "Custom Decoded Signal ID: " + std::to_string( signalId ) );
        }
    }
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    else if ( networkType == VehicleDataSourceProtocol::COMPLEX_DATA )
    {
        auto complexDataDictionary =
            std::dynamic_pointer_cast<ComplexDataDecoderDictionary>( decoderDictionaryMap[networkType] );
        if ( !complexDataDictionary )
        {
            FWE_LOG_WARN( "Can not cast dictionary to ComplexDataDecoderDictionary for Signal ID: " +
                          std::to_string( topLevelSignalId ) );
        }
        else
        {
            if ( signalId != INVALID_SIGNAL_ID )
            {
                auto complexSignalInfo = mDecoderManifest->getComplexSignalDecoderFormat( signalId );
                if ( complexSignalInfo.mInterfaceId.empty() )
                {
                    FWE_LOG_WARN( "Complex signal ID has empty interfaceID: " + std::to_string( signalId ) );
                }
                else
                {
                    auto &complexSignal =
                        complexDataDictionary
                            ->complexMessageDecoderMethod[complexSignalInfo.mInterfaceId][complexSignalInfo.mMessageId];
                    putComplexSignalInDictionary( complexSignal,
                                                  signalId,
                                                  topLevelSignalId,
                                                  signalPath,
                                                  complexSignalInfo.mRootTypeId,
                                                  partialSignalTypes );
                }
            }
        }
    }
#endif
}

void
CollectionSchemeManager::decoderDictionaryExtractor(
    std::map<VehicleDataSourceProtocol, std::shared_ptr<DecoderDictionary>> &decoderDictionaryMap
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    ,
    std::shared_ptr<InspectionMatrix> inspectionMatrix
#endif
)
{
    // Initialize the dictionary map with nullptr for each protocol, so that protocols are disabled if
    // none of the collection schemes collect data for that protocol
    decoderDictionaryMap.clear();
    for ( auto protocol : SUPPORTED_NETWORK_PROTOCOL )
    {
        decoderDictionaryMap[protocol] = nullptr;
    }
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    std::unordered_map<SignalID, SignalType> partialSignalTypes;
#endif
    // Iterate through enabled collectionScheme lists to locate the signals and CAN frames to be collected
    for ( auto it = mEnabledCollectionSchemeMap.begin(); it != mEnabledCollectionSchemeMap.end(); ++it )
    {
        const auto &collectionSchemePtr = it->second;
        if ( collectionSchemePtr->getDecoderManifestID() != mCurrentDecoderManifestID )
        {
            continue;
        }
        // first iterate through the signalID lists
        for ( const auto &signalInfo : collectionSchemePtr->getCollectSignals() )
        {
            SignalID signalId = signalInfo.signalID;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
            SignalPath signalPath;
            if ( ( signalId & INTERNAL_SIGNAL_ID_BITMASK ) != 0 )
            {
                auto partialSignalInfo =
                    collectionSchemePtr->getPartialSignalIdToSignalPathLookupTable().find( signalId );
                if ( partialSignalInfo == collectionSchemePtr->getPartialSignalIdToSignalPathLookupTable().end() )
                {
                    FWE_LOG_WARN( "Unknown partial signal ID: " + std::to_string( signalId ) );
                    signalId = INVALID_SIGNAL_ID;
                }
                else
                {
                    signalId = partialSignalInfo->second.first;
                    signalPath = partialSignalInfo->second.second;
                }
            }
#endif
            addSignalToDecoderDictionaryMap( signalId,
                                             decoderDictionaryMap
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                                             ,
                                             partialSignalTypes,
                                             signalInfo.signalID,
                                             signalPath
#endif
            );
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

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    // Now we need to update the InspectionMatrix to set the correct SignalType for partial signal IDs.
    // We do this here because by the time the InspectionMatrix is created we don't have enough info
    // to determine the type of partial signals.
    // So any signal that is a partial signal needs to be updated here, otherwise they would have the
    // wrong signal type which would cause a mismatch with the type used to set up CollectionInspectionEngine
    // buffers.
    if ( !partialSignalTypes.empty() )
    {
        for ( auto &condition : inspectionMatrix->conditions )
        {
            for ( auto &signal : condition.signals )
            {
                auto signalType = partialSignalTypes.find( signal.signalID );
                if ( signalType != partialSignalTypes.end() )
                {
                    FWE_LOG_TRACE(
                        "Signal type for partial signal with internal ID: " + std::to_string( signal.signalID ) +
                        " is being overwritten with type: " + signalTypeToString( signalType->second ) );
                    signal.signalType = signalType->second;
                }
            }
        }
    }
#endif

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    for ( const auto &stateTemplate : mStateTemplates )
    {
        if ( stateTemplate.second->decoderManifestID != mCurrentDecoderManifestID )
        {
            continue;
        }

        for ( const auto &lksSignal : stateTemplate.second->signals )
        {
            addSignalToDecoderDictionaryMap( lksSignal.signalID,
                                             decoderDictionaryMap
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                                             ,
                                             partialSignalTypes,
                                             lksSignal.signalID,
                                             // Complex types are not supported for Last Known State
                                             SignalPath()
#endif
            );
        }
    }
#endif
}

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
static boost::optional<SignalType>
findPartialSignalType( const ComplexDataMessageFormat &complexSignal, const SignalPathAndPartialSignalID &signalPath )
{
    auto currentTypeId = complexSignal.mRootTypeId;
    auto complexDataType = complexSignal.mComplexTypeMap.find( currentTypeId );

    for ( auto &pathLevel : signalPath.mSignalPath )
    {
        if ( complexDataType == complexSignal.mComplexTypeMap.end() )
        {
            FWE_LOG_ERROR( "Could not find type for ID: " + std::to_string( currentTypeId ) );
            return boost::none;
        }

        if ( complexDataType->second.type() == typeid( ComplexStruct ) )
        {
            try
            {
                auto complexStruct = boost::get<ComplexStruct>( complexDataType->second );
                currentTypeId = complexStruct.mOrderedTypeIds[pathLevel];
            }
            catch ( boost::bad_get & )
            {
                // Should not throw because of typeid check but catch it to prevent static analysis errors
            }
        }
        else if ( complexDataType->second.type() == typeid( ComplexArray ) )
        {
            try
            {
                auto complexArray = boost::get<ComplexArray>( complexDataType->second );
                currentTypeId = complexArray.mRepeatedTypeId;
            }
            catch ( boost::bad_get & )
            {
                // Should not throw because of typeid check but catch it to prevent static analysis errors
            }
        }
        else
        {
            break;
        }

        complexDataType = complexSignal.mComplexTypeMap.find( currentTypeId );
    }

    if ( complexDataType == complexSignal.mComplexTypeMap.end() )
    {
        FWE_LOG_ERROR( "Could not find partial type" );
        return boost::none;
    }

    if ( complexDataType->second.type() != typeid( PrimitiveData ) )
    {
        FWE_LOG_TRACE( "Signal path pointing to type ID: " + std::to_string( complexDataType->first ) +
                       " is not a primitive type" );
        return boost::none;
    }

    try
    {
        auto primitiveData = boost::get<PrimitiveData>( complexDataType->second );
        return primitiveData.mPrimitiveType;
    }
    catch ( boost::bad_get & )
    {
        // Should not throw because of typeid check but catch it to prevent static analysis errors
    }

    return boost::none;
}

void
CollectionSchemeManager::putComplexSignalInDictionary( ComplexDataMessageFormat &complexSignal,
                                                       SignalID signalID,
                                                       PartialSignalID partialSignalID,
                                                       SignalPath &signalPath,
                                                       ComplexDataTypeId complexSignalRootType,
                                                       std::unordered_map<SignalID, SignalType> &partialSignalTypes )
{
    if ( complexSignal.mSignalId == INVALID_SIGNAL_ID )
    {
        // First time this signal is accessed
        complexSignal.mSignalId = signalID;
        complexSignal.mRootTypeId = complexSignalRootType;
        // Add all needed complex types reachable
        std::stack<ComplexDataTypeId, std::vector<ComplexDataTypeId>> complexTypesToTraverse;
        complexTypesToTraverse.push( complexSignal.mRootTypeId );
        int elementsLeftToProcess = static_cast<int>( MAX_COMPLEX_TYPES );

        while ( ( elementsLeftToProcess > 0 ) && ( !complexTypesToTraverse.empty() ) )
        {
            elementsLeftToProcess--;
            auto c = complexTypesToTraverse.top();
            complexTypesToTraverse.pop();
            if ( complexSignal.mComplexTypeMap.find( c ) == complexSignal.mComplexTypeMap.end() )
            {
                auto complexDataType = mDecoderManifest->getComplexDataType( c );
                if ( complexDataType.type() == typeid( InvalidComplexVariant ) )
                {
                    FWE_LOG_ERROR( "Invalid complex type id: " + std::to_string( c ) );
                }
                else
                {
                    complexSignal.mComplexTypeMap[c] = complexDataType;
                    if ( complexDataType.type() == typeid( ComplexArray ) )
                    {
                        try
                        {
                            auto t = boost::get<ComplexArray>( complexDataType ).mRepeatedTypeId;
                            complexTypesToTraverse.push( t );
                        }
                        catch ( boost::bad_get & )
                        {
                            // Should not throw because of typeid check but catch it to prevent static analysis errors
                        }
                    }
                    if ( complexDataType.type() == typeid( ComplexStruct ) )
                    {
                        try
                        {
                            for ( auto member : boost::get<ComplexStruct>( complexDataType ).mOrderedTypeIds )
                            {
                                complexTypesToTraverse.push( member );
                            }
                        }
                        catch ( boost::bad_get & )
                        {
                            // Should not throw because of typeid check but catch it to prevent static analysis errors
                        }
                    }
                }
            }
        }
    }
    if ( signalPath.empty() )
    {
        complexSignal.mCollectRaw = true;
    }
    else
    {
        auto newPathToInsert = SignalPathAndPartialSignalID{ signalPath, partialSignalID };
        // insert sorted
        // coverity[autosar_cpp14_a23_0_1_violation] false positive - conversion is from iterator to const_iterator
        complexSignal.mSignalPaths.insert(
            std::upper_bound( complexSignal.mSignalPaths.begin(), complexSignal.mSignalPaths.end(), newPathToInsert ),
            newPathToInsert );

        auto signalType = findPartialSignalType( complexSignal, newPathToInsert );
        if ( signalType.has_value() )
        {
            partialSignalTypes[partialSignalID] = signalType.get();
        }
    }
}
#endif

void
CollectionSchemeManager::decoderDictionaryUpdater(
    std::map<VehicleDataSourceProtocol, std::shared_ptr<DecoderDictionary>> &decoderDictionaryMap )
{
    for ( auto const &dict : decoderDictionaryMap )
    {
        // Down cast the CAN Decoder Dictionary to base Decoder Dictionary. We will support more
        // types of Decoder Dictionary in later releases
        auto dictPtr = std::static_pointer_cast<const DecoderDictionary>( dict.second );
        mActiveDecoderDictionaryChangeListeners.notify( dictPtr, dict.first );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
