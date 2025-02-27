// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "aws/iotfleetwise/Clock.h"
#include "aws/iotfleetwise/ClockHandler.h"
#include "aws/iotfleetwise/SchemaListener.h"
#include "aws/iotfleetwise/Signal.h"
#include "aws/iotfleetwise/SignalTypes.h"
#include "aws/iotfleetwise/Thread.h"
#include "aws/iotfleetwise/TimeTypes.h"
#include <atomic>
#include <boost/optional/optional.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace Aws
{
namespace IoTFleetWise
{

/**
 * @brief This thread sends the check-in messages to the Cloud
 *
 * For that to happen it needs to be notified whenever any of the documents relevant to check-in
 * changes.
 */
class CheckinSender
{
public:
    CheckinSender( std::shared_ptr<SchemaListener> schemaListener, uint32_t checkinIntervalMs = 0 );
    ~CheckinSender();

    CheckinSender( const CheckinSender & ) = delete;
    CheckinSender &operator=( const CheckinSender & ) = delete;
    CheckinSender( CheckinSender && ) = delete;
    CheckinSender &operator=( CheckinSender && ) = delete;

    /**
     * @brief Callback from CollectionSchemeManager to notify that the new scheme is available
     *
     * This needs to be called at least once, otherwise no checkin message will be sent.
     *
     * @param documents the list of documents that will be sent in the next checkin message
     * */
    void onCheckinDocumentsChanged( const std::vector<SyncID> &documents );

    /**
     * @brief stops the internal thread if started and wait until it finishes
     *
     * @return true if the stop was successful
     */
    bool stop();

    /**
     * @brief starts the internal thread
     *
     * @return true if the start was successful
     */
    bool start();

    /**
     * @brief Checks that the worker thread is healthy.
     */
    bool isAlive();

private:
    // default checkin interval set to 5 mins
    static constexpr uint32_t DEFAULT_CHECKIN_INTERVAL_IN_MILLISECOND = 300000;

    bool shouldStop() const;

    void doWork();

    boost::optional<Timestamp> getTimeToSendNextCheckin();
    void setTimeToSendNextCheckin( boost::optional<Timestamp> timeToSendNextCheckin );

    // Time interval in ms to send checkin message
    uint32_t mCheckinIntervalMs{ DEFAULT_CHECKIN_INTERVAL_IN_MILLISECOND };
    boost::optional<Timestamp> mTimeToSendNextCheckin;
    std::mutex mTimeToSendNextCheckinMutex;

    Thread mThread;
    std::atomic<bool> mShouldStop{ false };
    std::mutex mThreadMutex;
    Signal mWait;
    std::shared_ptr<const Clock> mClock = ClockHandler::getClock();

    // The list of checkin documents that will be sent in the next checkin message. If this optional
    // doesn't contain a value, then no checkin message will be sent.
    boost::optional<std::vector<SyncID>> mCheckinDocuments;
    std::mutex mCheckinDocumentsMutex;

    std::shared_ptr<SchemaListener> mSchemaListener;
};

} // namespace IoTFleetWise
} // namespace Aws
