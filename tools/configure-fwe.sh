#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

if [ -z "${INPUT_CONFIG_FILE+x}" ]; then
    INPUT_CONFIG_FILE=""
fi
if [ -z "${OUTPUT_CONFIG_FILE+x}" ]; then
    OUTPUT_CONFIG_FILE=""
fi
if [ -z "${CERTIFICATE+x}" ]; then
    CERTIFICATE=""
fi
if [ -z "${CERTIFICATE_FILE+x}" ]; then
    CERTIFICATE_FILE="/etc/aws-iot-fleetwise/certificate.pem"
fi
if [ -z "${PRIVATE_KEY+x}" ]; then
    PRIVATE_KEY=""
fi
if [ -z "${PRIVATE_KEY_FILE+x}" ]; then
    PRIVATE_KEY_FILE="/etc/aws-iot-fleetwise/private-key.key"
fi
if [ -z "${VEHICLE_NAME+x}" ]; then
    VEHICLE_NAME=""
fi
if [ -z "${ENDPOINT_URL+x}" ]; then
    ENDPOINT_URL=""
fi
if [ -z "${CAN_BUS0+x}" ]; then
    CAN_BUS0=""
fi
if [ -z "${LOG_LEVEL+x}" ]; then
    LOG_LEVEL="Info"
fi
if [ -z "${LOG_COLOR+x}" ]; then
    LOG_COLOR="Auto"
fi
if [ -z "${PERSISTENCY_PATH+x}" ]; then
    PERSISTENCY_PATH="/var/aws-iot-fleetwise/"
fi
if [ -z "${TOPIC_PREFIX+x}" ]; then
    TOPIC_PREFIX="\$aws/iotfleetwise/"
fi
if [ -z "${CONNECTION_TYPE+x}" ]; then
    CONNECTION_TYPE="iotCore"
fi

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
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --input-config-file <FILE>    Input JSON config file"
            echo "  --output-config-file <FILE>   Output JSON config file"
            echo "  --connection-type <TYPE>      Connectivity connection type, default: ${CONNECTION_TYPE}"
            echo "  --vehicle-name <NAME>         Vehicle name"
            echo "  --endpoint-url <URL>          IoT Core MQTT endpoint URL"
            echo "  --can-bus0 <BUS>              CAN bus 0, e.g. vcan0"
            echo "  --certificate <CERTIFICATE>   Certificate"
            echo "  --certificate-file <FILE>     Certificate file, default: ${CERTIFICATE_FILE}"
            echo "  --private-key <KEY>           Private key"
            echo "  --private-key-file <FILE>     Private key file, default: ${PRIVATE_KEY_FILE}"
            echo "  --persistency-path <PATH>     Persistency path, default: ${PERSISTENCY_PATH}"
            echo "  --topic-prefix <PREFIX>       IoT MQTT topic prefix, default: ${TOPIC_PREFIX}"
            echo "  --log-level <LEVEL>           Log level. Either: Off, Error, Warning, Info, Trace. Default: ${LOG_LEVEL}"
            echo "  --log-color <COLOR_OPTION>    Whether logs should be colored. Either: Auto, Yes, No. Default: ${LOG_COLOR}"
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
            fi`
elif [ "$CONNECTION_TYPE" == "iotGreengrassV2" ]; then
    OUTPUT_CONFIG=`echo "${OUTPUT_CONFIG}" \
        | jq "del(.staticConfig.mqttConnection.endpointUrl)" \
        | jq "del(.staticConfig.mqttConnection.certificateFilename)" \
        | jq "del(.staticConfig.mqttConnection.privateKeyFilename)"`
else
    echo "Error: Unknown connection type ${CONNECTION_TYPE}"
fi

if [ "${CAN_BUS0}" != "" ]; then
    OUTPUT_CONFIG=`echo "${OUTPUT_CONFIG}" | jq ".networkInterfaces[0].canInterface.interfaceName=\"${CAN_BUS0}\"" \
        | jq ".networkInterfaces[1].obdInterface.interfaceName=\"${CAN_BUS0}\"" \
        | jq ".networkInterfaces[1].obdInterface.pidRequestIntervalSeconds=5" \
        | jq ".networkInterfaces[1].obdInterface.dtcRequestIntervalSeconds=5" \
        | jq ".networkInterfaces[1].interfaceId=\"0\""`
else
    OUTPUT_CONFIG=`echo "${OUTPUT_CONFIG}" | jq ".networkInterfaces=[]"`
fi

echo "${OUTPUT_CONFIG}" > ${OUTPUT_CONFIG_FILE}
