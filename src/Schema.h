// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Clock.h"
#include "ClockHandler.h"
#include "CollectionSchemeIngestionList.h"
#include "DecoderManifestIngestion.h"
#include "ICollectionSchemeList.h"
#include "IDecoderManifest.h"
#include "IReceiver.h"
#include "ISender.h"
#include "Listener.h"
#include "SchemaListener.h"
#include "checkin.pb.h"
#include <cstddef>
#include <cstdint>
#include <functional>
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
class Schema : public SchemaListener
{
public:
    /**
     * @brief Callback function that Schema uses to notify CollectionSchemeManagement when a new CollectionScheme List
     * arrives from the Cloud.
     *
     * @param collectionSchemeList ICollectionSchemeList from CollectionScheme Ingestion
     */
    using OnCollectionSchemeUpdateCallback =
        std::function<void( const ICollectionSchemeListPtr &collectionSchemeList )>;

    /**
     * @brief Callback function that Schema uses to notify CollectionSchemeManagement when a new Decoder
     * Manifest arrives from the Cloud.
     *
     * @param decoderManifest IDecoderManifest from CollectionScheme Ingestion
     */
    using OnDecoderManifestUpdateCallback = std::function<void( const IDecoderManifestPtr &decoderManifest )>;

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

    void
    subscribeToCollectionSchemeUpdate( OnCollectionSchemeUpdateCallback callback )
    {
        mCollectionSchemeListeners.subscribe( callback );
    }

    void
    subscribeToDecoderManifestUpdate( OnDecoderManifestUpdateCallback callback )
    {
        mDecoderManifestListeners.subscribe( callback );
    }

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
     * @brief Callback that should be called whenever a new message with DecoderManifest is received from the Cloud.
     * @param buf Pointer to the data. It will be valid only until the callback returns.
     * @param size Size of the data
     */
    void onDecoderManifestReceived( const uint8_t *buf, size_t size );

    /**
     * @brief Callback that should be called whenever a new message with CollectionScheme is received from the Cloud.
     * @param buf Pointer to the data. It will be valid only until the callback returns.
     * @param size Size of the data
     */
    void onCollectionSchemeReceived( const uint8_t *buf, size_t size );

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

    ThreadSafeListeners<OnCollectionSchemeUpdateCallback> mCollectionSchemeListeners;
    ThreadSafeListeners<OnDecoderManifestUpdateCallback> mDecoderManifestListeners;

    /**
     * @brief Sends an mProtoCheckinMsgOutput string on the checkin topic
     * @return True if the Connectivity Module packed and send the data out of the process space.
     * It does not guarantee that the data reached the Checkin topic ( best effort QoS )
     */
    bool transmitCheckin();
};

} // namespace IoTFleetWise
} // namespace Aws
