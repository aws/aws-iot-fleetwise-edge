// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Includes
#include "ClockHandler.h"
#include "IActiveDecoderDictionaryListener.h"
#include "IVehicleDataConsumer.h"
#include "LoggingModule.h"
#include "Signal.h"
#include "Thread.h"
#include "Timer.h"
#include "businterfaces/AbstractVehicleDataSource.h"
#include <atomic>
#include <map>
#include <mutex>

namespace Aws
{
namespace IoTFleetWise
{
namespace DataInspection
{
using namespace Aws::IoTFleetWise::VehicleNetwork;
using namespace Aws::IoTFleetWise::Platform::Linux;

/**
 * @brief The binder is the entity responsible for :
 * 1- creating all consumers
 * 2- handing the circular buffers of the data sources to the consumers in a thread safe way.
 * 3- Manages the life cycle of the consumer thread
 * 4- receives interrupts when the data sources are disconnected and reflects that to the consumer state.
 */
class VehicleDataSourceBinder : public VehicleDataSourceListener, public IActiveDecoderDictionaryListener
{
public:
    VehicleDataSourceBinder() = default;
    ~VehicleDataSourceBinder() override;

    VehicleDataSourceBinder( const VehicleDataSourceBinder & ) = delete;
    VehicleDataSourceBinder &operator=( const VehicleDataSourceBinder & ) = delete;
    VehicleDataSourceBinder( VehicleDataSourceBinder && ) = delete;
    VehicleDataSourceBinder &operator=( VehicleDataSourceBinder && ) = delete;

    /**
     * @brief Adds a Vehicle Data Source.
     * @param source Pointer to the Vehicle Data Source.
     * @return True if the data source has been added.
     */
    bool addVehicleDataSource( VehicleDataSourcePtr source );
    /**
     * @brief removes a Vehicle Data Source
     * @param id Vehicle Data Source ID.
     * @return True if the data source has been removed.
     */
    bool removeVehicleDataSource( const VehicleDataSourceID &id );
    /**
     * @brief Binds a Vehicle Data Source to a consumer
     * @param id Vehicle Data Source ID.
     * @param consumer Pointer to the consumer
     * @return True if the circular buffer of the data source is handed over to the consumer.
     * Starts the consumer worker.
     */
    bool bindConsumerToVehicleDataSource( VehicleDataConsumerPtr consumer, const VehicleDataSourceID &id );
    /**
     * @brief unBinds a Vehicle Data Source from the consumer.
     * @param id Vehicle Data Source ID.
     * @return True of the consumer worker is stopped.
     */
    bool unBindConsumerFromVehicleDataSource( const VehicleDataSourceID &id );

    /**
     * @brief Reports runtime state of the binder.
     * @return True if alive. False otherwise.
     */
    bool isAlive();

    /**
     * @brief Connect the Binder by starting its worker thread.
     * @return True if successful. False otherwise.
     */
    bool connect();

    /**
     * @brief disconnects all Vehicle Data Sources and corresponding consumers. Then stop the worker
     * thread
     * @return True if successful. False otherwise.
     */
    bool disconnect();

    /**
     * @brief From IActiveDecoderDictionaryListener. Effectively listens to the CollectionScheme Management
     * module and propagate that gracefully to the data sources and consumers.
     */
    void onChangeOfActiveDictionary( ConstDecoderDictionaryConstPtr &dictionary,
                                     VehicleDataSourceProtocol networkProtocol ) override;

    using SourcesToConsumers = std::map<VehicleDataSourceID, VehicleDataConsumerPtr>;
    using IdsToDataSources = std::map<VehicleDataSourceID, VehicleDataSourcePtr>;
    using IdsToStates = std::map<VehicleDataSourceID, VehicleDataSourceState>;

private:
    // atomic state of the thread. If true, we should stop
    bool shouldStop() const;

    // Main work function
    static void doWork( void *data );

    /**
     * @brief starts the binder worker. Worker is ready to bind data sources to consumers.
     * @return True of the binder worker is running.
     */
    bool start();

    /**
     * @brief stops the binder worker.
     * @return True of the binder worker is stopped.
     */
    bool stop();

    // Listener Callbacks
    void onVehicleDataSourceConnected( const VehicleDataSourceID &id ) override;
    void onVehicleDataSourceDisconnected( const VehicleDataSourceID &id ) override;
    bool reConnectConsumer( const VehicleDataSourceID &id );
    bool disconnectConsumer( const VehicleDataSourceID &id );

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    mutable std::mutex mThreadMutex;
    mutable std::mutex mVehicleDataSourceUpdatesMutex;
    mutable std::mutex mVehicleDataSourcesMutex;
    mutable std::mutex mConsumersMutex;
    IdsToStates mDataSourceStates;
    Platform::Linux::Signal mWait;
    Timer mTimer;
    LoggingModule mLogger;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();
    SourcesToConsumers mDataSourcesToConsumers;
    IdsToDataSources mIdsToDataSources;
};
} // namespace DataInspection
} // namespace IoTFleetWise
} // namespace Aws
