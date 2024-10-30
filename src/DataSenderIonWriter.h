// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CollectionInspectionAPITypes.h"
#include "IDecoderDictionary.h"
#include "RawDataManager.h"
#include "SignalTypes.h"
#include "StreambufBuilder.h"
#include "TimeTypes.h"
#include "VehicleDataSourceTypes.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

class IonStreambufBuilder; // hide ion dependencies in cpp file

struct FrameInfoForIon
{
    SignalID mId;
    Timestamp mReceiveTime;
    std::string mSignalName;
    std::string mSignalType;
    RawData::BufferHandle mHandle;
};

/**
 * @brief Class that creates a stream of all the frames appended to each other in the Ion format.
 * Currently always one full frame is held in the Ion format in memory
 */
// coverity[cert_dcl60_cpp_violation] false positive - class only defined once
// coverity[autosar_cpp14_m3_2_2_violation] false positive - class only defined once
// coverity[misra_cpp_2008_rule_3_2_2_violation] false positive - class only defined once
class DataSenderIonWriter
{
public:
    /**
     * @brief Constructor. Setup the DataSenderIonWriter.
     */
    DataSenderIonWriter( std::shared_ptr<RawData::BufferManager> rawDataBufferManager, std::string vehicleId );

    /**
     * @brief Destructor.
     */
    virtual ~DataSenderIonWriter();

    DataSenderIonWriter( const DataSenderIonWriter & ) = delete;
    DataSenderIonWriter &operator=( const DataSenderIonWriter & ) = delete;
    DataSenderIonWriter( DataSenderIonWriter && ) = delete;
    DataSenderIonWriter &operator=( DataSenderIonWriter && ) = delete;

    /**
     * @brief Starts a new set of data and a fresh stream
     *
     * @param triggeredVisionSystemData pointer to the collected data and metadata to be sent to cloud
     */
    virtual void setupVehicleData( std::shared_ptr<const TriggeredVisionSystemData> triggeredVisionSystemData );

    /**
     * @brief Hand over stream builder. After this to start a new stream setupVehicleData has to be called.
     *
     * Can only be called after setupVehicleData and only once. It hands over ownership of stream builder from
     * DataSenderIonWriter to caller of this function.
     * @return the only instance of the stream builder. The builder will return the real stream
     * when requested and after the stream is destructed buffer handles will be marked as not used
     * by data sender
     */
    virtual std::unique_ptr<StreambufBuilder> getStreambufBuilder();

    /**
     * @brief Appends the decoded raw frame handle to the stream generator
     *
     *  @param signal  only type SignalType::COMPLEX_SIGNAL will be accepted
     */
    virtual void append( const CollectedSignal &signal );

    /**
     * @brief As serialization did not happen at this point it is a rough estimation
     *
     * resets after calls to getStreambufBuilder() and setupVehicleData()
     *
     * @return the estimated number of bytes currently in the stream
     */
    uint64_t
    getEstimatedSizeInBytes() const
    {
        return mEstimatedBytesInCurrentStream;
    };

    /**
     * @brief To be called on dictionary updates
     * @param dictionary new dictionary with the topics to subscribe to and information how to decode
     * @param networkProtocol only COMPLEX_DATA will be accepted
     */
    void onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                     VehicleDataSourceProtocol networkProtocol );

private:
    bool fillFrameInfo( FrameInfoForIon &frame );
    std::unique_ptr<IonStreambufBuilder> mCurrentStreamBuilder;
    std::shared_ptr<RawData::BufferManager> mRawDataBufferManager;
    std::string mVehicleId;

    std::mutex mDecoderDictMutex;
    std::shared_ptr<const ComplexDataDecoderDictionary> mCurrentDict;
    uint64_t mEstimatedBytesInCurrentStream = 0;

    static constexpr uint64_t ESTIMATED_SERIALIZED_FRAME_METADATA_BYTES = 100;
    static constexpr uint64_t ESTIMATED_SERIALIZED_EVENT_METADATA_BYTES = 100;
};

} // namespace IoTFleetWise
} // namespace Aws
