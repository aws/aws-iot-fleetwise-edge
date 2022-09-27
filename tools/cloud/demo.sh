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
TIMESTAMP=`date +%s`
DEFAULT_VEHICLE_NAME="fwdemo"
VEHICLE_NAME=""
TIMESTREAM_DB_NAME="IoTFleetWiseDB-${TIMESTAMP}"
TIMESTREAM_TABLE_NAME="VehicleDataTable"
CAMPAIGN_FILE="campaign-brake-event.json"
CLEAN_UP=false
FLEET_SIZE=1
BATCH_SIZE=$((`nproc`*4))
HEALTH_CHECK_RETRIES=360 # About 30mins
MAX_ATTEMPTS_ON_REGISTRATION_FAILURE=5
FORCE_REGISTRATION=false

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --vehicle-name)
            VEHICLE_NAME=$2
            ;;
        --fleet-size)
            FLEET_SIZE=$2
            ;;
        --clean-up)
            CLEAN_UP=true
            ;;
        --campaign-file)
            CAMPAIGN_FILE=$2
            ;;
        --endpoint-url)
            ENDPOINT_URL=$2
            ;;
        --region)
            REGION=$2
            ;;
        --force-registration)
            FORCE_REGISTRATION=true
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --vehicle-name <ID>     Vehicle name"
            echo "  --fleet-size <SIZE>     Size of fleet, default: ${FLEET_SIZE}. When greater than 1,"
            echo "                          the instance number will be appended to each"
            echo "                          Vehicle name after a '-', e.g. ${DEFAULT_VEHICLE_NAME}-42"
            echo "  --campaign-file <FILE>  Campaign JSON file, default: ${CAMPAIGN_FILE}"
            echo "  --clean-up              Delete created resources"
            echo "  --endpoint-url <URL>    The endpoint URL used for AWS CLI calls"
            echo "  --region <REGION>       The region used for AWS CLI calls, default: ${REGION}"
            echo "  --force-registration    Force account registration"
            exit 0
            ;;
        esac
        shift
    done

    if [ "${ENDPOINT_URL}" != "" ]; then
        ENDPOINT_URL_OPTION="--endpoint-url ${ENDPOINT_URL}"
    fi
    if ((FLEET_SIZE==0)); then
        echo "Error: Fleet size must be greater than zero" >&2
        exit -1
    fi
}

echo "==================================="
echo "AWS IoT FleetWise Cloud Demo Script"
echo "==================================="

parse_args "$@"

if [ "${VEHICLE_NAME}" == "" ]; then
    echo -n "Enter Vehicle name [${DEFAULT_VEHICLE_NAME}]: "
    read VEHICLE_NAME
    if [ "${VEHICLE_NAME}" == "" ]; then
        VEHICLE_NAME=${DEFAULT_VEHICLE_NAME}
    fi
fi

NAME="${VEHICLE_NAME}-${TIMESTAMP}"

echo -n "Date: "
date --rfc-3339=seconds
echo "Timestamp: ${TIMESTAMP}"
echo "Vehicle name: ${VEHICLE_NAME}"
echo "Fleet Size: ${FLEET_SIZE}"

# AWS CLI v1.x has a double base64 encoding issue
echo "Checking AWS CLI version..."
CLI_VERSION=`aws --version`
echo ${CLI_VERSION}
if echo "${CLI_VERSION}" | grep -q "aws-cli/1."; then
    echo "Error: Please update AWS CLI to v2.x" >&2
    exit -1
fi

error_handler() {
    if [ ${CLEAN_UP} == true ]; then
        ./clean-up.sh \
            --vehicle-name ${VEHICLE_NAME} \
            --fleet-size ${FLEET_SIZE} \
            --timestamp ${TIMESTAMP} \
            ${ENDPOINT_URL_OPTION} \
            --region ${REGION}
    fi
}

register_account() {
    echo "Registering account..."
    aws iotfleetwise register-account \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --timestream-resources "{\"timestreamDatabaseName\":\"${TIMESTREAM_DB_NAME}\", \
            \"timestreamTableName\":\"${TIMESTREAM_TABLE_NAME}\"}" | jq -r .registerAccountStatus
    echo "Waiting for account to be registered..."
}

get_account_status() {
    aws iotfleetwise get-register-account-status ${ENDPOINT_URL_OPTION} --region ${REGION}
}

trap error_handler ERR

echo "Getting AWS account ID..."
ACCOUNT_ID=`aws sts get-caller-identity | jq -r .Account`
echo ${ACCOUNT_ID}

echo "Getting account registration status..."
if REGISTER_ACCOUNT_STATUS=`get_account_status 2>&1`; then
    ACCOUNT_STATUS=`echo "${REGISTER_ACCOUNT_STATUS}" | jq -r .accountStatus`
elif ! echo ${REGISTER_ACCOUNT_STATUS} | grep -q "ResourceNotFoundException"; then
    echo ${REGISTER_ACCOUNT_STATUS} >&2
    exit -1
else
    ACCOUNT_STATUS="NOT_REGISTERED"
fi
echo ${ACCOUNT_STATUS}
if ${FORCE_REGISTRATION}; then
    echo "Forcing registration..."
    ACCOUNT_STATUS="FORCE_REGISTRATION"
fi
if [ "${ACCOUNT_STATUS}" == "REGISTRATION_SUCCESS" ]; then
    echo "Account is already registered"
    TIMESTREAM_DB_NAME=`echo "${REGISTER_ACCOUNT_STATUS}" | jq -r .timestreamRegistrationResponse.timestreamDatabaseName`
    SERVICE_ROLE=`echo "${REGISTER_ACCOUNT_STATUS}" | jq -r .iamRegistrationResponse.roleArn | cut -d / -f 2`

    echo "Checking if Timestream database exists..."
    if TIMESTREAM_INFO=`aws timestream-write describe-database \
        --region ${REGION} --database-name ${TIMESTREAM_DB_NAME} 2>&1`; then
        echo ${TIMESTREAM_INFO} | jq -r .Database.Arn
    elif ! echo ${TIMESTREAM_INFO} | grep -q "ResourceNotFoundException"; then
        echo ${TIMESTREAM_INFO} >&2
        exit -1
    else
        echo "Error: Timestream database no longer exists. Try running script again with option --force-registration" >&2
        exit -1
    fi

    echo "Checking if service role exists..."
    if SERVICE_ROLE_INFO=`aws iam get-role --role-name ${SERVICE_ROLE} 2>&1`; then
        SERVICE_ROLE_ARN=`echo ${SERVICE_ROLE_INFO} | jq -r .Role.Arn`
        echo ${SERVICE_ROLE_ARN}
    elif ! echo ${SERVICE_ROLE_INFO} | grep -q "NoSuchEntity"; then
        echo ${SERVICE_ROLE_INFO}
        exit -1
    else
        echo "Error: Service role no longer exists. Try running script again with option --force-registration" >&2
        exit -1
    fi
elif [ "${ACCOUNT_STATUS}" == "REGISTRATION_PENDING" ]; then
    echo "Waiting for account to be registered..."
else
    echo "Creating Timestream database..."
    aws timestream-write create-database \
        --region ${REGION} \
        --database-name ${TIMESTREAM_DB_NAME} | jq -r .Database.Arn

    echo "Creating Timestream table..."
    aws timestream-write create-table \
        --region ${REGION} \
        --database-name ${TIMESTREAM_DB_NAME} \
        --table-name ${TIMESTREAM_TABLE_NAME} \
        --retention-properties "{\"MemoryStoreRetentionPeriodInHours\":2, \
            \"MagneticStoreRetentionPeriodInDays\":2}" | jq -r .Table.Arn

    register_account
fi

# Due to IAM's eventual consistency, the registration may initially fail because the
# recently created role and the attached policies take some time to propagate.
REGISTRATION_ATTEMPTS=0
while [ "${ACCOUNT_STATUS}" != "REGISTRATION_SUCCESS" ]; do
    sleep 5
    REGISTER_ACCOUNT_STATUS=`get_account_status`
    ACCOUNT_STATUS=`echo "${REGISTER_ACCOUNT_STATUS}" | jq -r .accountStatus`
    if [ "${ACCOUNT_STATUS}" == "REGISTRATION_FAILURE" ]; then
        echo "Error: Registration failed" >&2
        ((REGISTRATION_ATTEMPTS+=1))
        if ((REGISTRATION_ATTEMPTS >= MAX_ATTEMPTS_ON_REGISTRATION_FAILURE)); then
            echo "${REGISTER_ACCOUNT_STATUS}" >&2
            echo "All ${MAX_ATTEMPTS_ON_REGISTRATION_FAILURE} registration attempts failed" >&2
            exit -1
        else
            register_account
        fi
    fi
done
TIMESTREAM_DB_NAME=`echo "${REGISTER_ACCOUNT_STATUS}" | jq -r .timestreamRegistrationResponse.timestreamDatabaseName`
TIMESTREAM_TABLE_NAME=`echo "${REGISTER_ACCOUNT_STATUS}" | jq -r .timestreamRegistrationResponse.timestreamTableName` 

if ((FLEET_SIZE==1)); then
    echo "Deleting vehicle ${VEHICLE_NAME} if it already exists..."
    aws iotfleetwise delete-vehicle \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --vehicle-name "${VEHICLE_NAME}"
else
    echo "Deleting vehicle ${VEHICLE_NAME}-0..$((FLEET_SIZE-1)) if it already exists..."
    for ((i=0; i<${FLEET_SIZE}; i+=${BATCH_SIZE})); do
        for ((j=0; j<${BATCH_SIZE} && i+j<${FLEET_SIZE}; j++)); do
            # This output group is run in a background process. Note that stderr is redirected to stream 3 and back,
            # to print stderr from the output group, but not info about the background process.
            { \
                aws iotfleetwise delete-vehicle \
                    ${ENDPOINT_URL_OPTION} --region ${REGION} \
                    --vehicle-name "${VEHICLE_NAME}-$((i+j))" \
            2>&3 &} 3>&2 2>/dev/null
        done
        # Wait for all background processes to finish
        wait
    done
fi

VEHICLE_NODE=`cat vehicle-node.json`
OBD_NODES=`cat obd-nodes.json`
DBC_NODES=`python3.7 dbc-to-nodes.py hscan.dbc`

echo "Checking for existing signal catalog..."
SIGNAL_CATALOG_LIST=`aws iotfleetwise list-signal-catalogs \
    ${ENDPOINT_URL_OPTION} --region ${REGION}`
SIGNAL_CATALOG_COUNT=`echo ${SIGNAL_CATALOG_LIST} | jq '.summaries|length'`
# Currently only one signal catalog is supported by the service
if [ ${SIGNAL_CATALOG_COUNT} == 0 ]; then
    echo "No existing signal catalog"
    echo "Creating signal catalog with Vehicle node..."
    SIGNAL_CATALOG_ARN=`aws iotfleetwise create-signal-catalog \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${NAME}-signal-catalog \
        --nodes "${VEHICLE_NODE}" | jq -r .arn`
    echo ${SIGNAL_CATALOG_ARN}

    echo "Adding OBD signals to signal catalog..."
    aws iotfleetwise update-signal-catalog \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${NAME}-signal-catalog \
        --description "OBD signals" \
        --nodes-to-add "${OBD_NODES}" | jq -r .arn

    echo "Adding DBC signals to signal catalog..."
    aws iotfleetwise update-signal-catalog \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${NAME}-signal-catalog \
        --description "DBC signals" \
        --nodes-to-add "${DBC_NODES}" | jq -r .arn
else
    SIGNAL_CATALOG_NAME=`echo ${SIGNAL_CATALOG_LIST} | jq -r .summaries[0].name`
    SIGNAL_CATALOG_ARN=`echo ${SIGNAL_CATALOG_LIST} | jq -r .summaries[0].arn`
    echo ${SIGNAL_CATALOG_ARN}

    echo "Updating Vehicle node in signal catalog..."
    if UPDATE_SIGNAL_CATALOG_STATUS=`aws iotfleetwise update-signal-catalog \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${SIGNAL_CATALOG_NAME} \
        --description "Vehicle node" \
        --nodes-to-update "${VEHICLE_NODE}" 2>&1`; then
        echo ${UPDATE_SIGNAL_CATALOG_STATUS} | jq -r .arn
    elif ! echo ${UPDATE_SIGNAL_CATALOG_STATUS} | grep -q "InvalidSignalsException"; then
        echo ${UPDATE_SIGNAL_CATALOG_STATUS} >&2
        exit -1
    else
        echo "Node exists and is in use, continuing"
    fi

    echo "Updating OBD signals in signal catalog..."
    if UPDATE_SIGNAL_CATALOG_STATUS=`aws iotfleetwise update-signal-catalog \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${SIGNAL_CATALOG_NAME} \
        --description "OBD signals" \
        --nodes-to-update "${OBD_NODES}" 2>&1`; then
        echo ${UPDATE_SIGNAL_CATALOG_STATUS} | jq -r .arn
    elif ! echo ${UPDATE_SIGNAL_CATALOG_STATUS} | grep -q "InvalidSignalsException"; then
        echo ${UPDATE_SIGNAL_CATALOG_STATUS} >&2
        exit -1
    else
        echo "Signals exist and are in use, continuing"
    fi

    echo "Updating DBC signals in signal catalog..."
    if UPDATE_SIGNAL_CATALOG_STATUS=`aws iotfleetwise update-signal-catalog \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${SIGNAL_CATALOG_NAME} \
        --description "DBC signals" \
        --nodes-to-update "${DBC_NODES}" 2>&1`; then
        echo ${UPDATE_SIGNAL_CATALOG_STATUS} | jq -r .arn
    elif ! echo ${UPDATE_SIGNAL_CATALOG_STATUS} | grep -q "InvalidSignalsException"; then
        echo ${UPDATE_SIGNAL_CATALOG_STATUS} >&2
        exit -1
    else
        echo "Signals exist and are in use, continuing"
    fi
fi

echo "Creating model manifest..."
# Make a list of all node names:
NODE_LIST=`( echo ${DBC_NODES} | jq -r .[].sensor.fullyQualifiedName | grep Vehicle\\. ; \
             echo ${OBD_NODES} | jq -r .[].sensor.fullyQualifiedName | grep Vehicle\\. ) | jq -Rn [inputs]`
aws iotfleetwise create-model-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-model-manifest \
    --signal-catalog-arn ${SIGNAL_CATALOG_ARN} \
    --nodes "${NODE_LIST}" | jq -r .arn

echo "Activating model manifest..."
MODEL_MANIFEST_ARN=`aws iotfleetwise update-model-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-model-manifest \
    --status ACTIVE | jq -r .arn`
echo ${MODEL_MANIFEST_ARN}

echo "Creating decoder manifest with OBD signals..."
NETWORK_INTERFACES=`cat network-interfaces.json`
OBD_SIGNAL_DECODERS=`cat obd-decoders.json`
DECODER_MANIFEST_ARN=`aws iotfleetwise create-decoder-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-decoder-manifest \
    --model-manifest-arn ${MODEL_MANIFEST_ARN} \
    --network-interfaces "${NETWORK_INTERFACES}" \
    --signal-decoders "${OBD_SIGNAL_DECODERS}" | jq -r .arn`
echo ${DECODER_MANIFEST_ARN}

echo "Adding DBC signals to decoder manifest..."
DBC=`cat hscan.dbc | base64 -w0`
# Make map of node name to DBC signal name, i.e. {"Vehicle.SignalName":"SignalName"...}
NODE_TO_DBC_MAP=`echo ${DBC_NODES} | jq '.[].sensor.fullyQualifiedName//""|match("Vehicle\\\\.(.+)")|{(.captures[0].string):.string}'|jq -s add`
NETWORK_FILE_DEFINITIONS=`echo [] \
    | jq .[0].canDbc.signalsMap="${NODE_TO_DBC_MAP}" \
    | jq .[0].canDbc.networkInterface="\"1\"" \
    | jq .[0].canDbc.canDbcFiles[0]="\"${DBC}\""`
aws iotfleetwise import-decoder-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-decoder-manifest \
    --network-file-definitions "${NETWORK_FILE_DEFINITIONS}" | jq -r .arn

echo "Activating decoder manifest..."
aws iotfleetwise update-decoder-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-decoder-manifest \
    --status ACTIVE | jq -r .arn

if ((FLEET_SIZE==1)); then
    echo "Creating vehicle ${VEHICLE_NAME}..."
    aws iotfleetwise create-vehicle \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --decoder-manifest-arn ${DECODER_MANIFEST_ARN} \
        --association-behavior ValidateIotThingExists \
        --model-manifest-arn ${MODEL_MANIFEST_ARN} \
        --vehicle-name "${VEHICLE_NAME}" | jq -r .arn
else
    echo "Creating vehicle ${VEHICLE_NAME}-0..$((FLEET_SIZE-1))..."
    for ((i=0; i<${FLEET_SIZE}; i+=${BATCH_SIZE})); do
        for ((j=0; j<${BATCH_SIZE} && i+j<${FLEET_SIZE}; j++)); do
            # This output group is run in a background process. Note that stderr is redirected to stream 3 and back,
            # to print stderr from the output group, but not info about the background process.
            { \
                aws iotfleetwise create-vehicle \
                    ${ENDPOINT_URL_OPTION} --region ${REGION} \
                    --decoder-manifest-arn ${DECODER_MANIFEST_ARN} \
                    --association-behavior ValidateIotThingExists \
                    --model-manifest-arn ${MODEL_MANIFEST_ARN} \
                    --vehicle-name "${VEHICLE_NAME}-$((i+j))" >/dev/null \
            2>&3 &} 3>&2 2>/dev/null
        done
        # Wait for all background processes to finish
        wait
    done
fi

echo "Creating fleet..."
FLEET_ARN=`aws iotfleetwise create-fleet \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --fleet-id ${NAME}-fleet \
    --description "Description is required" \
    --signal-catalog-arn ${SIGNAL_CATALOG_ARN} | jq -r .arn`
echo ${FLEET_ARN}

if ((FLEET_SIZE==1)); then
    echo "Associating vehicle ${VEHICLE_NAME}..."
    aws iotfleetwise associate-vehicle-fleet \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --fleet-id ${NAME}-fleet \
        --vehicle-name "${VEHICLE_NAME}"
else
    echo "Associating vehicle ${VEHICLE_NAME}-0..$((FLEET_SIZE-1))..."
    for ((i=0; i<${FLEET_SIZE}; i+=${BATCH_SIZE})); do
        for ((j=0; j<${BATCH_SIZE} && i+j<${FLEET_SIZE}; j++)); do
            # This output group is run in a background process. Note that stderr is redirected to stream 3 and back,
            # to print stderr from the output group, but not info about the background process.
            { \
                aws iotfleetwise associate-vehicle-fleet \
                    ${ENDPOINT_URL_OPTION} --region ${REGION} \
                    --fleet-id ${NAME}-fleet \
                    --vehicle-name "${VEHICLE_NAME}-$((i+j))" \
            2>&3 &} 3>&2 2>/dev/null
        done
        # Wait for all background processes to finish
        wait
    done
fi

echo "Creating campaign from ${CAMPAIGN_FILE}..."
CAMPAIGN=`cat ${CAMPAIGN_FILE} \
    | jq .name=\"${NAME}-campaign\" \
    | jq .signalCatalogArn=\"${SIGNAL_CATALOG_ARN}\" \
    | jq .targetArn=\"${FLEET_ARN}\"`
aws iotfleetwise create-campaign \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --cli-input-json "${CAMPAIGN}" | jq -r .arn

echo "Waiting for campaign to become ready for approval..."
while true; do
    sleep 5
    CAMPAIGN_STATUS=`aws iotfleetwise get-campaign \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${NAME}-campaign | jq -r .status`
    if [ "${CAMPAIGN_STATUS}" == "WAITING_FOR_APPROVAL" ]; then
        break
    fi
done

echo "Approving campaign..."
aws iotfleetwise update-campaign \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-campaign \
    --action APPROVE | jq -r .arn

check_vehicle_healthy() {
    for ((k=0; k<${HEALTH_CHECK_RETRIES}; k++)); do
        VEHICLE_STATUS=`aws iotfleetwise get-vehicle-status \
            ${ENDPOINT_URL_OPTION} --region ${REGION} \
            --vehicle-name "$1"`
        for ((l=0; ; l++)); do
            CAMPAIGN_NAME=`echo ${VEHICLE_STATUS} | jq -r .campaigns[${l}].campaignName`
            CAMPAIGN_STATUS=`echo ${VEHICLE_STATUS} | jq -r .campaigns[${l}].status`
            # If the campaign was not found (when the index is out-of-range jq will return 'null')
            if [ "${CAMPAIGN_NAME}" == "null" ]; then
                echo "Error: Campaign not found in vehicle status for vehicle $1" >&2
                exit -1
            # If the campaign was found \
            elif [ "${CAMPAIGN_NAME}" == "${NAME}-campaign" ]; then
                if [ "${CAMPAIGN_STATUS}" == "HEALTHY" ]; then
                    break 2
                fi
                break
            fi
        done
        sleep 5
    done
    if ((k>=HEALTH_CHECK_RETRIES)); then
        echo "Error: Health check timeout for vehicle $1" >&2
        exit -1
    fi
}

if ((FLEET_SIZE==1)); then
    echo "Waiting until status of vehicle ${VEHICLE_NAME} is healthy..."
    check_vehicle_healthy "${VEHICLE_NAME}"
else
    echo "Waiting until status of vehicle ${VEHICLE_NAME}-0..$((FLEET_SIZE-1)) is healthy..."
    for ((i=0; i<${FLEET_SIZE}; i+=${BATCH_SIZE})); do
        for ((j=0; j<${BATCH_SIZE} && i+j<${FLEET_SIZE}; j++)); do
            # This output group is run in a background process. Note that stderr is redirected to stream 3 and back,
            # to print stderr from the output group, but not info about the background process.
            { \
                check_vehicle_healthy "${VEHICLE_NAME}-$((i+j))" \
            2>&3 &} 3>&2 2>/dev/null
        done
        # Wait for all background processes to finish
        wait
    done
fi

DELAY=30
echo "Waiting ${DELAY} seconds for data to be collected..."
sleep ${DELAY}

echo "Querying Timestream..."
aws timestream-query query \
    --region ${REGION} \
    --query-string "SELECT * FROM \"${TIMESTREAM_DB_NAME}\".\"${TIMESTREAM_TABLE_NAME}\" \
        WHERE vehicleName = '${VEHICLE_NAME}`if ((FLEET_SIZE>1)); then echo "-0"; fi`' \
        AND time between ago(1m) and now() ORDER BY time ASC" \
    > ${NAME}-timestream-result.json

echo "Converting to HTML..."
OUTPUT_FILE_HTML="${NAME}.html"
python3.7 timestream-to-html.py ${NAME}-timestream-result.json ${OUTPUT_FILE_HTML}

echo "You can now view the collected data."
echo "----------------------------------"
echo "| Collected data in HTML format: |"
echo "----------------------------------"
echo `pwd`/${OUTPUT_FILE_HTML}

if [ ${CLEAN_UP} == true ]; then
    ./clean-up.sh \
        --vehicle-name ${VEHICLE_NAME} \
        --fleet-size ${FLEET_SIZE} \
        --timestamp ${TIMESTAMP} \
        ${ENDPOINT_URL_OPTION} \
        --region ${REGION}
fi
