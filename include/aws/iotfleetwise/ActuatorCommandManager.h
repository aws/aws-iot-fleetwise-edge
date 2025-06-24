// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/CommandTypes.h"
#include "aws/iotfleetwise/DataSenderTypes.h"
#include "aws/iotfleetwise/ICommandDispatcher.h"
#include "aws/iotfleetwise/IDecoderManifest.h"
#include "aws/iotfleetwise/RawDataManager.h"
#include "aws/iotfleetwise/Signal.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/Thread.h"
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief Class that implements logic of remote command request processing
 *
 * This class is notified on the arrival of the new Decoder Manifest and Command Request. When new Command Request is
 * received, Command Manager will queue the command for the execution (FIFO). Command execution timeout and
 * preconditions are checked in this thread. After Command Dispatcher is finished with the command execution, Command
 * Manager will queue Command Response for the upload.
 */
class ActuatorCommandManager
{
public:
    ActuatorCommandManager( std::shared_ptr<DataSenderQueue> commandResponses,
                            size_t maxConcurrentCommandRequests,
                            RawData::BufferManager *rawDataBufferManager );

    ~ActuatorCommandManager();

    ActuatorCommandManager( const ActuatorCommandManager & ) = delete;
    ActuatorCommandManager &operator=( const ActuatorCommandManager & ) = delete;
    ActuatorCommandManager( ActuatorCommandManager && ) = delete;
    ActuatorCommandManager &operator=( ActuatorCommandManager && ) = delete;

    /**
     * @brief Sets up connection and start main thread.
     * @return True if successful. False otherwise.
     */
    bool start();

    /**
     * @brief Disconnect and stops main thread.
     * @return True if successful. False otherwise.
     */
    bool stop();

    /**
     * @brief Checks that the worker thread is healthy and consuming data.
     */
    bool isAlive();

    void onChangeOfCustomSignalDecoderFormatMap(
        const SyncID &currentDecoderManifestID,
        std::shared_ptr<const SignalIDToCustomSignalDecoderFormatMap> customSignalDecoderFormatMap );

    /**
     * @brief callback to be invoked to receive command request
     */
    void onReceivingCommandRequest( const ActuatorCommandRequest &commandRequest );

    /**
     * Register a command dispatcher for a given interface ID
     * @param interfaceId Network interface ID
     * @param dispatcher Command dispatcher
     * @return True if registered, false if another dispatcher was already registered for the given
     * interface ID
     */
    bool registerDispatcher( const std::string &interfaceId, std::shared_ptr<ICommandDispatcher> dispatcher );

    /**
     * @brief Gets the actuator names supported by the command dispatchers
     * @todo The decoder manifest doesn't yet have an indication of whether a signal is
     * READ/WRITE/READ_WRITE. Until it does this interface is needed to get the names of the
     * actuators supported by the command dispatcher, so that for string signals, buffers can be
     * pre-allocated in the RawDataManager by the CollectionSchemeManager when a new decoder
     * manifest arrives. When the READ/WRITE/READ_WRITE usage of a signal is available this
     * interface can be removed.
     * @return Actuator names per interface ID
     */
    std::unordered_map<InterfaceID, std::vector<std::string>> getActuatorNames();

private:
    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::mutex mThreadMutex;
    Signal mWait;

    std::mutex mCommandRequestMutex;

    std::mutex mCustomSignalDecoderFormatMapUpdateMutex;
    std::shared_ptr<const SignalIDToCustomSignalDecoderFormatMap> mCustomSignalDecoderFormatMap;

    /**
     * @brief Internal command manager queue to process multiple commands
     */
    std::queue<ActuatorCommandRequest> mCommandRequestQueue;

    /**
     * @brief Maximum amount of elements mCommandRequestQueue can contain
     */
    size_t mMaxConcurrentCommandRequests{ 0 };

    /**
     * @brief Queue shared with the Data Sender containing Command Response to upload to the cloud
     */
    std::shared_ptr<DataSenderQueue> mCommandResponses;

    /**
     * @brief Clock member variable used to generate the time a command request was received
     */
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    /**
     * @brief Map of suported Command dispatchers to call to dispatch command request to the underlying vehicle service.
     */
    std::unordered_map<InterfaceID, std::shared_ptr<ICommandDispatcher>> mInterfaceIDToCommandDispatcherMap;

    /**
     * @brief Sync ID of the used decoder manifest to compare with the sync ID from the command request
     */
    SyncID mCurrentDecoderManifestID;

    RawData::BufferManager *mRawDataBufferManager;

    // Stop the  thread
    bool shouldStop() const;

    void doWork();

    /**
     * @brief Function with the main logic for command processing, including timeout ad precondition checks
     */
    void processCommandRequest( const ActuatorCommandRequest &commandRequest );

    /**
     * @brief Adds command response object to the queue for the Data Sender to upload
     */
    void queueCommandResponse( const ActuatorCommandRequest &commandRequest,
                               CommandStatus commandStatus,
                               CommandReasonCode reasonCode,
                               const CommandReasonDescription &reasonDescription );
};

} // namespace IoTFleetWise
} // namespace Aws
