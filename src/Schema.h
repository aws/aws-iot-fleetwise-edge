// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionSchemeIngestionList.h"
#include "CollectionSchemeManagementListener.h"
#include "DecoderManifestIngestion.h"
#include "IReceiver.h"
#include "ISender.h"
#include "Listener.h"
#include "LoggingModule.h"
#include "SchemaListener.h"
#include "checkin.pb.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/// Use shared pointers to the derived class
using CollectionSchemeListPtr = std::shared_ptr<CollectionSchemeIngestionList>;
using DecoderManifestPtr = std::shared_ptr<DecoderManifestIngestion>;

/**
 * @brief This class handles the receipt of Decoder Manifests and CollectionScheme Lists from the Cloud.
 */
class Schema : public ThreadListeners<CollectionSchemeManagementListener>, public SchemaListener
{
public:
    /**
     * @brief Constructor for the Schema class that handles receiving CollectionSchemes and DecoderManifest protobuffers
     * from Cloud and sending them to CollectionSchemeManagement.
     *
     * @param receiverDecoderManifest Receiver for a decoder_manifest.proto message on a DecoderManifest topic
     * @param receiverCollectionSchemeList Receiver for a collectionSchemes.proto message on a CollectionSchemes topic
     * @param sender ISender interface with MQTT topic set to the checkin topic for sending Checkins to the Cloud
     */
    Schema( std::shared_ptr<IReceiver> receiverDecoderManifest,
            std::shared_ptr<IReceiver> receiverCollectionSchemeList,
            std::shared_ptr<ISender> sender );

    ~Schema() override = default;

    Schema( const Schema & ) = delete;
    Schema &operator=( const Schema & ) = delete;
    Schema( Schema && ) = delete;
    Schema &operator=( Schema && ) = delete;

    /**
     * @brief Sends CollectionScheme List to CollectionScheme Management
     *
     * @param collectionSchemeListPtr A shared pointer of a collectionScheme list object received from Cloud containing
     * the binary data packed inside it.
     */
    void setCollectionSchemeList( const CollectionSchemeListPtr collectionSchemeListPtr );

    /**
     * @brief Sends DecoderManifest to CollectionScheme Management
     *
     * @param decoderManifestPtr A shared pointer of a Decoder Manifest object received from Cloud containing the binary
     * data packed inside it.
     */
    void setDecoderManifest( const DecoderManifestPtr decoderManifestPtr );

    /**
     * @brief Send a Checkin message to the cloud that includes the active decoder manifest and schemes currently in the
     * system.
     *
     * @param documentARNs List of the ARNs
     * @return True if the message has been sent. False otherwise.
     */
    bool sendCheckin( const std::vector<std::string> &documentARNs ) override;

private:
    /**
     * @brief This struct is used to receive the callback from MQTT IoT Core on receipt of data on the DecoderManifest
     * topic
     */
    struct DecoderManifestCb : IReceiverCallback
    {
        Schema &mSchema; //< Member variable to the Schema object which will receive the data

        /**
         * @brief Constructor that will initialize the member variables
         *
         * @param collectionSchemeIngestion Reference to a Schema object which allow this struct to pass data to
         */
        DecoderManifestCb( Schema &collectionSchemeIngestion )
            : mSchema( collectionSchemeIngestion )
        {
        }

        void
        onDataReceived( const uint8_t *buf, size_t size ) override
        {
            // Check for a empty input data
            if ( ( buf == nullptr ) || ( size == 0 ) )
            {
                FWE_LOG_ERROR( "Received empty CollectionScheme List data from Cloud" );
                return;
            }

            // Create an empty shared pointer which we'll copy the data to
            DecoderManifestPtr decoderManifestPtr = std::make_shared<DecoderManifestIngestion>();

            // Try to copy the binary data into the decoderManifest object
            if ( !decoderManifestPtr->copyData( buf, size ) )
            {
                FWE_LOG_ERROR( "DecoderManifest copyData from IoT core failed" );
                return;
            }

            // Successful copy, so we cache the decoderManifest in the Schema object
            mSchema.setDecoderManifest( decoderManifestPtr );
            FWE_LOG_TRACE( "Received Decoder Manifest in PI DecoderManifestCb" );
        }
    };

    DecoderManifestCb mDecoderManifestCb;

    /**
     * @brief This struct is used to receive the callback from MQTT IoT Core on receipt of data on the
     * CollectionSchemeList topic
     */
    struct CollectionSchemeListCb : IReceiverCallback
    {
        Schema &mSchema; //< Member variable to the Schema object which will receive the data

        /**
         * @brief Constructor that will initialize the member variables
         *
         * @param collectionSchemeIngestion Reference to a Schema object which allow this struct to pass data to
         */
        CollectionSchemeListCb( Schema &collectionSchemeIngestion )
            : mSchema( collectionSchemeIngestion )
        {
        }

        void
        onDataReceived( const uint8_t *buf, size_t size ) override
        {
            // Check for a empty input data
            if ( ( buf == nullptr ) || ( size == 0 ) )
            {
                FWE_LOG_ERROR( "Received empty CollectionScheme List data from Cloud" );
                return;
            }

            // Create an empty shared pointer which we'll copy the data to
            CollectionSchemeListPtr collectionSchemeListPtr = std::make_shared<CollectionSchemeIngestionList>();

            // Try to copy the binary data into the collectionSchemeList object
            if ( !collectionSchemeListPtr->copyData( buf, size ) )
            {
                FWE_LOG_ERROR( "CollectionSchemeList copyData from IoT core failed" );
                return;
            }

            // Successful copy, so we cache the collectionSchemeList in the Schema object
            mSchema.setCollectionSchemeList( collectionSchemeListPtr );
            FWE_LOG_TRACE( "Received CollectionSchemeList" );
        }
    };

    CollectionSchemeListCb mCollectionSchemeListCb;

    /**
     * @brief ISender object used to interface with cloud to send Checkins
     */
    std::shared_ptr<ISender> mSender;

    /**
     * @brief CheckinMsg member variable used to hold the checkin data and minimize heap fragmentation
     */
    Schemas::CheckinMsg::Checkin mProtoCheckinMsg;

    /**
     * @brief Holds the serialized output of the checkin message to minimize heap fragmentation
     */
    std::string mProtoCheckinMsgOutput;

    /**
     * @brief Clock member variable used to generate the time a checkin was sent
     */
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    /**
     * @brief Sends an mProtoCheckinMsgOutput string on the checkin topic
     * @return True if the Connectivity Module packed and send the data out of the process space.
     * It does not guarantee that the data reached the Checkin topic ( best effort QoS )
     */
    bool transmitCheckin();
};

} // namespace IoTFleetWise
} // namespace Aws
