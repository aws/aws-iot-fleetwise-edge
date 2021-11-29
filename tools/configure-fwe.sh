#!/bin/bash
# Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
# SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
# Licensed under the Amazon Software License (the "License").
# You may not use this file except in compliance with the License.
# A copy of the License is located at
# http://aws.amazon.com/asl/
# or in the "license" file accompanying this file. This file is distributed
# on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
# express or implied. See the License for the specific language governing
# permissions and limitations under the License.

set -euo pipefail

INPUT_CONFIG_FILE=""
OUTPUT_CONFIG_FILE=""
VEHICLE_ID=""
ENDPOINT_URL=""
CAN_BUS0="vcan0"
USE_EXTENDED_IDS=false
HAS_TRANSMISSION_ECU=false
PERSISTENCY_PATH="/var/aws-iot-fleetwise/"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --input-config-file)
            INPUT_CONFIG_FILE=$2
            ;;
        --output-config-file)
            OUTPUT_CONFIG_FILE=$2
            ;;
        --vehicle-id)
            VEHICLE_ID=$2
            ;;
        --endpoint-url)
            ENDPOINT_URL=$2
            ;;
        --use-extended-ids)
            USE_EXTENDED_IDS=true
            ;;
        --has-transmission-ecu)
            HAS_TRANSMISSION_ECU=true
            ;;
        --can-bus0)
            CAN_BUS0=$2
            ;;
        --persistency-path)
            PERSISTENCY_PATH=$2
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --input-config-file <CONFIG_FILE>   Input JSON config file"
            echo "  --output-config-file <CONFIG_FILE>  Output JSON config file"
            echo "  --vehicle-id <ID>                   Vehicle ID"
            echo "  --endpoint-url <URL>                IoT Core MQTT endpoint URL"
            echo "  --can-bus0                          CAN bus 0, default vcan0"
            echo "  --use-extended-ids                  Use extended CAN IDs for diagnostic communication"
            echo "  --has-transmission-ecu              Vehicle has automatic transmission"
            echo "  --persistency-path <PATH>           Desired persistency path, default $PERSISTENCY_PATH"
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
    if [ "${VEHICLE_ID}" == "" ]; then
        echo "Error: No vehicle ID specified"
        exit -1
    fi
    if [ "${ENDPOINT_URL}" == "" ]; then
        echo "Error: No endpoint URL specified"
        exit -1
    fi
}

parse_args "$@"

if [ "`jq '.networkInterfaces[0].canInterface' ${INPUT_CONFIG_FILE}`" == "null" ] \
    || [ "`jq '.networkInterfaces[1].obdInterface' ${INPUT_CONFIG_FILE}`" == "null" ]; then
    echo "Error: Unexpected format in input config file"
    exit -1
fi

# Create the config file:
jq ".staticConfig.mqttConnection.endpointUrl=\"${ENDPOINT_URL}\"" ${INPUT_CONFIG_FILE} \
    | jq ".staticConfig.mqttConnection.clientId=\"${VEHICLE_ID}\"" \
    | jq ".staticConfig.mqttConnection.collectionSchemeListTopic=\"\$aws/iotfleetwise/vehicles/${VEHICLE_ID}/collection_schemes\"" \
    | jq ".staticConfig.mqttConnection.decoderManifestTopic=\"\$aws/iotfleetwise/vehicles/${VEHICLE_ID}/decoder_manifests\"" \
    | jq ".staticConfig.mqttConnection.canDataTopic=\"\$aws/iotfleetwise/vehicles/${VEHICLE_ID}/signals\"" \
    | jq ".staticConfig.mqttConnection.checkinTopic=\"\$aws/iotfleetwise/vehicles/${VEHICLE_ID}/checkins\"" \
    | jq ".staticConfig.mqttConnection.certificateFilename=\"/etc/aws-iot-fleetwise/certificate.pem\"" \
    | jq ".staticConfig.mqttConnection.privateKeyFilename=\"/etc/aws-iot-fleetwise/private-key.key\"" \
    | jq ".staticConfig.internalParameters.systemWideLogLevel=\"Info\"" \
    | jq ".staticConfig.persistency.persistencyPath=\"${PERSISTENCY_PATH}\"" \
    | jq ".networkInterfaces[0].canInterface.interfaceName=\"${CAN_BUS0}\"" \
    | jq ".networkInterfaces[1].obdInterface.interfaceName=\"${CAN_BUS0}\"" \
    | jq ".networkInterfaces[1].obdInterface.useExtendedIds=${USE_EXTENDED_IDS}" \
    | jq ".networkInterfaces[1].obdInterface.hasTransmissionEcu=${HAS_TRANSMISSION_ECU}" \
    | jq ".networkInterfaces[1].obdInterface.pidRequestIntervalSeconds=5" \
    | jq ".networkInterfaces[1].obdInterface.dtcRequestIntervalSeconds=5" \
    | jq ".networkInterfaces[1].interfaceId=\"0\"" \
    > ${OUTPUT_CONFIG_FILE}
