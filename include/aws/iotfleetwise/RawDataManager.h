// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <atomic>
#include <boost/optional/optional.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{
namespace RawData
{

using BufferTypeId = SignalID;
using RawDataType = std::vector<uint8_t>;
using FrameTimestamp = uint64_t;

/**
 * @brief Define the possible stages in the data pipeline where a handle can be referenced.
 *
 * This intends to allow users of BufferManager indicate that they are holding a handle and that
 * they MIGHT want access to the data in the future.
 */
enum struct BufferHandleUsageStage
{
    COLLECTED_NOT_IN_HISTORY_BUFFER = 0,
    COLLECTION_INSPECTION_ENGINE_HISTORY_BUFFER = 1,
    COLLECTION_INSPECTION_ENGINE_SELECTED_FOR_UPLOAD = 2,
    HANDED_OVER_TO_SENDER = 3,
    UPLOADING = 4,
    STAGE_SIZE = 5 // Used as reference to size arrays
};

struct Frame
{
    BufferHandle mHandleID{ 0 };
    Timestamp mTimestamp{ 0 };
    RawDataType mRawData;
    uint8_t mDataInUseCounter{ 0 }; // Controls how many references to the data are currently held. When this is not
                                    // zero, we can't delete the data as it could cause corrupted data to be uploaded.
    uint8_t mUsageHintCountersPerStage[static_cast<uint32_t>( BufferHandleUsageStage::STAGE_SIZE )] = {
        0 }; // Controls whether the handle (not necessarily the data) is
             // being referenced somewhere. Each element in the array means
             // a different type of usage so that the BufferManager can
             // prioritize some types of usage when it needs to free up
             // space. The data can be deleted only when both
             // mDataInUseCounter and mUsageHintCountersPerStage are 0
    Frame( BufferHandle handleID, Timestamp timestamp, RawDataType rawData )
        : mHandleID( handleID )
        , mTimestamp( timestamp )
        , mRawData( std::move( rawData ) )
    {
    }

    ~Frame() = default;

    Frame( const Frame & ) = delete;
    Frame &operator=( const Frame & ) = delete;
    Frame( Frame &&other ) = default;
    Frame &operator=( Frame &&other ) = default;

    uint8_t
    getUsageHint( BufferHandleUsageStage stage ) const
    {
        return mUsageHintCountersPerStage[static_cast<uint32_t>( stage )];
    }

    bool
    hasUsageHints() const
    {
        for ( int i = 0; i < static_cast<int>( BufferHandleUsageStage::STAGE_SIZE ); i++ )
        {
            if ( mUsageHintCountersPerStage[i] != 0 )
            {
                return true;
            }
        }
        return false;
    }

    size_t
    getSize() const
    {
        return mRawData.size();
    }
};

class BufferManager;

class LoanedFrame
{
public:
    // Intentional friend declaration so that returnLoanedFrame method can only be called by
    // LoanedFrame
    // coverity[autosar_cpp14_a11_3_1_violation]
    friend class BufferManager;

    // Copy is not allowed, only move is allowed for the following reasons:
    // 1. We want to avoid this object to be copied multiple times to reduce the chance of
    //    holding the data by mistake and thus preventing the manager from releasing it.
    // 2. Give more control to BufferManager so that it can decide to deny new access to
    //    some old data that the manager wants to delete soon or know exactly where the data is
    //    being used. If multiple consumers want access to the data at the same time, they
    //    should get it directly from the BufferManager instead of copying this object.
    LoanedFrame( const LoanedFrame & ) = delete;
    LoanedFrame &operator=( const LoanedFrame & ) = delete;
    LoanedFrame( LoanedFrame &&other ) noexcept;
    LoanedFrame &operator=( LoanedFrame &&other ) noexcept;

    ~LoanedFrame();

    const uint8_t *
    getData() const
    {
        return mData;
    }
    size_t
    getSize() const
    {
        return mSize;
    }
    bool

    isNull()
    {
        return ( mData == nullptr ) && ( mSize == 0 );
    }

private:
    BufferManager *mRawDataBufferManager{ nullptr };
    BufferTypeId mTypeId{ 0 };
    BufferHandle mHandle{ 0 };
    const uint8_t *mData{ nullptr };
    size_t mSize{ 0 };

    // This class should never be created by a consumer because it is the BufferManager that
    // owns the actual data and it needs to track what is in use to ensure the data is kept valid.
    LoanedFrame() = default;

    // coverity[autosar_cpp14_a0_1_3_violation] false-positive, this constructor is used
    LoanedFrame( BufferManager *rawDataBufferManager,
                 BufferTypeId typeId,
                 BufferHandle handle,
                 const uint8_t *data,
                 size_t size )
        : mRawDataBufferManager( rawDataBufferManager )
        , mTypeId( typeId )
        , mHandle( handle )
        , mData( data )
        , mSize( size )
    {
    }

    /**
     * @brief Swap all members of the given LoanedFrame objects
     *
     * This is mostly intended to make the move assignment operator compliant with autosar_cpp14_a12_8_2 rule.
     */
    static void swap( LoanedFrame &lhs, LoanedFrame &rhs ) noexcept;
};

enum struct StorageStrategy
{
    COPY_ON_INGEST_ASYNC = 0,     // Background task so input shared ptr might be used for long
    COPY_ON_INGEST_SYNC = 1,      // Call will block until copied
    ZERO_COPY = 2,                // Manage the unique,shared ptr, file handle etc. without copying
    COMPRESS_ON_INGEST_ASYNC = 3, // Compress in background task and release memory when done
    COMPRESS_ON_INGEST_SYNC = 4,
    STORE_TO_FILE_ASYNC = 5, // Copy the data to a file and then release the memory
    STORE_TO_FILE_SYNC = 6
};

enum struct BufferErrorCode
{
    SUCCESSFUL,
    OUTOFMEMORY
};

struct SignalConfig
{
    BufferTypeId typeId{ 0 };
    StorageStrategy storageStrategy{ StorageStrategy::COPY_ON_INGEST_SYNC };
    size_t maxNumOfSamples{ 0 };
    size_t maxBytesPerSample{ 0 };
    size_t maxOverallBytes{ 0 };
    size_t reservedBytes{ 0 };
};

struct TypeStatistics
{
    size_t overallNumOfSamplesReceived{ 0 };
    size_t numOfSamplesCurrentlyInMemory{ 0 };
    size_t numOfSamplesAccessedBySender{ 0 };
    FrameTimestamp maxTimeInMemory{ 0 };
    FrameTimestamp avgTimeInMemory{ 0 };
    FrameTimestamp minTimeInMemory{ 0 };

    TypeStatistics() = default;

    TypeStatistics( size_t overallNumOfSamplesReceivedIn,
                    size_t numOfSamplesCurrentlyInMemoryIn,
                    size_t numOfSamplesAccessedBySenderIn )
        : overallNumOfSamplesReceived( overallNumOfSamplesReceivedIn )
        , numOfSamplesCurrentlyInMemory( numOfSamplesCurrentlyInMemoryIn )
        , numOfSamplesAccessedBySender( numOfSamplesAccessedBySenderIn )
    {
    }

    TypeStatistics( size_t overallNumOfSamplesReceivedIn,
                    size_t numOfSamplesCurrentlyInMemoryIn,
                    size_t numOfSamplesAccessedBySenderIn,
                    FrameTimestamp maxTimeInMemoryIn,
                    FrameTimestamp avgTimeInMemoryIn,
                    FrameTimestamp minTimeInMemoryIn )
        : overallNumOfSamplesReceived( overallNumOfSamplesReceivedIn )
        , numOfSamplesCurrentlyInMemory( numOfSamplesCurrentlyInMemoryIn )
        , numOfSamplesAccessedBySender( numOfSamplesAccessedBySenderIn )
        , maxTimeInMemory( maxTimeInMemoryIn )
        , avgTimeInMemory( avgTimeInMemoryIn )
        , minTimeInMemory( minTimeInMemoryIn )
    {
    }
};

static const TypeStatistics INVALID_TYPE_STATISTICS = TypeStatistics{ 0, 0, 0 };

struct SignalBufferOverrides
{
    InterfaceID interfaceId;
    std::string messageId;
    boost::optional<size_t> reservedBytes;
    boost::optional<size_t> maxNumOfSamples;
    boost::optional<size_t> maxBytesPerSample;
    boost::optional<size_t> maxBytes;
};

// coverity[cert_dcl60_cpp_violation:FALSE] class only defined once
// coverity[autosar_cpp14_m3_2_2_violation:FALSE] class only defined once
// coverity[misra_cpp_2008_rule_3_2_2_violation:FALSE] class only defined once
// coverity[ODR_VIOLATION:FALSE] class only defined once
class BufferManagerConfig
{
public:
    ~BufferManagerConfig() = default;

    BufferManagerConfig( const BufferManagerConfig & ) = default;
    BufferManagerConfig &operator=( const BufferManagerConfig & ) = default;
    // coverity[autosar_cpp14_m3_2_2_violation:FALSE] not defined anywhere else
    // coverity[misra_cpp_2008_rule_3_2_2_violation:FALSE] not defined anywhere else
    // coverity[cert_dcl60_cpp_violation:FALSE] not defined anywhere else
    // coverity[ODR_VIOLATION:FALSE] not defined anywhere else
    BufferManagerConfig( BufferManagerConfig && ) = default;
    BufferManagerConfig &operator=( BufferManagerConfig && ) = default;

    /**
     * @brief Create a BufferManagerConfig object with default values
     *
     * @return the created config object
     */
    static boost::optional<BufferManagerConfig> create();

    /**
     * @brief Create a BufferManagerConfig optionally overriding individual settings
     * @param maxBytes the maximum size that can be used by all signals (the sum of space used by all signals can never
     * go beyond this value)
     * @param reservedBytesPerSignal the amount of memory dedicated to each signal. If set, each signal added to the
     * BufferManager will have this amount guaranteed to be available.
     * @param maxNumOfSamplesPerSignal the maximum number of samples that can be stored in memory for a single
     * signal.
     * @param maxBytesPerSample the maximum size that can be used by a single sample
     * @param maxBytesPerSignal the maximum size that can be used by a single signal. If set, each signal added to
     * the BufferManager will never occupy more than this amount unless an override is given.
     * @param overridesPerSignal a list of overrides for specific signals.
     * @return the created config object or boost::none if any of the parameters are invalid
     */
    static boost::optional<BufferManagerConfig> create( boost::optional<size_t> maxBytes,
                                                        boost::optional<size_t> reservedBytesPerSignal,
                                                        boost::optional<size_t> maxNumOfSamplesPerSignal,
                                                        boost::optional<size_t> maxBytesPerSample,
                                                        boost::optional<size_t> maxBytesPerSignal,
                                                        std::vector<SignalBufferOverrides> &overridesPerSignal );

    /**
     * @brief Give the config for a signal buffer considering any signal-specific overrides
     * @param typeId the type ID that will be used when adding raw data for this signal
     * @param interfaceId the interface ID associated to this signal. It will be used together with messageID to
     * uniquely identify the signal and find a config override.
     * @param messageId the message ID associated to this signal. It will be used together with interfaceID to
     * uniquely identify the signal and find a config override.
     * @return the config for the signal buffer with any overrides already resolved
     */
    SignalConfig getSignalConfig( BufferTypeId typeId,
                                  const InterfaceID &interfaceId,
                                  const std::string &messageId ) const;

    size_t
    getMaxBytes() const
    {
        return mMaxBytes;
    }

private:
    BufferManagerConfig() = default;

    size_t mMaxBytes{ 0 };
    size_t mReservedBytesPerSignal{ 0 };
    size_t mMaxNumOfSamplesPerSignal{ 0 };
    size_t mMaxBytesPerSample{ 0 };
    size_t mMaxBytesPerSignal{ 0 };

    std::unordered_map<InterfaceID, std::unordered_map<std::string, SignalBufferOverrides>>
        mOverridesPerSignal; // It can contain config overrides for a specific signal. When a signal is not
                             // present in this map, the default config (defined by the other members) is used. The
                             // first key is the interfaceId and the second key is the messageId. Both are needed to
                             // uniquely identify a signal.
};

struct SignalUpdateConfig
{
    BufferTypeId typeId{ 0 };
    InterfaceID interfaceId;
    std::string messageId;
};

/**
 * @brief Main class to implement the Raw Data Buffer Manager
 */
class BufferManager
{
public:
    // Intentional friend declaration so that only the manager can construct a LoanedFrame
    // coverity[autosar_cpp14_a11_3_1_violation]
    friend class LoanedFrame;

    /**
     * @brief Construct a new Raw Data Buffer Manager object
     *
     * @param config the config object containing general config that should be applied to all signals and
     * signal-specific config
     */
    BufferManager( const BufferManagerConfig &config );

    BufferManager( const BufferManager & ) = delete;
    BufferManager &operator=( const BufferManager & ) = delete;
    BufferManager( BufferManager && ) = delete;
    BufferManager &operator=( BufferManager && ) = delete;

    virtual ~BufferManager() = default;

    /**
     * @brief Update the Raw Buffer Config and allocated buffer for signals requested
     *
     * @param updatedSignals signals that should be added/updated. Signals that are missing from this set will have its
     * buffer removed.
     * @return BufferErrorCode
     * SUCCESSFUL : If all the signals requested can be allocated buffer
     * OUTOFMEMORY : If all the signals requested can't be allocated buffer due to memory limitation
     */
    virtual BufferErrorCode updateConfig( const std::unordered_map<BufferTypeId, SignalUpdateConfig> &updatedSignals );

    /**
     * @brief Get the total memory allocated for the raw data collection
     * @return Total memory allocated
     */
    inline size_t
    getUsedMemory()
    {
        std::lock_guard<std::mutex> lock( mBufferManagerMutex );
        return mBytesInUse;
    }

    /**
     * @brief Get the # of Active Raw data Buffers
     * @return # of Active data buffers
     */
    inline size_t
    getActiveBuffers()
    {
        std::lock_guard<std::mutex> lock( mBufferManagerMutex );
        return mTypeIDToBufferMap.size();
    }

    /**
     * @brief Push the raw data to the raw data Buffer Manager
     *
     * @param data pointer to the raw data vector
     * @param size size of the raw data vector
     * @param receiveTimestamp Received Timestamp for the raw data
     * @param typeId TypeID for the raw data
     * @return Unique BufferHandle for the data. There is no guarantee that the data related
     * to this handle will be kept. It may need to be released to give space to more recent data.
     * When the data needs to be read, the handle should be passed to the borrowFrame method.
     */
    virtual BufferHandle push( const uint8_t *data, size_t size, Timestamp receiveTimestamp, BufferTypeId typeId );

    /**
     * @brief Get the Statistics for a particular signal raw buffer
     *
     * @param typeId TypeID of the raw data requested
     * @return TypeStatistics
     */
    TypeStatistics getStatistics( BufferTypeId typeId );

    /**
     * @brief Get the Statistics for the buffer Manager
     *
     * @return TypeStatistics
     */
    TypeStatistics getStatistics();

    /**
     * @brief Temporarily get access to a raw data frame
     *
     * This method should be called when the actual raw data needs to be read. While the
     * LoanedFrame is alive, the data is guaranteed to be valid.
     *
     * Consumers should not hold the raw pointer only. All access should be via
     * LoanedFrame. Otherwise the data can be freed or overwritten.
     *
     * @param typeId TypeID of the raw data requested
     * @param handle Unique handle of the raw data requested
     * @return LoanedFrame
     */
    virtual LoanedFrame borrowFrame( BufferTypeId typeId, BufferHandle handle );

    /**
     * @brief Mark a handle as being used and for what purpose.
     *
     * This allows users to indicate they are currently using a handle and gives more information for
     * the BufferManager to decide which data to delete first.
     *
     * Note that this does not guarantee that the data will be available when the caller wants to read it.
     * This only allows the caller to indicate that it is currently holding a handle and that it may
     * need the data in the future. The data can still be deleted if needed (e.g. when there is no free
     * space for new data).
     *
     * The data is only guaranteed to be kept valid when calling borrowFrame() and keeping the
     * LoanedFrame object alive.
     *
     * @param typeId TypeID of the raw data
     * @param handle Unique handle of the raw data that may be needed in the future
     * @param handleUsageStage The stage in the data pipeline where this handle is being held.
     * @return whether the operation succeeded or not
     */
    virtual bool increaseHandleUsageHint( BufferTypeId typeId,
                                          BufferHandle handle,
                                          BufferHandleUsageStage handleUsageStage );

    /**
     * @brief Mark a handle as not being used for a particular purpose.
     *
     * This should normally be called after calling increaseHandleUsageHint() to indicate that the handle is
     * not needed for that purpose anymore.
     *
     * @param typeId TypeID of the raw data
     * @param handle Unique handle of the raw data that may be needed in the future
     * @param handleUsageStage The stage in the data pipeline where this handle is being held.
     * @return whether the operation succeeded or not
     */
    virtual bool decreaseHandleUsageHint( BufferTypeId typeId,
                                          BufferHandle handle,
                                          BufferHandleUsageStage handleUsageStage );

    /**
     * @brief Reset the usage hints of all handles for the specified stage
     *
     * @param handleUsageStage The stage in the data pipeline where this handle is being held.
     */
    void resetUsageHintsForStage( BufferHandleUsageStage handleUsageStage );

private:
    BufferManagerConfig mConfig; // General config for the buffer manager and signals
    size_t mMaxOverallMemory;    // Overall Maximum memory allocation
    size_t mBytesReserved{ 0 };  // Sum of Limits of all the signals
    size_t mBytesInUse{ 0 };     // Memory used
    size_t mBytesInUseAndReserved{
        0 }; // Memory used, including what is reserved for each signal (which might be used or not)
    size_t mOverallNumOfSamplesReceived{ 0 };   // Total message received
    size_t mNumOfSamplesCurrentlyInMemory{ 0 }; // # of Currently present messages
    size_t mNumOfSamplesAccessedBySender{ 0 };  // # of Messages accessed by Sender

    /**
     * @brief Allocated the raw buffer for the signal requested
     *
     * @param signalIDCollection SignalIDCollection struct containing details about buffer size, typeID,
     * max Sample
     * @return BufferErrorCode
     * SUCCESSFUL : allocated buffer for the signal requested
     * OUTOFMEMORY : can't allocate buffer for the signal requested due to memory constraints
     */
    BufferErrorCode addRawDataToBuffer( const SignalConfig &signalIDCollection );

    /**
     * @brief Indicated that the Raw Data Frame is not in use any more
     *
     * This should only be called when a LoanedFrame is destroyed, indicating that the data
     * is not being used by the consumer that previously called borrowFrame.
     *
     * The data won't necessarily be released. It might be still in use by others or the manager may
     * decide to keep it for other reasons.
     *
     * @param typeId TypeID of the raw data requested
     * @param handle Unique handle of the raw data requested
     */
    void returnLoanedFrame( BufferTypeId typeId, BufferHandle handle );

    /**
     * @brief Generates uniques Handle for the raw data using the timestamp
     *
     * @param timestamp Timestamp of the data for which handle is requested
     * @return BufferHandle
     */
    static BufferHandle generateHandleID( Timestamp timestamp );

    /**
     * @brief Get an raw message counter since the last reset of the software.
     * @return The incremented counter value
     */
    // coverity[autosar_cpp14_a0_1_3_violation] false-positive, this function is used
    static uint8_t
    generateRawMsgCounter()
    {
        static std::atomic<uint8_t> counter( 0 );
        // We shouldn't let it wrap to zero as this could make the buffer handle become zero, which
        // is invalid.
        if ( counter == std::numeric_limits<uint8_t>::max() )
        {
            counter = 1;
        }
        else
        {
            counter++;
        }
        return counter;
    }

    /**
     * @brief Checks if the memory can be allocated or not
     *
     * @param memoryReq required memory for the data
     * @return true if memory is available otherwise false
     */
    bool checkMemoryLimit( size_t memoryReq ) const;

    // coverity[autosar_cpp14_a0_1_3_violation] false-positive, this function is used
    static bool
    isValidStageIndex( uint32_t stageIndex )
    {
        return ( stageIndex < static_cast<uint32_t>( BufferHandleUsageStage::STAGE_SIZE ) );
    }

    struct Buffer
    {
        Buffer() = default;

        Buffer( BufferTypeId typeID,
                size_t maxNumOfSamples,
                size_t maxBytesPerSample,
                size_t maxOverallBytes,
                size_t reservedBytes,
                StorageStrategy storageStrategy )
            : mTypeID( typeID )
            , mMaxNumOfSamples( maxNumOfSamples )
            , mMaxBytesPerSample( maxBytesPerSample )
            , mMaxOverallBytes( maxOverallBytes )
            , mReservedBytes( reservedBytes )
            , mStorageStrategy( storageStrategy )
        {
        }

        BufferTypeId mTypeID{ 0 };      // TypeID of the signal stored
        size_t mMaxNumOfSamples{ 0 };   // Max number of samples that this buffer can store
        size_t mMaxBytesPerSample{ 0 }; // Max size per signal sample
        size_t mMaxOverallBytes{ 0 };   // Max size that can be stored considering all samples
        size_t mReservedBytes{ 0 }; // Memory that will be dedicated to this buffer. Even if unused, this amount won't
                                    // be shared with other buffers.
        size_t mBytesInUse{ 0 };    // Current memory in use for storing samples
        size_t mNumOfSamplesReceived{ 0 };          // Total message received
        size_t mNumOfSamplesCurrentlyInMemory{ 0 }; // # of Currently present messages
        size_t mNumOfSamplesAccessedBySender{ 0 };  // # of Messages accessed by Sender
        bool mDeleting{ false }; // Indicate whether this buffer should be deleted. This flag is used when the buffer
                                 // can't be immediately deleted because some data is still in use.
        StorageStrategy mStorageStrategy{ StorageStrategy::COPY_ON_INGEST_SYNC };
        std::vector<Frame> mBuffer; // Buffer to store the raw data
        std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

        bool addData( const uint8_t *data,
                      size_t size,
                      Timestamp receiveTimestamp,
                      BufferHandle rawDataHandle,
                      size_t availableFreeMemory );

        bool deleteUnusedData();

        size_t deleteDataFromHandle( const BufferHandle handle );

        inline size_t
        getUsedMemory() const
        {
            return mBytesInUse;
        }

        inline size_t
        getSize() const
        {
            return mBuffer.size();
        }

        inline FrameTimestamp
        getMaxTimeInMemory() const
        {
            if ( getSize() == 0 )
            {
                return 0;
            }
            return ( mClock->systemTimeSinceEpochMs() - mBuffer.front().mTimestamp );
        }

        inline FrameTimestamp
        getMinTimeInMemory() const
        {
            if ( getSize() == 0 )
            {
                return 0;
            }
            return ( mClock->systemTimeSinceEpochMs() - mBuffer.back().mTimestamp );
        }

        FrameTimestamp getAvgTimeInMemory() const;

        TypeStatistics getStatistics() const;
    };

    /**
     * @brief Try to find the Buffer and Frame associated to the given typeId and handle
     * @param typeId TypeID of the raw data requested
     * @param handle Unique handle of the raw data requested
     * @return pointers to the found Buffer and Frame
     */
    std::pair<Buffer *, Frame *> findBufferAndFrame( BufferTypeId typeId, BufferHandle handle );

    /**
     *@brief This function first checks if the frame is used and if not it deletes the frame
     *
     * @param buffer the buffer so no new search for handle must be started
     * @param frame the frame so no new search for the frame must be started
     */
    void deleteUnused( Buffer &buffer, Frame &frame );

    void addBufferToStats( Aws::IoTFleetWise::RawData::BufferManager::Buffer &buffer );

    void deleteBufferFromStats( Aws::IoTFleetWise::RawData::BufferManager::Buffer &buffer );

    // Map for TypeId to the Raw Data Buffer
    std::unordered_map<BufferTypeId, Buffer> mTypeIDToBufferMap;
    std::mutex mBufferManagerMutex;

    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
};

} // namespace RawData
} // namespace IoTFleetWise
} // namespace Aws
