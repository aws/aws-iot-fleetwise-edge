// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "SignalTypes.h"
#include <cstdint>
#include <vector>

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include <boost/variant.hpp>
#include <unordered_map>
#endif

namespace Aws
{
namespace IoTFleetWise
{

static constexpr SignalID INVALID_CAN_FRAME_ID = 0xFFFFFFFF;

/**
 * @brief Contains the decoding rules to decode all signals in a CAN frame.
 */
struct CANMessageFormat
{
    uint32_t mMessageID{ 0x0 };
    uint8_t mSizeInBytes{ 0 };
    bool mIsMultiplexed{ false };
    std::vector<CANSignalFormat> mSignals;

public:
    /**
     * @brief Overload of the == operator
     * @param other Other CANMessageFormat object to compare
     * @return true if ==, false otherwise
     */
    bool
    operator==( const CANMessageFormat &other ) const
    {
        return ( mMessageID == other.mMessageID ) && ( mSizeInBytes == other.mSizeInBytes ) &&
               ( mSignals == other.mSignals ) && ( mIsMultiplexed == other.mIsMultiplexed );
    }

    /**
     * @brief Overload of the != operator
     * @param other Other CANMessageFormat object to compare
     * @return true if !=, false otherwise
     */
    bool
    operator!=( const CANMessageFormat &other ) const
    {
        return !( *this == other );
    }

    /**
     * @brief Check if a CAN Message Format is value by making sure it contains at least one signal
     * @return True if valid, false otherwise.
     */
    inline bool
    isValid() const
    {
        return !mSignals.empty();
    }

    /**
     * @brief Check if a CAN Message Decoding rule is multiplexed
     * @return True if multiplexed, false otherwise.
     */
    inline bool
    isMultiplexed() const
    {
        return mIsMultiplexed;
    }
};

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
using ComplexDataTypeId = uint32_t;

/**
 * @brief a UTF-8 string is represented internally as uint8 array and the corresponding primitive type id is:
 */
static constexpr ComplexDataTypeId RESERVED_UTF8_UINT8_TYPE_ID = 0xF0000001;

/**
 * @brief a UTF-16 string is represented internally in ROS2 as uint32 array and the corresponding primitive type id is:
 */
static constexpr ComplexDataTypeId RESERVED_UTF16_UINT32_TYPE_ID = 0xF0000002;

struct PrimitiveData
{
    SignalType mPrimitiveType;
    double mScaling;
    double mOffset;
};

struct ComplexArray
{
    /**
     * size > 0 : Fixed sized array always has the same size
     * size < 0 : The array has dynamic size but is never bigger than the maximum = size * -1
     * size = 0 : The array has dynamic size and a maximum estimate is unknown so from having 1 element
     *                  to multiple gigabyte can arrive as long as edge client can handle it
     */
    int64_t mSize;
    ComplexDataTypeId mRepeatedTypeId;
};

struct ComplexStruct
{
    std::vector<ComplexDataTypeId> mOrderedTypeIds;
};

struct InvalidComplexVariant
{
};

using ComplexDataElement = boost::variant<PrimitiveData, ComplexArray, ComplexStruct, InvalidComplexVariant>;

using ComplexDataInterfaceId = std::string;

const std::string INVALID_COMPLEX_DATA_INTERFACE = std::string(); // empty string is not valid

using ComplexDataMessageId = std::string;

static constexpr char COMPLEX_DATA_MESSAGE_ID_SEPARATOR = ':';

/**
 * @brief Select partial data inside a signal. For example if the Signal is an array of tuples the
 * signal path: {500,0} refers to the first element of the tuple of the 500th element.
 */
using SignalPath = std::vector<uint32_t>;

struct SignalPathAndPartialSignalID
{
    SignalPath mSignalPath;
    PartialSignalID mPartialSignalID;
    friend bool
    operator<( const SignalPathAndPartialSignalID &l, const SignalPathAndPartialSignalID &r )
    {
        size_t numberToCompare = std::min( l.mSignalPath.size(), r.mSignalPath.size() );
        for ( size_t i = 0; i < numberToCompare; i++ )
        {
            if ( l.mSignalPath[i] != r.mSignalPath[i] )
            {
                return l.mSignalPath[i] < r.mSignalPath[i];
            }
        }
        return l.mSignalPath.size() < r.mSignalPath.size();
    }
    bool
    equals( const SignalPath &signalPath ) const
    {
        if ( signalPath.size() != mSignalPath.size() )
        {
            return false;
        }
        for ( size_t i = 0; i < mSignalPath.size(); i++ )
        {
            if ( mSignalPath[i] != signalPath[i] )
            {
                return false;
            }
        }
        return true;
    }
    bool
    isSmaller( const SignalPath &than ) const
    {
        size_t sizeToCompare = std::min( mSignalPath.size(), than.size() );
        for ( size_t i = 0; i < sizeToCompare; i++ )
        {
            if ( mSignalPath[i] != than[i] )
            {
                return mSignalPath[i] < than[i];
            }
        }
        return false;
    }
};

/**
 * @brief This message format is used to decode a complex message
 */
struct ComplexDataMessageFormat
{
    /**
     * @brief the signalID as assigned by cloud to this message
     */
    SignalID mSignalId{ INVALID_SIGNAL_ID };

    /**
     * @brief Should the full raw message be collected
     */
    bool mCollectRaw;

    /**
     * @brief root Id that can be used to traves the full tree of the message
     */
    ComplexDataTypeId mRootTypeId;

    /**
     * @brief Always sorted by the SignalPath for example {10,1,5} {500,0,8} {500,1,0} is the valid order for the signal
     * paths independet of the assigned PartialSignalID. Not all possible signal paths have to be included
     */
    std::vector<SignalPathAndPartialSignalID> mSignalPaths;

    /**
     * @brief all ComplexDataTypeId recursively reachable over mRootTypeId must be in this map
     */
    std::unordered_map<ComplexDataTypeId, ComplexDataElement> mComplexTypeMap;
};
#endif

} // namespace IoTFleetWise
} // namespace Aws
