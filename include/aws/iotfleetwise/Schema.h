// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CollectionSchemeIngestionList.h"
#include "aws/iotfleetwise/DecoderManifestIngestion.h"
#include "aws/iotfleetwise/ICollectionSchemeList.h"
#include "aws/iotfleetwise/IDecoderManifest.h"
#include "aws/iotfleetwise/IReceiver.h"
#include "aws/iotfleetwise/ISender.h"
#include "aws/iotfleetwise/Listener.h"
#include "aws/iotfleetwise/SchemaListener.h"
#include "aws/iotfleetwise/SignalTypes.h"
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
    Schema( IReceiver &receiverDecoderManifest, IReceiver &receiverCollectionSchemeList, ISender &sender );

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
     * @param callback callback that will be called when the operation completes (successfully or not).
     *                 IMPORTANT: The callback can be called by the same thread before sendBuffer even returns
     *                 or a separate thread, depending on whether the results are known synchronously or asynchronously.
     */
    void sendCheckin( const std::vector<SyncID> &documentARNs, OnCheckinSentCallback callback ) override;

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
    ISender &mMqttSender;

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
     * @param callback callback that will be called when the operation completes (successfully or not).
     *                 IMPORTANT: The callback can be called by the same thread before sendBuffer even returns
     *                 or a separate thread, depending on whether the results are known synchronously or asynchronously.
     */
    void transmitCheckin( OnCheckinSentCallback callback );
};

} // namespace IoTFleetWise
} // namespace Aws
