// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "DataSenderIonWriter.h"
#include "EventTypes.h"
#include "LoggingModule.h"
#include "MessageTypes.h"
#include "RawDataManager.h"
#include "SignalTypes.h"
#include "StreambufBuilder.h"
#include <cstddef>
#include <functional>
#include <ionc/ion.h>
#include <ios>
#include <unordered_map>
#include <utility>
#include <vector>

#define ION_CHECK( x )                                                                                                 \
    {                                                                                                                  \
        auto res = ( x );                                                                                              \
        if ( res != IERR_OK )                                                                                          \
        {                                                                                                              \
            FWE_LOG_ERROR( "ion returned: " + std::to_string( res ) )                                                  \
            return;                                                                                                    \
        }                                                                                                              \
    }

using IonWriteCallback = std::function<iERR( _ion_user_stream *stream )>;

extern "C"
{
    static iERR
    ionWriteCallback( _ion_user_stream *stream )
    {
        // forwards call to mIonWriteCallback
        return ( *reinterpret_cast<IonWriteCallback *>( stream->handler_state ) )( stream );
    }
}

namespace Aws
{
namespace IoTFleetWise
{

class IonFileGenerator : public std::streambuf
{
    // Functionality used by std::streambuf public functions:
protected:
    int_type
    underflow() override
    {
        if ( mOutputStep == 0 )
        {
            mWrittenBytesInIonFullStream = 0;
            serializeMetadata();
        }
        else if ( mOutputStep > mFramesToSendOut.size() )
        {
            mOverallSize = static_cast<int64_t>( mWrittenBytesInIonFullStream );
            setg( reinterpret_cast<char *>( &mIonWriteBuffer[0] ),
                  reinterpret_cast<char *>( &mIonWriteBuffer[0] ),
                  reinterpret_cast<char *>( &mIonWriteBuffer[0] ) );
            return traits_type::eof();
        }
        else
        {
            auto frameToSend = mFramesToSendOut[mOutputStep - 1]; // First iteration is metadata
            auto loanedRawDataFrame = mRawDataBufferManager->borrowFrame( frameToSend.mId, frameToSend.mHandle );
            while ( loanedRawDataFrame.isNull() && ( mOutputStep < mFramesToSendOut.size() ) )
            {
                FWE_LOG_ERROR( "Raw data with signalid: " + std::to_string( frameToSend.mId ) +
                               " and buffer handle: " + std::to_string( frameToSend.mHandle ) +
                               " could not be sent because it was already deleted" )
                mOutputStep++;
                frameToSend = mFramesToSendOut[mOutputStep - 1];
                loanedRawDataFrame = mRawDataBufferManager->borrowFrame( frameToSend.mId, frameToSend.mHandle );
            }
            if ( loanedRawDataFrame.isNull() )
            {
                FWE_LOG_ERROR( "Raw data with signalid: " + std::to_string( frameToSend.mId ) +
                               " and buffer handle: " + std::to_string( frameToSend.mHandle ) +
                               " could not be sent because it was already deleted" )
                mOverallSize = static_cast<int64_t>( mWrittenBytesInIonFullStream );
                setg( reinterpret_cast<char *>( &mIonWriteBuffer[0] ),
                      reinterpret_cast<char *>( &mIonWriteBuffer[0] ),
                      reinterpret_cast<char *>( &mIonWriteBuffer[0] ) );
                return traits_type::eof();
            }
            serializeOneRawBufferToIon( frameToSend,
                                        reinterpret_cast<const char *>( loanedRawDataFrame.getData() ),
                                        loanedRawDataFrame.getSize() );
        }
        mWrittenBytesInIonFullStream += mWrittenBytesInIonWriteBuffer;

        mOutputStep++;
        setg( reinterpret_cast<char *>( &mIonWriteBuffer[0] ),
              reinterpret_cast<char *>( &mIonWriteBuffer[0] ),
              reinterpret_cast<char *>( &mIonWriteBuffer[mWrittenBytesInIonWriteBuffer] ) );
        return mIonWriteBuffer[0];
    }

    // coverity[autosar_cpp14_m3_9_1_violation] false-positive, redeclaration is compatible
    // coverity[misra_cpp_2008_rule_3_9_1_violation] same
    std::streampos
    seekpos( std::streampos sp, std::ios_base::openmode which = std::ios_base::in | std::ios_base::out ) override
    {
        FWE_LOG_TRACE( "seekpos sp: " + std::to_string( sp ) + " which: " + std::to_string( which ) );
        return seekoff( sp, std::ios_base::beg, which );
    };

    pos_type
    seekoff( off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which ) override
    {
        if ( ( off == 0 ) && ( dir == std::ios_base::beg ) && ( which == std::ios_base::in ) )
        {
            // start new
            FWE_LOG_TRACE( "seek from " + std::to_string( mWrittenBytesInIonFullStream ) +
                           " to beginning of Ion file" );
            mOutputStep = 0;
            mWrittenBytesInIonFullStream = 0;
            // call setg so first read calls underflow
            setg( reinterpret_cast<char *>( &mIonWriteBuffer[0] ),
                  reinterpret_cast<char *>( &mIonWriteBuffer[0] ),
                  reinterpret_cast<char *>( &mIonWriteBuffer[0] ) );

            return pos_type( off_type( 0 ) );
        }
        if ( ( ( ( off == 0 ) && ( dir == std::ios_base::end ) ) ||
               ( ( off == mOverallSize ) && ( dir == std::ios_base::beg ) ) ) &&
             ( which == std::ios_base::in ) )
        {
            while ( underflow() != traits_type::eof() )
            {
            }
            FWE_LOG_TRACE( "seek from " + std::to_string( mWrittenBytesInIonFullStream ) +
                           " to end of Ion file overall size: " + std::to_string( mOverallSize ) );
            setg( reinterpret_cast<char *>( &mIonWriteBuffer[0] ),
                  reinterpret_cast<char *>( &mIonWriteBuffer[0] ),
                  reinterpret_cast<char *>( &mIonWriteBuffer[0] ) );
            return pos_type( off_type( mOverallSize ) );
        }
        if ( ( off > 0 ) && ( dir == std::ios_base::beg ) && ( which == std::ios_base::in ) )
        {
            size_t requestedAbsolutePos = static_cast<size_t>( off );
            auto absolutePosOfFirstByteInBuffer = mWrittenBytesInIonFullStream - mWrittenBytesInIonWriteBuffer;
            if ( absolutePosOfFirstByteInBuffer > requestedAbsolutePos )
            {
                FWE_LOG_TRACE(
                    "seek backwards to a position that is not available anymore, need to regenerate the data" );
                mOutputStep = 0;
                mWrittenBytesInIonFullStream = 0;
            }

            while ( ( mWrittenBytesInIonFullStream < requestedAbsolutePos ) && ( underflow() != traits_type::eof() ) )
            {
            }

            if ( mWrittenBytesInIonFullStream < requestedAbsolutePos )
            {
                FWE_LOG_ERROR(
                    "End of stream reached but the requested position was not. Written bytes in Ion stream: " +
                    std::to_string( mWrittenBytesInIonFullStream ) +
                    " requested position: " + std::to_string( requestedAbsolutePos ) );
                return pos_type( off_type( -1 ) );
            }

            if ( mWrittenBytesInIonFullStream < mWrittenBytesInIonWriteBuffer )
            {
                FWE_LOG_ERROR( "Size of full stream is smaller than the number of bytes written in the buffer. Written "
                               "bytes in Ion stream: " +
                               std::to_string( mWrittenBytesInIonFullStream ) +
                               " written bytes in buffer: " + std::to_string( mWrittenBytesInIonWriteBuffer ) );
                return pos_type( off_type( -1 ) );
            }
            absolutePosOfFirstByteInBuffer = mWrittenBytesInIonFullStream - mWrittenBytesInIonWriteBuffer;

            if ( requestedAbsolutePos < absolutePosOfFirstByteInBuffer )
            {
                FWE_LOG_ERROR( "Absolute position of first byte available in the buffer is already beyond requested "
                               "position. Position of first byte in buffer: " +
                               std::to_string( absolutePosOfFirstByteInBuffer ) +
                               " requested position: " + std::to_string( requestedAbsolutePos ) );
                return pos_type( off_type( -1 ) );
            }
            size_t bufferPos = requestedAbsolutePos - absolutePosOfFirstByteInBuffer;

            if ( bufferPos >= mWrittenBytesInIonWriteBuffer )
            {
                FWE_LOG_ERROR( "Requested position is larger than the number of bytes written in the buffer. "
                               "Written bytes in the buffer: " +
                               std::to_string( mWrittenBytesInIonWriteBuffer ) +
                               " requested position: " + std::to_string( requestedAbsolutePos ) );
                return pos_type( off_type( -1 ) );
            }

            FWE_LOG_TRACE( "seek from beginning of Ion file to " + std::to_string( requestedAbsolutePos ) );
            setg( reinterpret_cast<char *>( &mIonWriteBuffer[0] ),
                  reinterpret_cast<char *>( &mIonWriteBuffer[bufferPos] ),
                  reinterpret_cast<char *>( &mIonWriteBuffer[mWrittenBytesInIonWriteBuffer] ) );
            return pos_type( off );
        }
        if ( ( off == 0 ) && ( dir == std::ios_base::cur ) && ( which == std::ios_base::in ) )
        {
            FWE_LOG_TRACE( "seek from " + std::to_string( mWrittenBytesInIonFullStream ) +
                           " to cur of Ion file return pos " + std::to_string( mWrittenBytesInIonFullStream ) );
            return pos_type( off_type( mWrittenBytesInIonFullStream ) );
        }
        FWE_LOG_ERROR( "Ion stream seek in not supported way. Position: " + std::to_string( off ) +
                       " dir: " + std::to_string( static_cast<uint32_t>( dir ) ) +
                       " openmode: " + std::to_string( static_cast<uint32_t>( which ) ) );
        return pos_type( off_type( -1 ) );
    }

    static const int WRITE_BUFFER_INCREASE_STEP = 64 * 1024;
    // IonFileGenerator specific logic not directly used by std::streambuf:
public:
    IonFileGenerator( std::shared_ptr<RawData::BufferManager> rawDataBufferManager,
                      std::string vehicleId,
                      Timestamp triggerTime,
                      EventID eventId,
                      std::string decoderManifestId,
                      std::string collectionSchemeId,
                      std::vector<FrameInfoForIon> &framesToSendOut )
        : mIonWriteCallback( nullptr )
        , mRawDataBufferManager( std::move( rawDataBufferManager ) )
        , mTriggerTime( triggerTime )
        , mEventId( eventId )
        , mDecoderManifestId( std::move( decoderManifestId ) )
        , mCollectionSchemeId( std::move( collectionSchemeId ) )
        , mVehicleId( std::move( vehicleId ) )
    {
        lockAllBufferHandles( framesToSendOut );
    }

    ~IonFileGenerator() override
    {
        unlockAllBufferHandles();
        if ( mWriterNeedsClose )
        {
            ION_CHECK( ion_writer_close( mIonWriter ) );
            mWriterNeedsClose = false;
        }
    }

    IonFileGenerator( const IonFileGenerator & ) = delete;
    IonFileGenerator &operator=( const IonFileGenerator & ) = delete;
    IonFileGenerator( IonFileGenerator && ) = delete;
    IonFileGenerator &operator=( IonFileGenerator && ) = delete;

    size_t
    getNumberOfBufferHandles()
    {
        return mFramesToSendOut.size();
    }

private:
    static constexpr uint64_t MAX_BYTES_PER_BLOB = 1000000000; // 1 GB
                                                               //
    void
    lockAllBufferHandles( std::vector<FrameInfoForIon> &framesToSendOut )
    {
        for ( auto frame : framesToSendOut )
        {
            FWE_LOG_TRACE( "Reserving raw data with signalid: " + std::to_string( frame.mId ) +
                           " and buffer handle: " + std::to_string( frame.mHandle ) )
            auto loanedRawDataFrame = mRawDataBufferManager->borrowFrame( frame.mId, frame.mHandle );
            if ( loanedRawDataFrame.isNull() )
            {
                FWE_LOG_WARN( "Raw data with signalid: " + std::to_string( frame.mId ) + " and buffer handle: " +
                              std::to_string( frame.mHandle ) + " will be skipped because it is already deleted" )
                continue;
            }

            if ( mRawDataBufferManager->increaseHandleUsageHint(
                     frame.mId, frame.mHandle, RawData::BufferHandleUsageStage::UPLOADING ) )
            {
                mFramesToSendOut.push_back( frame );
            }
            else
            {
                FWE_LOG_WARN( "Raw data with signalid: " + std::to_string( frame.mId ) + " and buffer handle: " +
                              std::to_string( frame.mHandle ) + " will be skipped because it couldn't be reserved" )
            }

            mRawDataBufferManager->decreaseHandleUsageHint(
                frame.mId, frame.mHandle, RawData::BufferHandleUsageStage::HANDED_OVER_TO_SENDER );
        }
    }

    void
    unlockAllBufferHandles()
    {
        for ( auto frame : mFramesToSendOut )
        {
            mRawDataBufferManager->decreaseHandleUsageHint(
                frame.mId, frame.mHandle, RawData::BufferHandleUsageStage::UPLOADING );
        }
        mFramesToSendOut.clear();
    }

    static iERR
    ionWriteFieldStr( hWRITER writer, const char *str, size_t len )
    {
        ION_STRING fieldNameString;
        // clang-format off
        // coverity[autosar_cpp14_a5_2_3_violation] ion-c needs non-const input
        // coverity[misra_cpp_2008_rule_5_2_5_violation] ion-c needs non-const input
        // coverity[cert_exp55_cpp_violation] ion-c needs non-const input
        ion_string_assign_cstr( &fieldNameString, const_cast<char *>( str ), static_cast<SIZE>( len ) ); // NOLINT(cppcoreguidelines-pro-type-const-cast) ion-c needs non-const input
        // clang-format on
        return ion_writer_write_field_name( ( writer ), &fieldNameString );
    }

    static iERR
    ionWriteValueStr( hWRITER writer, const char *str, size_t len )
    {
        ION_STRING valueString;
        // clang-format off
        // coverity[autosar_cpp14_a5_2_3_violation] ion-c needs non-const input
        // coverity[misra_cpp_2008_rule_5_2_5_violation] ion-c needs non-const input
        // coverity[cert_exp55_cpp_violation] ion-c needs non-const input
        ion_string_assign_cstr( &valueString, const_cast<char *>( str ), static_cast<SIZE>( len ) ); // NOLINT(cppcoreguidelines-pro-type-const-cast) ion-c needs non-const input
        // clang-format on
        return ion_writer_write_string( ( writer ), &valueString );
    }

    /**
     * @brief This causes the ion writer to close so a new symbol table will be emitted for following data
     *
     * @return true if successful
     */
    bool
    openNewIonWriter()
    {
        if ( mWriterNeedsClose )
        {
            auto res = ion_writer_close( mIonWriter );
            if ( res != IERR_OK )
            {
                FWE_LOG_WARN( "Could not close old ion writer stream because of error:  " + std::to_string( res ) );
            }
            mWriterNeedsClose = false;
        }

        mWrittenBytesInIonWriteBuffer = 0;
        mIonWriteBuffer.resize( WRITE_BUFFER_INCREASE_STEP );

        // The callbacks will come only directly form inside a ion_ call and not from a different thread
        mIonWriteCallback = [this]( _ion_user_stream *stream ) -> iERR {
            // Check limit to skip first call where stream->limit is not initialized
            if ( stream->limit == static_cast<BYTE *>( mIonWriteBuffer.data() + mIonWriteBuffer.size() ) )
            {
                // coverity[misra_cpp_2008_rule_5_0_18_violation] it was checked that stream->curr is inside the array
                // coverity[autosar_cpp14_m5_0_18_violation] same
                // coverity[cert_ctr54_cpp_violation] same
                if ( ( stream->curr >= static_cast<BYTE *>( &( *mIonWriteBuffer.begin() ) ) ) &&
                     // clang-format off
                    // coverity[misra_cpp_2008_rule_5_0_18_violation] it was checked that stream->curr is inside the array
                    // coverity[autosar_cpp14_m5_0_18_violation] same
                     ( stream->curr <= static_cast<BYTE *>( mIonWriteBuffer.data() + mIonWriteBuffer.size() ) ) )
                // clang-format on
                {
                    // coverity[autosar_cpp14_m5_0_17_violation] it was checked that stream->curr is inside the array
                    // coverity[autosar_cpp14_m5_0_9_violation] same
                    // coverity[misra_cpp_2008_rule_5_0_17_violation] same
                    // coverity[misra_cpp_2008_rule_5_0_9_violation] same
                    // coverity[cert_ctr54_cpp_violation] same
                    mWrittenBytesInIonWriteBuffer =
                        static_cast<size_t>( stream->curr - static_cast<BYTE *>( &( *mIonWriteBuffer.begin() ) ) );
                }
                if ( stream->curr == stream->limit )
                { // only increase buffer if necessary
                    mIonWriteBuffer.resize( mIonWriteBuffer.size() +
                                            WRITE_BUFFER_INCREASE_STEP ); // Resize will change address
                    stream->limit = static_cast<BYTE *>( mIonWriteBuffer.data() + mIonWriteBuffer.size() );
                    stream->curr = static_cast<BYTE *>( &( *mIonWriteBuffer.begin() ) ) + mWrittenBytesInIonWriteBuffer;
                }
            }
            else
            {
                // First call
                mWrittenBytesInIonWriteBuffer = 0;
                stream->limit = static_cast<BYTE *>( mIonWriteBuffer.data() + mIonWriteBuffer.size() );
                if ( mIonWriteBuffer.size() < WRITE_BUFFER_INCREASE_STEP )
                {
                    mIonWriteBuffer.resize( WRITE_BUFFER_INCREASE_STEP );
                }
                stream->curr = static_cast<BYTE *>( &( *mIonWriteBuffer.begin() ) );
            }
            return IERR_OK;
        };

        ION_WRITER_OPTIONS options{};
        options.output_as_binary = 1;

        auto res = ion_writer_open_stream( &mIonWriter, &ionWriteCallback, &mIonWriteCallback, &options );
        if ( res != IERR_OK )
        {
            FWE_LOG_ERROR( "Could not open Ion writer stream because of error:  " + std::to_string( res ) )
            return false;
        }
        mWriterNeedsClose = true;
        return true;
    }

    void
    serializeMetadata()
    {
        if ( !openNewIonWriter() )
        {
            FWE_LOG_ERROR( "Abort serializeMetadata as Ion Writer could not be created" );
            return;
        }
        // clang-format off
        // coverity[autosar_cpp14_m5_2_9_violation] tid_STRUCT comes from library so cast is done in library header
        // coverity[misra_cpp_2008_rule_5_2_9_violation] tid_STRUCT comes from library so cast is done in library header
        // coverity[cert_exp57_cpp_violation] tid_STRUCT comes from library so cast is done in library header
        ION_CHECK( ion_writer_start_container( mIonWriter, tid_STRUCT ) ); // NOLINT(cppcoreguidelines-pro-type-const-cast, cppcoreguidelines-pro-type-cstyle-cast)
        // clang-format on

        ION_CHECK( ionWriteFieldStr(
            mIonWriter, &FIELD_NAME_SCHEMA[0], static_cast<size_t>( sizeof( FIELD_NAME_SCHEMA ) - 1 ) ) );
        std::string version = "1.0.1";
        ION_CHECK( ionWriteValueStr( mIonWriter, version.c_str(), version.size() ) );

        ION_CHECK( ionWriteFieldStr( mIonWriter,
                                     &FIELD_NAME_CAMPAIGN_SYNC_ID[0],
                                     static_cast<size_t>( sizeof( FIELD_NAME_CAMPAIGN_SYNC_ID ) - 1 ) ) );
        ION_CHECK( ionWriteValueStr( mIonWriter, mCollectionSchemeId.c_str(), mCollectionSchemeId.size() ) );

        ION_CHECK( ionWriteFieldStr( mIonWriter,
                                     &FIELD_NAME_DECODER_SYNC_ID[0],
                                     static_cast<size_t>( sizeof( FIELD_NAME_DECODER_SYNC_ID ) - 1 ) ) );
        ION_CHECK( ionWriteValueStr( mIonWriter, mDecoderManifestId.c_str(), mDecoderManifestId.size() ) );

        ION_CHECK( ionWriteFieldStr(
            mIonWriter, &FIELD_NAME_VEHICLE_NAME[0], static_cast<size_t>( sizeof( FIELD_NAME_VEHICLE_NAME ) - 1 ) ) );
        ION_CHECK( ionWriteValueStr( mIonWriter, mVehicleId.c_str(), mVehicleId.size() ) );

        ION_CHECK( ionWriteFieldStr( mIonWriter,
                                     &FIELD_NAME_COLLECTION_EVENT_ID[0],
                                     static_cast<size_t>( sizeof( FIELD_NAME_COLLECTION_EVENT_ID ) - 1 ) ) );
        ION_CHECK( ion_writer_write_int64( mIonWriter, mEventId ) );

        ION_CHECK( ionWriteFieldStr( mIonWriter,
                                     &FIELD_NAME_VEHICLE_EVENT_TIME[0],
                                     static_cast<size_t>( sizeof( FIELD_NAME_VEHICLE_EVENT_TIME ) - 1 ) ) );
        ION_CHECK( ion_writer_write_int64( mIonWriter, static_cast<int64_t>( mTriggerTime ) ) );

        ION_CHECK( ion_writer_finish_container( mIonWriter ) );
        ION_CHECK( ion_writer_finish( mIonWriter, nullptr ) );
    }

    void
    serializeOneRawBufferToIon( const FrameInfoForIon &frameInfo, const char *buffer, size_t size )
    {
        if ( !openNewIonWriter() )
        {
            FWE_LOG_ERROR( "Abort serializeMetadata as Ion Writer could not be created" );
            return;
        }
        // clang-format off
        // coverity[autosar_cpp14_m5_2_9_violation] tid_STRUCT comes from library so cast is done in library header
        // coverity[misra_cpp_2008_rule_5_2_9_violation] tid_STRUCT comes from library so cast is done in library header
        // coverity[cert_exp57_cpp_violation] tid_STRUCT comes from library so cast is done in library header
        ION_CHECK( ion_writer_start_container( mIonWriter, tid_STRUCT ) ); // NOLINT(cppcoreguidelines-pro-type-const-cast, cppcoreguidelines-pro-type-cstyle-cast)
        // clang-format on
        ION_CHECK( ionWriteFieldStr(
            mIonWriter, &FIELD_NAME_SIGNAL_ID[0], static_cast<size_t>( sizeof( FIELD_NAME_SIGNAL_ID ) - 1 ) ) );
        ION_CHECK( ion_writer_write_int32( mIonWriter, static_cast<int32_t>( frameInfo.mId ) ) );

        ION_CHECK( ionWriteFieldStr(
            mIonWriter, &FIELD_NAME_SIGNAL_NAME[0], static_cast<size_t>( sizeof( FIELD_NAME_SIGNAL_NAME ) - 1 ) ) );
        ION_CHECK( ionWriteValueStr( mIonWriter, frameInfo.mSignalName.c_str(), frameInfo.mSignalName.size() ) );

        ION_CHECK( ionWriteFieldStr(
            mIonWriter, &FIELD_NAME_SIGNAL_TYPE[0], static_cast<size_t>( sizeof( FIELD_NAME_SIGNAL_TYPE ) - 1 ) ) );
        ION_CHECK( ionWriteValueStr( mIonWriter, frameInfo.mSignalType.c_str(), frameInfo.mSignalType.size() ) );

        ION_CHECK( ionWriteFieldStr(
            mIonWriter, &FIELD_NAME_RELATIVE_TIME[0], static_cast<size_t>( sizeof( FIELD_NAME_RELATIVE_TIME ) - 1 ) ) );
        ION_CHECK( ion_writer_write_int64(
            mIonWriter, static_cast<int64_t>( frameInfo.mReceiveTime ) - static_cast<int64_t>( mTriggerTime ) ) );

        ION_CHECK( ionWriteFieldStr(
            mIonWriter, &FIELD_NAME_DATA_FORMAT[0], static_cast<size_t>( sizeof( FIELD_NAME_DATA_FORMAT ) - 1 ) ) );
        ION_STRING dataFormat;
        char dataFormatValue[] = "CDR";
        dataFormat.value = reinterpret_cast<BYTE *>( dataFormatValue );
        dataFormat.length = static_cast<SIZE>( sizeof( dataFormatValue ) - 1 );
        ION_CHECK( ion_writer_write_symbol( mIonWriter, &dataFormat ) );

        if ( size > MAX_BYTES_PER_BLOB )
        {
            FWE_LOG_ERROR( "Single frame with signalId " + std::to_string( frameInfo.mId ) + " has " +
                           std::to_string( size ) + " which is too big for Ion" );
        }
        else
        {
            ION_CHECK( ionWriteFieldStr(
                mIonWriter, &FIELD_NAME_SIGNAL_BLOB[0], static_cast<size_t>( sizeof( FIELD_NAME_SIGNAL_BLOB ) - 1 ) ) );
            // clang-format off
            // coverity[autosar_cpp14_a5_2_3_violation] ion-c needs non-const input
            // coverity[misra_cpp_2008_rule_5_2_5_violation] ion-c needs non-const input
            // coverity[cert_exp55_cpp_violation] ion-c needs non-const input
            ION_CHECK( ion_writer_write_blob(
                mIonWriter, reinterpret_cast<BYTE *>( const_cast<char *>( buffer ) ), static_cast<SIZE>( size ) ) ); // NOLINT(cppcoreguidelines-pro-type-const-cast) ion-c needs non-const input
            // clang-format on
        }
        ION_CHECK( ion_writer_finish_container( mIonWriter ) );

        ION_CHECK( ion_writer_finish( mIonWriter, nullptr ) );
    }

    hWRITER mIonWriter = nullptr;
    bool mWriterNeedsClose = false;
    IonWriteCallback mIonWriteCallback{};

    /** keeps one full Ion message in buffer*/
    std::vector<uint8_t> mIonWriteBuffer;
    size_t mWrittenBytesInIonWriteBuffer = { 0 };
    size_t mWrittenBytesInIonFullStream = { 0 };
    int64_t mOverallSize = -1;
    size_t mOutputStep = 0;
    std::vector<FrameInfoForIon> mFramesToSendOut;
    std::shared_ptr<RawData::BufferManager> mRawDataBufferManager;

    Timestamp mTriggerTime;
    EventID mEventId;
    SyncID mDecoderManifestId;
    SyncID mCollectionSchemeId;
    std::string mVehicleId;

    // Metadata:
    static constexpr char FIELD_NAME_SCHEMA[] = "ion_scheme_version";
    static constexpr char FIELD_NAME_CAMPAIGN_SYNC_ID[] = "campaign_sync_id";
    static constexpr char FIELD_NAME_DECODER_SYNC_ID[] = "decoder_sync_id";
    static constexpr char FIELD_NAME_VEHICLE_NAME[] = "vehicle_name";
    static constexpr char FIELD_NAME_COLLECTION_EVENT_ID[] = "collection_event_id";
    static constexpr char FIELD_NAME_VEHICLE_EVENT_TIME[] = "collection_event_time";

    // Frame:
    static constexpr char FIELD_NAME_SIGNAL_ID[] = "signal_id";
    static constexpr char FIELD_NAME_SIGNAL_NAME[] = "signal_name";
    static constexpr char FIELD_NAME_SIGNAL_TYPE[] = "signal_type";
    static constexpr char FIELD_NAME_RELATIVE_TIME[] = "relative_time";
    static constexpr char FIELD_NAME_DATA_FORMAT[] = "data_format";
    static constexpr char FIELD_NAME_SIGNAL_BLOB[] = "signal_byte_values";
};

class IonStreambufBuilder : public StreambufBuilder
{
public:
    IonStreambufBuilder( std::shared_ptr<RawData::BufferManager> rawDataBufferManager,
                         std::string vehicleId,
                         Timestamp triggerTime,
                         EventID eventId,
                         SyncID decoderManifestId,
                         SyncID collectionSchemeId )
        : mRawDataBufferManager( std::move( rawDataBufferManager ) )
        , mVehicleId( std::move( vehicleId ) )
        , mTriggerTime( triggerTime )
        , mEventId( eventId )
        , mDecoderManifestId( std::move( decoderManifestId ) )
        , mCollectionSchemeId( std::move( collectionSchemeId ) )
    {
    }

    std::unique_ptr<std::streambuf>
    build() override
    {
        auto generator = std::make_unique<IonFileGenerator>( mRawDataBufferManager,
                                                             mVehicleId,
                                                             mTriggerTime,
                                                             mEventId,
                                                             mDecoderManifestId,
                                                             mCollectionSchemeId,
                                                             mFramesToSendOut );
        if ( generator->getNumberOfBufferHandles() == 0 )
        {
            return nullptr;
        }
        return generator;
    }

    void
    appendFrame( FrameInfoForIon frameInfo )
    {
        mFramesToSendOut.push_back( frameInfo );
    }

private:
    std::shared_ptr<RawData::BufferManager> mRawDataBufferManager;
    std::string mVehicleId;
    Timestamp mTriggerTime;
    EventID mEventId;
    SyncID mDecoderManifestId;
    SyncID mCollectionSchemeId;

    std::vector<FrameInfoForIon> mFramesToSendOut;
};

// NOLINT below due to C++17 warning of redundant declarations that are required to maintain C++14 compatibility
constexpr char IonFileGenerator::FIELD_NAME_SCHEMA[];              // NOLINT
constexpr char IonFileGenerator::FIELD_NAME_CAMPAIGN_SYNC_ID[];    // NOLINT
constexpr char IonFileGenerator::FIELD_NAME_DECODER_SYNC_ID[];     // NOLINT
constexpr char IonFileGenerator::FIELD_NAME_VEHICLE_NAME[];        // NOLINT
constexpr char IonFileGenerator::FIELD_NAME_COLLECTION_EVENT_ID[]; // NOLINT
constexpr char IonFileGenerator::FIELD_NAME_VEHICLE_EVENT_TIME[];  // NOLINT

constexpr char IonFileGenerator::FIELD_NAME_SIGNAL_ID[];     // NOLINT
constexpr char IonFileGenerator::FIELD_NAME_SIGNAL_NAME[];   // NOLINT
constexpr char IonFileGenerator::FIELD_NAME_SIGNAL_TYPE[];   // NOLINT
constexpr char IonFileGenerator::FIELD_NAME_RELATIVE_TIME[]; // NOLINT
constexpr char IonFileGenerator::FIELD_NAME_DATA_FORMAT[];   // NOLINT
constexpr char IonFileGenerator::FIELD_NAME_SIGNAL_BLOB[];   // NOLINT

DataSenderIonWriter::DataSenderIonWriter( std::shared_ptr<RawData::BufferManager> rawDataBufferManager,
                                          std::string vehicleId )
    : mRawDataBufferManager( std::move( rawDataBufferManager ) )
    , mVehicleId( std::move( vehicleId ) )
{
}

std::unique_ptr<StreambufBuilder>
DataSenderIonWriter::getStreambufBuilder()
{
    mEstimatedBytesInCurrentStream = 0;
    return std::move( mCurrentStreamBuilder );
}

void
DataSenderIonWriter::onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                                 VehicleDataSourceProtocol networkProtocol )
{
    if ( networkProtocol == VehicleDataSourceProtocol::COMPLEX_DATA )
    {
        FWE_LOG_TRACE( "Ion Writer received decoder dictionary with complex data update" );
        auto decoderDictionaryPtr = std::dynamic_pointer_cast<const ComplexDataDecoderDictionary>( dictionary );
        {
            std::lock_guard<std::mutex> lock( mDecoderDictMutex );
            mCurrentDict = decoderDictionaryPtr;
        }
    }
}

DataSenderIonWriter::~DataSenderIonWriter() = default;

void
DataSenderIonWriter::setupVehicleData( std::shared_ptr<const TriggeredVisionSystemData> triggeredVisionSystemData )
{
    mEstimatedBytesInCurrentStream = ESTIMATED_SERIALIZED_EVENT_METADATA_BYTES; // reset and start new
    if ( mRawDataBufferManager != nullptr )
    {

        mCurrentStreamBuilder =
            std::make_unique<IonStreambufBuilder>( mRawDataBufferManager,
                                                   mVehicleId,
                                                   triggeredVisionSystemData->triggerTime,
                                                   triggeredVisionSystemData->eventID,
                                                   triggeredVisionSystemData->metadata.decoderID,
                                                   triggeredVisionSystemData->metadata.collectionSchemeID );
    }
    else
    {
        FWE_LOG_ERROR( "Ion Stream can not be started without Raw Data Buffer Manager" );
    }
}

bool
DataSenderIonWriter::fillFrameInfo( FrameInfoForIon &frame )
{
    if ( mCurrentDict != nullptr )
    {
        std::lock_guard<std::mutex> lock( mDecoderDictMutex );
        for ( auto &interface : mCurrentDict->complexMessageDecoderMethod )
        {
            for ( auto &message : interface.second )
            {
                if ( message.second.mSignalId == frame.mId )
                {
                    auto m = message.first;
                    if ( m.empty() )
                    {
                        return false;
                    }
                    auto firstColon = m.find( COMPLEX_DATA_MESSAGE_ID_SEPARATOR );
                    if ( firstColon != std::string::npos )
                    {
                        // Colon found in message id so treat messageID as 'topic:type'
                        frame.mSignalName = m.substr( 0, firstColon );
                        frame.mSignalType = m.substr( firstColon + 1, m.length() );
                    }
                    else
                    {
                        frame.mSignalName = m;
                    }

                    return true;
                }
            }
        }
    }
    return false;
}

void
DataSenderIonWriter::append( const CollectedSignal &signal )
{
    // Currently ION file only supports raw data
    if ( ( signal.value.type == SignalType::COMPLEX_SIGNAL ) && ( mCurrentStreamBuilder != nullptr ) )
    {
        if ( mRawDataBufferManager != nullptr )
        {
            // load frame only to get the size
            auto loanedRawDataFrame =
                mRawDataBufferManager->borrowFrame( signal.signalID, signal.value.value.uint32Val );
            if ( !loanedRawDataFrame.isNull() )
            {
                mEstimatedBytesInCurrentStream += loanedRawDataFrame.getSize();
                mEstimatedBytesInCurrentStream += ESTIMATED_SERIALIZED_FRAME_METADATA_BYTES;

                mRawDataBufferManager->increaseHandleUsageHint(
                    signal.signalID,
                    signal.value.value.uint32Val,
                    RawData::BufferHandleUsageStage::HANDED_OVER_TO_SENDER );
                mRawDataBufferManager->decreaseHandleUsageHint(
                    signal.signalID,
                    signal.value.value.uint32Val,
                    RawData::BufferHandleUsageStage::COLLECTION_INSPECTION_ENGINE_SELECTED_FOR_UPLOAD );
            }
        }
        else
        {
            FWE_LOG_WARN( "Raw Data Buffer not initalized so impossible to estimate size for signal: " +
                          std::to_string( signal.signalID ) );
        }
        FrameInfoForIon frameInfo{
            signal.signalID, signal.receiveTime, "UNKNOWN", "UNKNOWN", signal.value.value.uint32Val };
        if ( !fillFrameInfo( frameInfo ) )
        {
            FWE_LOG_WARN( "Could not find encoding for signalId: " + std::to_string( signal.signalID ) );
        }
        mCurrentStreamBuilder->appendFrame( frameInfo );
    }
    else
    {
        FWE_LOG_WARN( "Currently FW ION schema only supports raw data. Type: " +
                      std::to_string( static_cast<uint32_t>( signal.value.type ) ) +
                      " mCurrentStreamBuilder ptr: " + ( mCurrentStreamBuilder == nullptr ? "nullptr" : "valid" ) );
    }
}

} // namespace IoTFleetWise
} // namespace Aws
