// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Includes
#include "DataCollectionJSONWriter.h"
#include <snappy.h>

#include <fstream>
#include <iostream>
#include <sstream>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataManagement
{

namespace
{
constexpr char JSON_FILE_EXT[] = ".json";
constexpr char SNAPPY_FILE_EXT[] = ".snappy";

constexpr char EVENT_KEY[] = "Event";
constexpr char COLLECTION_SCHEME_ARN_KEY[] = "collectionSchemeARN";
constexpr char DECODER_ARN_KEY[] = "decoderARN";
constexpr char COLLECTION_EVENT_ID_KEY[] = "collectionEventID";
constexpr char COLLECTION_EVENT_TIME_KEY[] = "collectionEventTimeMSEpoch";

constexpr char PATH_SEP = '/';

} // namespace

DataCollectionJSONWriter::DataCollectionJSONWriter( std::string persistencyPath )
    : mPersistencyPath( std::move( persistencyPath ) )
{
}

void
DataCollectionJSONWriter::append( const CollectedCanRawFrame &msg )
{
    Json::Value message;
    message["CANFrame"]["messageID"] = (Json::UInt)msg.frameID;
    message["CANFrame"]["nodeID"] = (Json::UInt)msg.channelId;
    message["CANFrame"]["relativeTimeMS"] = ( Json::Int64 )( ( msg.receiveTime ) - mTriggerTime );

    Json::Value signals( Json::arrayValue );
    Json::Value signal;
    for ( size_t i = 0; i < msg.size; ++i )
    {
        signal["CANFrame"]["byteValue"] = msg.data[i];
        signals.append( signal );
    }
    message["CANFrame"]["byteValues"] = signals;
    mMessages.append( message );
}

void
DataCollectionJSONWriter::append( const CollectedSignal &msg )
{
    Json::Value message;
    message["CapturedSignal"]["signalID"] = (Json::UInt)msg.signalID;
    message["CapturedSignal"]["relativeTimeMS"] = ( Json::Int64 )( ( msg.receiveTime ) - mTriggerTime );
    message["CapturedSignal"]["doubleValue"] = msg.value;
    mMessages.append( message );
}

void
DataCollectionJSONWriter::append( const GeohashInfo &geohashInfo )
{
    Json::Value message;
    message["Geohash"] = geohashInfo.mGeohashString;
    message["PrevReportedGeohash"] = geohashInfo.mPrevReportedGeohashString;
    mMessages.append( message );
}

std::pair<boost::filesystem::path, bool>
DataCollectionJSONWriter::flushToFile()
{
    std::string eventId = "NoEventID";
    if ( mEvent.isMember( EVENT_KEY ) && mEvent[EVENT_KEY].isMember( COLLECTION_EVENT_ID_KEY ) )
    {
        eventId = std::to_string( mEvent[EVENT_KEY][COLLECTION_EVENT_ID_KEY].asUInt() );
    }

    static constexpr char SEP = '-';
    std::ostringstream oss;
    oss << "IoTFleetWise" << SEP << "Data" << SEP << eventId << SEP
        << std::to_string( mClock->systemTimeSinceEpochMs() );
    return flushToFile( oss.str() );
}

std::pair<boost::filesystem::path, bool>
DataCollectionJSONWriter::flushToFile( const std::string &fileBaseName )
{
    constexpr char FUNC_NAME[] = "DataCollectionJSONWriter::flushToFile";

    mLogger.trace( FUNC_NAME, "Base file name: " + fileBaseName );
    mEvent["Messages"] = mMessages;
    Json::StreamWriterBuilder builder;
    builder["commentStyle"] = "None";
    builder["indentation"] = "   "; // or whatever you like
    std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
    std::ostringstream ss;
    writer->write( mEvent, &ss );

    auto data = ss.str();
    std::string outData;
    bool didCompress = false;
    if ( mShouldCompress )
    {
        mLogger.trace( FUNC_NAME, "File contents will be compressed" );
        if ( snappy::Compress( data.data(), data.size(), &outData ) == 0u )
        {
            mLogger.warn( FUNC_NAME, "Compression resulted in 0 bytes. Writing uncompressed file content" );
            outData = std::move( data );
        }
        else
        {
            didCompress = true;
        }
    }
    else
    {
        mLogger.trace( FUNC_NAME, "File contents will be uncompressed" );
        outData = std::move( data );
    }

    auto fileCloser = []( std::ofstream *fs ) { fs->close(); };
    std::ofstream ofs;
    std::unique_ptr<std::ofstream, decltype( fileCloser )> fileHandle( &ofs, fileCloser );

    const std::string fileExt = didCompress ? SNAPPY_FILE_EXT : JSON_FILE_EXT;
    const auto fileNameWithExt = mPersistencyPath + PATH_SEP + fileBaseName + fileExt;
    ofs.open( fileNameWithExt, std::ios::out | std::ios::binary );
    ofs.write( outData.c_str(), static_cast<std::streamsize>( outData.size() ) );
    mLogger.trace( FUNC_NAME, "File write completed. File: " + fileNameWithExt );

    return std::make_pair( boost::filesystem::path( fileNameWithExt ), didCompress );
}

void
DataCollectionJSONWriter::setupEvent( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeData,
                                      uint32_t collectionEventID )
{
    mEvent.clear();
    mMessages.clear();
    mEvent[EVENT_KEY][COLLECTION_SCHEME_ARN_KEY] = triggeredCollectionSchemeData->metaData.collectionSchemeID;
    mEvent[EVENT_KEY][DECODER_ARN_KEY] = triggeredCollectionSchemeData->metaData.decoderID;
    mEvent[EVENT_KEY][COLLECTION_EVENT_ID_KEY] = (Json::UInt)collectionEventID;
    mEvent[EVENT_KEY][COLLECTION_EVENT_TIME_KEY] = ( Json::UInt64 )( triggeredCollectionSchemeData->triggerTime );
    mTriggerTime = triggeredCollectionSchemeData->triggerTime;
    mShouldCompress = triggeredCollectionSchemeData->metaData.compress;
}

unsigned
DataCollectionJSONWriter::getJSONMessageCount() const
{
    return mMessages.size();
}

} // namespace DataManagement
} // namespace IoTFleetWise
} // namespace Aws
