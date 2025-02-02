#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
ENDPOINT_URL=""
ENDPOINT_URL_OPTION=""
REGION="us-east-1"
VEHICLE_NAME=""
DISAMBIGUATOR=""
SIGNAL_CATALOG=""
ACCOUNT_ID=`aws sts get-caller-identity --query "Account" --output text`
SERVICE_ROLE="IoTFleetWiseServiceRole"
SERVICE_ROLE_POLICY_ARN="arn:aws:iam::${ACCOUNT_ID}:policy/"
FLEET_SIZE=1
BATCH_SIZE=$((`nproc`*4))
CAMPAIGN_NAMES=""

if [ -f demo.env ]; then
    source demo.env
fi

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --vehicle-name)
            VEHICLE_NAME=$2
            shift
            ;;
        --fleet-size)
            FLEET_SIZE=$2
            shift
            ;;
        --disambiguator)
            DISAMBIGUATOR=$2
            shift
            ;;
        --signal-catalog)
            SIGNAL_CATALOG=$2
            shift
            ;;
        --endpoint-url)
            ENDPOINT_URL=$2
            shift
            ;;
        --region)
            REGION=$2
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --vehicle-name <NAME>      Vehicle name"
            echo "  --fleet-size <SIZE>        Size of fleet, default: ${FLEET_SIZE}. When greater than 1,"
            echo "                             the instance number will be appended to each"
            echo "                             Vehicle name after a '-', e.g. fwdemo-42"
            echo "  --disambiguator <STRING>   The unique string used by the demo.sh script to avoid resource name conflicts"
            echo "  --signal-catalog <NAME>    Optional: name of signal catalog to delete"
            echo "  --endpoint-url <URL>       The endpoint URL used for AWS CLI calls"
            echo "  --region <REGION>          The region used for AWS CLI calls, default: ${REGION}"
            exit 0
            ;;
        esac
        shift
    done

    if [ "${ENDPOINT_URL}" != "" ]; then
        ENDPOINT_URL_OPTION="--endpoint-url ${ENDPOINT_URL}"
    fi
    if [ "${VEHICLE_NAME}" == "" ]; then
        echo "Error: Vehicle name not provided"
        exit 1
    fi
    if [ "${DISAMBIGUATOR}" != "" ]; then
        DISAMBIGUATOR="-${DISAMBIGUATOR}"
    fi
}

echo "======================================="
echo "AWS IoT FleetWise Cloud Clean-up Script"
echo "======================================="

parse_args "$@"

if [ ${FLEET_SIZE} -gt 1 ]; then
    VEHICLES=( $(seq -f "${VEHICLE_NAME}-%g" 0 $((${FLEET_SIZE}-1))) )
else
    VEHICLES=( ${VEHICLE_NAME} )
fi

NAME="${VEHICLE_NAME}${DISAMBIGUATOR}"
SERVICE_ROLE="${SERVICE_ROLE}-${REGION}${DISAMBIGUATOR}"
SERVICE_ROLE_POLICY_ARN="${SERVICE_ROLE_POLICY_ARN}${SERVICE_ROLE}-policy"

echo -n "Date: "
date --rfc-3339=seconds

echo "Disambiguator: ${DISAMBIGUATOR}"
echo "Vehicle name: ${VEHICLE_NAME}"
echo "Fleet size: ${FLEET_SIZE}"
echo "Vehicles: ${VEHICLES[@]}"

FAILED_CLEAN_UP_STEPS=""

# $1 is the campaign name
suspend_and_delete_campaign(){
    echo "Suspending campaign $1..."
    aws iotfleetwise update-campaign \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name $1 \
        --action SUSPEND | jq -r .arn \
        || FAILED_CLEAN_UP_STEPS="${FAILED_CLEAN_UP_STEPS} suspend-campaign"

    echo "Deleting campaign $1..."
    aws iotfleetwise delete-campaign \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name $1 | jq -r .arn \
        || FAILED_CLEAN_UP_STEPS="${FAILED_CLEAN_UP_STEPS} delete-campaign"
}

for CAMPAIGN_NAME in $(echo $CAMPAIGN_NAMES | tr "," " "); do
    suspend_and_delete_campaign $CAMPAIGN_NAME
done

for ((i=0; i<${FLEET_SIZE}; i+=${BATCH_SIZE})); do
    PIDS=()
    for ((j=0; j<${BATCH_SIZE} && i+j<${FLEET_SIZE}; j++)); do
        vehicle=${VEHICLES[$((i+j))]}
        echo "Disassociating vehicle ${vehicle}..."
        # This output group is run in a background process. Note that stderr is redirected to stream 3 and back,
        # to print stderr from the output group, but not info about the background process.
        { \
            aws iotfleetwise disassociate-vehicle-fleet \
                ${ENDPOINT_URL_OPTION} --region ${REGION} \
                --fleet-id ${NAME}-fleet \
                --vehicle-name "${vehicle}" \
        2>&3 &} 3>&2
        PIDS+=($!)
    done
    # Wait for all background processes to finish
    for PID in ${PIDS[@]}; do
        wait $PID || FAILED_CLEAN_UP_STEPS="${FAILED_CLEAN_UP_STEPS} disassociate-vehicle-fleet"
    done
done

for ((i=0; i<${FLEET_SIZE}; i+=${BATCH_SIZE})); do
    PIDS=()
    for ((j=0; j<${BATCH_SIZE} && i+j<${FLEET_SIZE}; j++)); do
        vehicle=${VEHICLES[$((i+j))]}
        echo "Deleting vehicle ${vehicle}..."
        # This output group is run in a background process. Note that stderr is redirected to stream 3 and back,
        # to print stderr from the output group, but not info about the background process.
        { \
            aws iotfleetwise delete-vehicle \
                ${ENDPOINT_URL_OPTION} --region ${REGION} \
                --vehicle-name "${vehicle}" \
        2>&3 &} 3>&2
        PIDS+=($!)
    done
    # Wait for all background processes to finish
    for PID in ${PIDS[@]}; do
        wait $PID || FAILED_CLEAN_UP_STEPS="${FAILED_CLEAN_UP_STEPS} delete-vehicle"
    done
done

echo "Deleting fleet..."
aws iotfleetwise delete-fleet \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --fleet-id ${NAME}-fleet \
    || FAILED_CLEAN_UP_STEPS="${FAILED_CLEAN_UP_STEPS} delete-fleet"

echo "Deleting decoder manifest..."
aws iotfleetwise delete-decoder-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-decoder-manifest \
    || FAILED_CLEAN_UP_STEPS="${FAILED_CLEAN_UP_STEPS} delete-decoder-manifest"

echo "Deleting model manifest..."
aws iotfleetwise delete-model-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-model-manifest \
    || FAILED_CLEAN_UP_STEPS="${FAILED_CLEAN_UP_STEPS} delete-model-manifest"

if [ "${SIGNAL_CATALOG}" != "" ]; then
    echo "Deleting signal catalog..."
    aws iotfleetwise delete-signal-catalog \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${SIGNAL_CATALOG} \
        || FAILED_CLEAN_UP_STEPS="${FAILED_CLEAN_UP_STEPS} delete-signal-catalog"
fi

${SCRIPT_DIR}/manage-service-role.sh \
    --service-role ${SERVICE_ROLE} \
    --clean-up \
    || FAILED_CLEAN_UP_STEPS="${FAILED_CLEAN_UP_STEPS} delete-service-role"

if [ "${FAILED_CLEAN_UP_STEPS}" != "" ]; then
    echo "Failed to clean up the following resources: ${FAILED_CLEAN_UP_STEPS}"
    exit 1
fi
