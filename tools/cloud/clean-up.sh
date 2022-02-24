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

ENDPOINT_URL=""
ENDPOINT_URL_OPTION=""
REGION="us-east-1"
VEHICLE_ID=""
TIMESTAMP=""
FLEET_SIZE=1
BATCH_SIZE=$((`nproc`*4))

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --vehicle-id)
            VEHICLE_ID=$2
            ;;
        --fleet-size)
            FLEET_SIZE=$2
            ;;
        --timestamp)
            TIMESTAMP=$2
            ;;
        --endpoint-url)
            ENDPOINT_URL=$2
            ;;
        --region)
            REGION=$2
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --vehicle-id <ID>     Vehicle ID"
            echo "  --fleet-size <SIZE>   Size of fleet, default: ${FLEET_SIZE}. When greater than 1,"
            echo "                        the instance number will be appended to each"
            echo "                        vehicle ID after a '-', e.g. fwdemo-42"
            echo "  --timestamp <TS>      Timestamp of demo.sh script"
            echo "  --endpoint-url <URL>  The endpoint URL used for AWS CLI calls"
            echo "  --region <REGION>     The region used for AWS CLI calls, default: ${REGION}"
            exit 0
            ;;
        esac
        shift
    done

    if [ "${ENDPOINT_URL}" != "" ]; then
        ENDPOINT_URL_OPTION="--endpoint-url ${ENDPOINT_URL}"
    fi
    if [ "${VEHICLE_ID}" == "" ]; then
        echo "Error: Vehicle ID not provided"
        exit -1
    fi
    if [ "${TIMESTAMP}" == "" ]; then
        echo "Error: Timestamp not provided"
        exit -1
    fi
}

echo "======================================="
echo "AWS IoT FleetWise Cloud Clean-up Script"
echo "======================================="

parse_args "$@"

NAME="${VEHICLE_ID}-${TIMESTAMP}"

echo -n "Date: "
date --rfc-3339=seconds

echo "Timestamp: ${TIMESTAMP}"
echo "Vehicle ID: ${VEHICLE_ID}"
echo "Fleet size: ${FLEET_SIZE}"

echo "Suspending campaign..."
aws iotfleetwise update-campaign \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --campaign-name ${NAME}-campaign \
    --action SUSPEND 2> /dev/null | jq -r .arn || true

echo "Deleting campaign..."
aws iotfleetwise delete-campaign \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --campaign-name ${NAME}-campaign 2> /dev/null | jq -r .arn || true

if ((FLEET_SIZE==1)); then
    echo "Disassociating vehicle ${VEHICLE_ID}..."
    aws iotfleetwise disassociate-vehicle \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --fleet-id ${NAME}-fleet \
        --vehicle-id "${VEHICLE_ID}" 2> /dev/null || true
else
    echo "Disassociating vehicle ${VEHICLE_ID}-0..$((FLEET_SIZE-1))..."
    for ((i=0; i<${FLEET_SIZE}; i+=${BATCH_SIZE})); do
        for ((j=0; j<${BATCH_SIZE} && i+j<${FLEET_SIZE}; j++)); do
            # This output group is run in a background process. Note that stderr is redirected to stream 3 and back,
            # to print stderr from the output group, but not info about the background process.
            { \
                aws iotfleetwise disassociate-vehicle \
                    ${ENDPOINT_URL_OPTION} --region ${REGION} \
                    --fleet-id ${NAME}-fleet \
                    --vehicle-id "${VEHICLE_ID}-$((i+j))" 2> /dev/null || true \
            2>&3 &} 3>&2 2>/dev/null
        done
        # Wait for all background processes to finish
        wait
    done
fi

if ((FLEET_SIZE==1)); then
    echo "Deleting vehicle ${VEHICLE_ID}..."
    aws iotfleetwise delete-vehicle \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --vehicle-id "${VEHICLE_ID}" 2> /dev/null || true
else
    echo "Deleting vehicle ${VEHICLE_ID}-0..$((FLEET_SIZE-1))..."
    for ((i=0; i<${FLEET_SIZE}; i+=${BATCH_SIZE})); do
        for ((j=0; j<${BATCH_SIZE} && i+j<${FLEET_SIZE}; j++)); do
            # This output group is run in a background process. Note that stderr is redirected to stream 3 and back,
            # to print stderr from the output group, but not info about the background process.
            { \
                aws iotfleetwise delete-vehicle \
                    ${ENDPOINT_URL_OPTION} --region ${REGION} \
                    --vehicle-id "${VEHICLE_ID}-$((i+j))" 2> /dev/null || true \
            2>&3 &} 3>&2 2>/dev/null
        done
        # Wait for all background processes to finish
        wait
    done
fi

echo "Deleting fleet..."
aws iotfleetwise delete-fleet \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --fleet-id ${NAME}-fleet 2> /dev/null || true

echo "Deleting decoder manifest..."
aws iotfleetwise delete-decoder-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-decoder-manifest 2> /dev/null || true

echo "Deleting model manifest..."
aws iotfleetwise delete-model-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-model-manifest 2> /dev/null || true

# Note: As the service currently only supports one signal catalog, do not delete it
# echo "Deleting signal catalog..."
# aws iotfleetwise delete-signal-catalog \
#     ${ENDPOINT_URL_OPTION} --region ${REGION} \
#     --name ${NAME}-signal-catalog 2> /dev/null || true
