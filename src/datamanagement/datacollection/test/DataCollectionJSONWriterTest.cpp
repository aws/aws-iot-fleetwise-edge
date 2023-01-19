// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <boost/filesystem.hpp>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <json/json.h>
#include <math.h>
#include <snappy.h>
#include <sstream>

#include "DataCollectionJSONWriter.h"

using namespace Aws::IoTFleetWise::DataManagement;
using namespace Aws::IoTFleetWise::Platform::Linux;

using ::testing::HasSubstr;

namespace
{

static constexpr int collectionEventID = 10;
static constexpr uint64_t testTimestamp = 1000;
static constexpr char EVENT_KEY[] = "Event";
static constexpr char MESSAGES_KEY[] = "Messages";
static constexpr char SIGNAL_KEY[] = "CapturedSignal";
static constexpr char SIGNAL_VAL_KEY[] = "doubleValue";
static constexpr char SIGNAL_TIME_KEY[] = "relativeTimeMS";
static constexpr char SIGNAL_ID_KEY[] = "signalID";
static constexpr char COLLECTION_EVENT_ID_KEY[] = "collectionEventID";

static constexpr char JSON_FILE_EXT[] = "json";
static constexpr char SNAPPY_FILE_EXT[] = "snappy";

/**
 * @brief Helper function to read the binary contents of a file.
 *
 * @param fs File stream
 * @return std::string Contents of the file
 */
std::string
readFile( std::ifstream &fs )
{
    std::stringstream buffer;
    buffer << fs.rdbuf();
    return buffer.str();
}

/**
 * @brief Assert that the JSON value conforms to the expected signals
 *
 * @param expectedSignals The expected signals to compare against
 * @param root The root of the JSON document to compare
 */
void
assertEventData( const std::vector<CollectedSignal> &expectedSignals, const Json::Value &root )
{
    const auto &event = root[EVENT_KEY];
    ASSERT_EQ( collectionEventID, event[COLLECTION_EVENT_ID_KEY].asInt() );
    const auto &messages = root[MESSAGES_KEY];
    ASSERT_EQ( messages.size(), expectedSignals.size() );
    for ( size_t i = 0; i < messages.size(); ++i )
    {
        const auto &msg = messages[static_cast<Json::ArrayIndex>( i )];
        const auto &sig = msg[SIGNAL_KEY];
        const auto &expectedSig = expectedSignals[i];
        ASSERT_DOUBLE_EQ( expectedSig.value, sig[SIGNAL_VAL_KEY].asDouble() );
        ASSERT_EQ( expectedSig.receiveTime, sig[SIGNAL_TIME_KEY].asInt() );
        ASSERT_DOUBLE_EQ( expectedSig.signalID, sig[SIGNAL_ID_KEY].asInt() );
    }
}

} // namespace

class DataCollectionJSONWriterTest : public ::testing::TestWithParam<std::tuple<size_t, bool>>
{
protected:
    void
    TearDown() override
    {
        // remove the file if it exists
        std::ifstream f( mFileName.c_str() );
        if ( f.good() )
        {
            std::remove( mFileName.c_str() );
        }
    }

    /**
     * @brief Helper function for test cases to assert the event data written to JSON.
     *
     * @param signals Expected signals to be present in the output files
     * @param expectedToCompress Whether compression is expected or not.
     */
    void
    testEventData( const std::vector<CollectedSignal> &signals, bool expectedToCompress )
    {
        DataCollectionJSONWriter writer{ mTmpDir.generic_string() };
        writer.setupEvent( schemaDataPtr, collectionEventID );
        for ( const auto &sig : signals )
        {
            writer.append( sig );
        }

        const auto res = writer.flushToFile();
        mFileName = res.first.filename().generic_string();
        EXPECT_THAT( mFileName, HasSubstr( std::to_string( collectionEventID ) ) );
        ASSERT_EQ( expectedToCompress, res.second );

        auto fileCloser = []( std::ifstream *fs ) { fs->close(); };
        std::ifstream fs( res.first.generic_string(), std::ifstream::binary );
        std::unique_ptr<std::ifstream, decltype( fileCloser )> fh( &fs, fileCloser );
        ASSERT_TRUE( fs.good() );

        auto data = readFile( fs );
        std::string inData;
        if ( schemaDataPtr->metaData.compress )
        {
            EXPECT_THAT( mFileName, HasSubstr( SNAPPY_FILE_EXT ) );
            const bool status = snappy::Uncompress( data.data(), data.size(), &inData );
            ASSERT_TRUE( status );
        }
        else
        {
            EXPECT_THAT( mFileName, HasSubstr( JSON_FILE_EXT ) );
            inData = std::move( data );
        }

        Json::Value root;
        Json::Reader reader;
        ASSERT_TRUE( reader.parse( inData, root ) );
        ASSERT_FALSE( root.empty() );
        assertEventData( signals, root );
    }

    std::shared_ptr<TriggeredCollectionSchemeData> schemaDataPtr = std::make_shared<TriggeredCollectionSchemeData>();
    std::string mFileName;
    boost::filesystem::path mTmpDir = boost::filesystem::temp_directory_path();
};

/**
 * @brief Tests the event writing to JSON for
 * 1. Different signals size
 * 2. with/without compression
 *
 */
TEST_P( DataCollectionJSONWriterTest, TestSingleEventWithSignals )
{
    const size_t numSignals = std::get<0>( GetParam() );
    schemaDataPtr->metaData.compress = std::get<1>( GetParam() );

    constexpr uint64_t signalT = 2000;
    constexpr double signalVal = 1.0;
    std::vector<CollectedSignal> signals;
    for ( size_t i = 0; i < numSignals; ++i )
    {
        signals.emplace_back( i + 1, signalT + ( i * 1000 ), signalVal );
    }

    testEventData( signals, schemaDataPtr->metaData.compress );
}

INSTANTIATE_TEST_CASE_P( DataCollectionJSONWriterTestSuite,
                         DataCollectionJSONWriterTest,
                         ::testing::Values( std::make_tuple( 0, false ),
                                            std::make_tuple( 0, true ),
                                            std::make_tuple( 1, false ),
                                            std::make_tuple( 10, false ),
                                            std::make_tuple( 1, false ),
                                            std::make_tuple( 10, true ) ) );
