// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CANInterfaceIDTranslator.h"
#include "CollectionInspectionAPITypes.h"
#include "DataSenderProtoWriter.h"
#include "IConnectionTypes.h"
#include "ISender.h"
#include "PayloadManager.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
#include "DataSenderIonWriter.h"
#include "ICollectionScheme.h"
#include "ICollectionSchemeList.h"
#include "S3Sender.h"
#include <functional>
#endif

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Class that implements data sender logic: data preprocessing and upload
 *
 * This class is not multithreading safe to the caller needs to ensure that the different functions
 * are called only from one thread. This class will be instantiated and used from the Data Sender
 * Manager Worker thread
 */
class DataSenderManager
{

public:
    DataSenderManager( std::shared_ptr<ISender> mqttSender,
                       std::shared_ptr<PayloadManager> payloadManager,
                       CANInterfaceIDTranslator &canIDTranslator,
                       unsigned transmitThreshold
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                       ,
                       std::shared_ptr<S3Sender> s3Sender,
                       std::shared_ptr<DataSenderIonWriter> ionWriter,
                       std::string vehicleName
#endif
    );

    virtual ~DataSenderManager() = default;

    /**
     * @brief Process collection scheme parameters and prepare data for upload
     */
    virtual void processCollectedData( const TriggeredCollectionSchemeDataPtr triggeredCollectionSchemeDataPtr
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
                                       ,
                                       std::function<void( TriggeredCollectionSchemeDataPtr )> reportUploadCallback
#endif
    );

    /**
     * @brief Retrieve all the persisted data and hand it over to the correct sender
     */
    virtual void checkAndSendRetrievedData();

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    virtual void onChangeCollectionSchemeList(
        const std::shared_ptr<const ActiveCollectionSchemes> &activeCollectionSchemes );
#endif

private:
    std::shared_ptr<ISender> mMQTTSender;
    std::shared_ptr<PayloadManager> mPayloadManager;
    DataSenderProtoWriter mProtoWriter;
#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    std::shared_ptr<DataSenderIonWriter> mIonWriter;
    std::shared_ptr<S3Sender> mS3Sender; // might be nullptr
    std::string mVehicleName;
    std::shared_ptr<const ActiveCollectionSchemes> mActiveCollectionSchemes;
#endif
    CollectionSchemeParams mCollectionSchemeParams;
    std::string mCollectionSchemeID;

    std::string mProtoOutput;
    std::string mCompressedProtoOutput;

    unsigned mTransmitThreshold{ 0 }; // max number of messages that can be sent to cloud at one time

    /**
     * @brief Set up collectionSchemeParams struct
     * @param triggeredCollectionSchemeDataPtr collected data
     */
    void setCollectionSchemeParameters( const TriggeredCollectionSchemeDataPtr &triggeredCollectionSchemeDataPtr );

    /**
     * @brief Put collected telemetry data into protobuf in chunks. Initiates serialization, compression, and
     * upload for each partition.
     * @param triggeredCollectionSchemeDataPtr collected data
     */
    void transformTelemetryDataToProto( const TriggeredCollectionSchemeDataPtr &triggeredCollectionSchemeDataPtr );

#ifdef FWE_FEATURE_VISION_SYSTEM_DATA
    /**
     * @brief Put vision system data into ion in chunks. Initiate serialization, compression, and
     * upload for each partition.
     * @param triggeredCollectionSchemeDataPtr collected data
     * @param uploadedDataCallback Callback after data has been successfully uploaded
     */
    void transformVisionSystemDataToIon(
        const TriggeredCollectionSchemeDataPtr &triggeredCollectionSchemeDataPtr,
        std::function<void( TriggeredCollectionSchemeDataPtr uploadedData )> uploadedDataCallback );

    S3UploadMetadata getS3UploadMetadataForCollectionScheme( const std::string &collectionSchemeID );
#endif

    /**
     * @brief Serializes, compresses, and uploads proto output.
     */
    void uploadProto();

    template <typename T>
    void
    appendMessageToProto( const TriggeredCollectionSchemeDataPtr &triggeredCollectionSchemeDataPtr, T msg )
    {
        mProtoWriter.append( msg );
        if ( mProtoWriter.getVehicleDataMsgCount() >= mTransmitThreshold )
        {
            uploadProto();
            // Setup the next payload chunk
            mProtoWriter.setupVehicleData( triggeredCollectionSchemeDataPtr, mCollectionSchemeParams.eventID );
        }
    }

    /**
     * @brief Serializes data
     * @param output Output string
     * @return True if serialization succeeds
     */
    bool serialize( std::string &output );

    /**
     * @brief Compresses data
     * @param input Input data string
     * @return True if compression succeeds
     */
    bool compress( std::string &input );

    /**
     * @brief Forwards data from buffer to the provided sender
     * @param data Data to send
     * @param size Buffer size
     * @param sender sender to use for the upload
     * @return Success if upload succeeds
     */
    ConnectivityError send( const std::uint8_t *data, size_t size, std::shared_ptr<ISender> sender );

    /**
     * @brief Upload file from persistency folder
     * @param filename File to send
     * @param size File size
     * @param collectionSchemeParams object containing collectionScheme related metadata for data persistency and
     * transmission
     * @return Success if upload succeeds
     */
    ConnectivityError uploadPersistedFile( const std::string &filename,
                                           size_t size,
                                           CollectionSchemeParams collectionSchemeParams );
};

} // namespace IoTFleetWise
} // namespace Aws
