// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "IConnectionTypes.h"
#include <boost/optional.hpp>
#include <functional>
#include <memory>
#include <string>

namespace Aws
{
namespace IoTFleetWise
{

struct TopicConfigArgs
{
    boost::optional<std::string> iotFleetWisePrefix;
    boost::optional<std::string> deviceShadowPrefix;
    boost::optional<std::string> commandsPrefix;
    boost::optional<std::string> jobsPrefix;
    std::string metricsTopic;
    std::string logsTopic;
};

struct TopicConfig
{
    const std::string iotFleetWisePrefix;
    const std::string deviceShadowPrefix;
    const std::string namedDeviceShadowPrefix;
    const std::string commandsPrefix;
    const std::string jobsPrefix;

    const std::string telemetryDataTopic;
    const std::string checkinsTopic;
    const std::string collectionSchemesTopic;
    const std::string decoderManifestTopic;
    const std::string metricsTopic;
    const std::string logsTopic;

#ifdef FWE_FEATURE_LAST_KNOWN_STATE
    const std::string lastKnownStateDataTopic;
    const std::string lastKnownStateConfigTopic;
#endif

#ifdef FWE_FEATURE_REMOTE_COMMANDS
    const std::string commandRequestTopic;
    const std::string commandResponseAcceptedTopic;
    const std::string commandResponseRejectedTopic;
#endif

#ifdef FWE_FEATURE_STORE_AND_FORWARD
    const std::string getPendingJobExecutionsTopic;
    const std::string getPendingJobExecutionsAcceptedTopic;
    const std::string getPendingJobExecutionsRejectedTopic;
    const std::string getJobExecutionAcceptedTopic;
    const std::string getJobExecutionRejectedTopic;
    const std::string updateJobExecutionAcceptedTopic;
    const std::string updateJobExecutionRejectedTopic;
    const std::string jobNotificationTopic;
    const std::string jobCancellationInProgressTopic;
#endif

    TopicConfig( const std::string &thingName, const TopicConfigArgs &topicConfigArgs )
        : iotFleetWisePrefix( topicConfigArgs.iotFleetWisePrefix.value_or( "$aws/iotfleetwise/" ) + "vehicles/" +
                              thingName + "/" )
        , deviceShadowPrefix( topicConfigArgs.deviceShadowPrefix.value_or( "$aws/things/" ) + thingName + "/shadow/" )
        , namedDeviceShadowPrefix( deviceShadowPrefix + "name/" )
        , commandsPrefix( topicConfigArgs.commandsPrefix.value_or( "$aws/commands/" ) + "things/" + thingName + "/" )
        , jobsPrefix( topicConfigArgs.jobsPrefix.value_or( "$aws/things/" ) + thingName + "/jobs/" )
        , telemetryDataTopic( iotFleetWisePrefix + "signals" )
        , checkinsTopic( iotFleetWisePrefix + "checkins" )
        , collectionSchemesTopic( iotFleetWisePrefix + "collection_schemes" )
        , decoderManifestTopic( iotFleetWisePrefix + "decoder_manifests" )
        , metricsTopic( topicConfigArgs.metricsTopic )
        , logsTopic( topicConfigArgs.logsTopic )
#ifdef FWE_FEATURE_LAST_KNOWN_STATE
        , lastKnownStateDataTopic( iotFleetWisePrefix + "last_known_states/data" )
        , lastKnownStateConfigTopic( iotFleetWisePrefix + "last_known_states/config" )
#endif
#ifdef FWE_FEATURE_REMOTE_COMMANDS
        , commandRequestTopic( commandsPrefix + "executions/+/request/protobuf" )
        , commandResponseAcceptedTopic( commandsPrefix + "executions/+/response/accepted/protobuf" )
        , commandResponseRejectedTopic( commandsPrefix + "executions/+/response/rejected/protobuf" )
#endif
#ifdef FWE_FEATURE_STORE_AND_FORWARD
        , getPendingJobExecutionsTopic( jobsPrefix + "get" )
        , getPendingJobExecutionsAcceptedTopic( jobsPrefix + "get/accepted" )
        , getPendingJobExecutionsRejectedTopic( jobsPrefix + "get/rejected" )
        , getJobExecutionAcceptedTopic( jobsPrefix + "+/get/accepted" )
        , getJobExecutionRejectedTopic( jobsPrefix + "+/get/rejected" )
        , updateJobExecutionAcceptedTopic( jobsPrefix + "+/update/accepted" )
        , updateJobExecutionRejectedTopic( jobsPrefix + "+/update/rejected" )
        , jobNotificationTopic( jobsPrefix + "notify" )
        , jobCancellationInProgressTopic( "$aws/events/job/+/cancellation_in_progress" )
#endif
    {
    }

    ~TopicConfig() = default;

#ifdef FWE_FEATURE_REMOTE_COMMANDS
    std::string
    commandResponseTopic( const std::string &commandId ) const
    {
        return commandsPrefix + "executions/" + commandId + "/response/protobuf";
    }
#endif

#ifdef FWE_FEATURE_STORE_AND_FORWARD
    std::string
    getJobExecutionTopic( const std::string &jobId ) const
    {
        return jobsPrefix + jobId + "/get";
    }

    std::string
    updateJobExecutionTopic( const std::string &jobId ) const
    {
        return jobsPrefix + jobId + "/update";
    }
#endif

#ifdef FWE_FEATURE_SOMEIP
    std::string
    getDeviceShadowTopic( const std::string &shadowName ) const
    {
        return ( shadowName.empty() ? deviceShadowPrefix : namedDeviceShadowPrefix + shadowName + "/" ) + "get";
    }

    std::string
    updateDeviceShadowTopic( const std::string &shadowName ) const
    {
        return ( shadowName.empty() ? deviceShadowPrefix : namedDeviceShadowPrefix + shadowName + "/" ) + "update";
    }

    std::string
    deleteDeviceShadowTopic( const std::string &shadowName ) const
    {
        return ( shadowName.empty() ? deviceShadowPrefix : namedDeviceShadowPrefix + shadowName + "/" ) + "delete";
    }
#endif
};

} // namespace IoTFleetWise
} // namespace Aws
