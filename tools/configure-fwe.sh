#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

# Allow options to also be set as env vars, but set default values if missing
: ${INPUT_CONFIG_FILE:=""}
: ${OUTPUT_CONFIG_FILE:=""}
: ${CERTIFICATE:=""}
: ${CERTIFICATE_FILE:="/etc/aws-iot-fleetwise/certificate.pem"}
: ${PRIVATE_KEY:=""}
: ${PRIVATE_KEY_FILE:="/etc/aws-iot-fleetwise/private-key.key"}
: ${VEHICLE_NAME:=""}
: ${ENDPOINT_URL:=""}
: ${CAN_BUS0:=""}
: ${LOG_LEVEL:="Info"}
: ${LOG_COLOR:="Auto"}
: ${PERSISTENCY_PATH:="/var/aws-iot-fleetwise/"}
: ${TOPIC_PREFIX:="\$aws/iotfleetwise/"}
: ${CONNECTION_TYPE:="iotCore"}
: ${KEEP_ALIVE_INTERVAL_SECONDS:="60"}
: ${PING_TIMEOUT_MS:="30000"}
: ${SESSION_EXPIRY_INTERVAL_SECONDS:="0"}
: ${CREDS_ENDPOINT_URL:=""}
: ${CREDS_ROLE_ALIAS:=""}
: ${RAW_DATA_BUFFER_SIZE:=1073741824}

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --input-config-file)
            INPUT_CONFIG_FILE=$2
            shift
            ;;
        --output-config-file)
            OUTPUT_CONFIG_FILE=$2
            shift
            ;;
        --connection-type)
            CONNECTION_TYPE=$2
            shift
            ;;
        --keep-alive-interval-seconds)
            KEEP_ALIVE_INTERVAL_SECONDS=$2
            shift
            ;;
        --ping-timeout-ms)
            PING_TIMEOUT_MS=$2
            shift
            ;;
        --session-expiry-interval-seconds)
            SESSION_EXPIRY_INTERVAL_SECONDS=$2
            shift
            ;;
        --vehicle-name)
            VEHICLE_NAME=$2
            shift
            ;;
        --endpoint-url)
            ENDPOINT_URL=$2
            shift
            ;;
        --certificate)
            CERTIFICATE=$2
            shift
            ;;
        --certificate-file)
            CERTIFICATE_FILE=$2
            shift
            ;;
        --private-key)
            PRIVATE_KEY=$2
            shift
            ;;
        --private-key-file)
            PRIVATE_KEY_FILE=$2
            shift
            ;;
        --can-bus0)
            CAN_BUS0=$2
            shift
            ;;
        --persistency-path)
            PERSISTENCY_PATH=$2
            shift
            ;;
        --topic-prefix)
            TOPIC_PREFIX=$2
            shift
            ;;
        --log-level)
            LOG_LEVEL=$2
            shift
            ;;
        --log-color)
            LOG_COLOR=$2
            shift
            ;;
        --creds-endpoint-url)
            CREDS_ENDPOINT_URL=$2
            shift
            ;;
        --creds-role-alias)
            CREDS_ROLE_ALIAS=$2
            shift
            ;;
        --raw-data-buffer-size)
            RAW_DATA_BUFFER_SIZE=$2
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --input-config-file <FILE>               Input JSON config file"
            echo "  --output-config-file <FILE>              Output JSON config file"
            echo "  --connection-type <TYPE>                 Connectivity connection type, default: ${CONNECTION_TYPE}"
            echo "  --keep-alive-interval-seconds <INT>      (\"iotCore\" connection type only) Keep alive interval for MQTT client, default: ${KEEP_ALIVE_INTERVAL_SECONDS}"
            echo "  --ping-timeout-ms <INT>                  (\"iotCore\" connection type only) Ping timeout for MQTT client, default: ${PING_TIMEOUT_MS}"
            echo "  --session-expiry-interval-seconds <INT>  (\"iotCore\" connection type only) Expiry interval for persistent sessions, 0 means disabled, default: ${SESSION_EXPIRY_INTERVAL_SECONDS}"
            echo "  --vehicle-name <NAME>                    Vehicle name"
            echo "  --endpoint-url <URL>                     IoT Core MQTT endpoint URL"
            echo "  --can-bus0 <BUS>                         CAN bus 0, e.g. vcan0"
            echo "  --certificate <CERTIFICATE>              Certificate"
            echo "  --certificate-file <FILE>                Certificate file, default: ${CERTIFICATE_FILE}"
            echo "  --private-key <KEY>                      Private key"
            echo "  --private-key-file <FILE>                Private key file, default: ${PRIVATE_KEY_FILE}"
            echo "  --persistency-path <PATH>                Persistency path, default: ${PERSISTENCY_PATH}"
            echo "  --topic-prefix <PREFIX>                  IoT MQTT topic prefix, default: ${TOPIC_PREFIX}"
            echo "  --log-level <LEVEL>                      Log level. Either: Off, Error, Warning, Info, Trace. Default: ${LOG_LEVEL}"
            echo "  --log-color <COLOR_OPTION>               Whether logs should be colored. Either: Auto, Yes, No. Default: ${LOG_COLOR}"
            echo "  --creds-endpoint-url <URL>               Endpoint URL for AWS IoT Credentials Provider"
            echo "  --creds-role-alias <ALIAS>               Role alias for AWS IoT Credentials Provider"
            echo "  --raw-data-buffer-size <SIZE>            Raw data buffer size, default: ${RAW_DATA_BUFFER_SIZE}"
            exit 0
            ;;
        esac
        shift
    done

    if [ "${INPUT_CONFIG_FILE}" == "" ]; then
        echo "Error: No input config file specified"
        exit -1
    fi
    if [ "${OUTPUT_CONFIG_FILE}" == "" ]; then
        echo "Error: No output config file specified"
        exit -1
    fi
    if [ "${VEHICLE_NAME}" == "" ]; then
        echo "Error: No Vehicle name specified"
        exit -1
    fi
    if [ "${CONNECTION_TYPE}" == "iotCore" ]; then
        if [ "${ENDPOINT_URL}" == "" ]; then
            echo "Error: No endpoint URL specified"
            exit -1
        fi
    fi
}

parse_args "$@"

if [ "`jq '.networkInterfaces[0].canInterface' ${INPUT_CONFIG_FILE}`" == "null" ] \
    || [ "`jq '.networkInterfaces[1].obdInterface' ${INPUT_CONFIG_FILE}`" == "null" ]; then
    echo "Error: Unexpected format in input config file"
    exit -1
fi

# Create the config file:
OUTPUT_CONFIG=` \
    jq ".staticConfig.mqttConnection.clientId=\"${VEHICLE_NAME}\"" ${INPUT_CONFIG_FILE} \
    | jq ".staticConfig.mqttConnection.collectionSchemeListTopic=\"${TOPIC_PREFIX}vehicles/${VEHICLE_NAME}/collection_schemes\"" \
    | jq ".staticConfig.mqttConnection.decoderManifestTopic=\"${TOPIC_PREFIX}vehicles/${VEHICLE_NAME}/decoder_manifests\"" \
    | jq ".staticConfig.mqttConnection.canDataTopic=\"${TOPIC_PREFIX}vehicles/${VEHICLE_NAME}/signals\"" \
    | jq ".staticConfig.mqttConnection.checkinTopic=\"${TOPIC_PREFIX}vehicles/${VEHICLE_NAME}/checkins\"" \
    | jq ".staticConfig.internalParameters.systemWideLogLevel=\"${LOG_LEVEL}\"" \
    | jq ".staticConfig.internalParameters.logColor=\"${LOG_COLOR}\"" \
    | jq ".staticConfig.persistency.persistencyPath=\"${PERSISTENCY_PATH}\"" \
    | jq ".staticConfig.publishToCloudParameters.collectionSchemeManagementCheckinIntervalMs=5000" \
    | jq ".staticConfig.mqttConnection.connectionType=\"${CONNECTION_TYPE}\""`

if [ "$CONNECTION_TYPE" == "iotCore" ]; then
    OUTPUT_CONFIG=`echo "${OUTPUT_CONFIG}" \
        | jq ".staticConfig.mqttConnection.endpointUrl=\"${ENDPOINT_URL}\"" \
        | if [[ $CERTIFICATE ]]; \
            then jq "del(.staticConfig.mqttConnection.certificateFilename)" | jq ".staticConfig.mqttConnection.certificate=\"${CERTIFICATE}\""; \
            else jq ".staticConfig.mqttConnection.certificateFilename=\"${CERTIFICATE_FILE}\""; \
            fi \
        | if [[ $PRIVATE_KEY ]]; \
            then jq "del(.staticConfig.mqttConnection.privateKeyFilename)" | jq ".staticConfig.mqttConnection.privateKey=\"${PRIVATE_KEY}\""; \
            else jq ".staticConfig.mqttConnection.privateKeyFilename=\"${PRIVATE_KEY_FILE}\""; \
            fi \
        | jq ".staticConfig.mqttConnection.keepAliveIntervalSeconds=${KEEP_ALIVE_INTERVAL_SECONDS}" \
        | jq ".staticConfig.mqttConnection.pingTimeoutMs=${PING_TIMEOUT_MS}" \
        | jq ".staticConfig.mqttConnection.sessionExpiryIntervalSeconds=${SESSION_EXPIRY_INTERVAL_SECONDS}"`
elif [ "$CONNECTION_TYPE" == "iotGreengrassV2" ]; then
    OUTPUT_CONFIG=`echo "${OUTPUT_CONFIG}" \
        | jq "del(.staticConfig.mqttConnection.endpointUrl)" \
        | jq "del(.staticConfig.mqttConnection.certificateFilename)" \
        | jq "del(.staticConfig.mqttConnection.privateKeyFilename)" \
        | jq "del(.staticConfig.mqttConnection.keepAliveIntervalSeconds)" \
        | jq "del(.staticConfig.mqttConnection.pingTimeoutMs)" \
        | jq "del(.staticConfig.mqttConnection.sessionExpiryIntervalSeconds)"`
else
    echo "Error: Unknown connection type ${CONNECTION_TYPE}"
fi

# Clear all existing interfaces in the template file:
OUTPUT_CONFIG=`echo "${OUTPUT_CONFIG}" | jq ".networkInterfaces=[]"`

if [ "${CAN_BUS0}" != "" ]; then
    CAN_INTERFACE=`echo "{}" | jq ".interfaceId=\"1\"" \
        | jq ".type=\"canInterface\"" \
        | jq ".canInterface.interfaceName=\"${CAN_BUS0}\"" \
        | jq ".canInterface.protocolName=\"CAN\"" \
        | jq ".canInterface.protocolVersion=\"2.0A\""`
    OUTPUT_CONFIG=`echo "${OUTPUT_CONFIG}" | jq ".networkInterfaces+=[${CAN_INTERFACE}]"`

    OBD_INTERFACE=`echo "{}" | jq ".interfaceId=\"0\"" \
        | jq ".type=\"obdInterface\"" \
        | jq ".obdInterface.interfaceName=\"${CAN_BUS0}\"" \
        | jq ".obdInterface.pidRequestIntervalSeconds=5" \
        | jq ".obdInterface.dtcRequestIntervalSeconds=5" \
        | jq ".obdInterface.broadcastRequests=true" \
        | jq ".obdInterface.obdStandard=\"J1979\""`
    OUTPUT_CONFIG=`echo "${OUTPUT_CONFIG}" | jq ".networkInterfaces+=[${OBD_INTERFACE}]"`
fi

if [ "${CREDS_ENDPOINT_URL}" != "" ] || [ "${CREDS_ROLE_ALIAS}" != "" ]; then
    ROS2_INTERFACE=`echo "{}" | jq ".interfaceId=\"10\"" \
        | jq ".type=\"ros2Interface\"" \
        | jq ".ros2Interface.subscribeQueueLength=100" \
        | jq ".ros2Interface.executorThreads=2" \
        | jq ".ros2Interface.introspectionLibraryCompare=\"ErrorAndFail\""`
    OUTPUT_CONFIG=`echo "${OUTPUT_CONFIG}" | jq ".networkInterfaces+=[${ROS2_INTERFACE}]" \
        | jq ".staticConfig.credentialsProvider.endpointUrl=\"${CREDS_ENDPOINT_URL}\"" \
        | jq ".staticConfig.credentialsProvider.roleAlias=\"${CREDS_ROLE_ALIAS}\"" \
        | jq ".staticConfig.visionSystemDataCollection.rawDataBuffer.maxSize=${RAW_DATA_BUFFER_SIZE}"`
fi

echo "${OUTPUT_CONFIG}" > ${OUTPUT_CONFIG_FILE}
