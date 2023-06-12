#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ENDPOINT_URL=""
ENDPOINT_URL_OPTION=""
REGION="us-east-1"
TIMESTAMP_FILENAME="config/timestamp.txt"
TIMESTAMP=`date +%s`
VEHICLE_NAME_FILENAME="config/vehicle-name.txt"
VEHICLE_NAME=""
SERVICE_ROLE="IoTFleetWiseServiceRole-${TIMESTAMP}"
DEFAULT_TIMESTREAM_DB_NAME="IoTFleetWiseDB-${TIMESTAMP}"
SERVICE_PRINCIPAL="iotfleetwise.amazonaws.com"
TIMESTREAM_DB_NAME_FILENAME="config/timestream-db-name.txt"
TIMESTREAM_DB_NAME=""
TIMESTREAM_TABLE_NAME="VehicleDataTable"
SERVICE_ROLE_ARN_FILENAME="config/service-role-arn.txt"
SERVICE_ROLE_ARN=""
CAMPAIGN_FILE="campaign-android.json"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --vehicle-name)
            VEHICLE_NAME=$2
            shift
            ;;
        --timestream-db-name)
            TIMESTREAM_DB_NAME=$2
            shift
            ;;
        --timestream-table-name)
            TIMESTREAM_TABLE_NAME=$2
            shift
            ;;
        --service-role-arn)
            SERVICE_ROLE_ARN=$2
            shift
            ;;
        --service-principal)
            SERVICE_PRINCIPAL=$2
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
            echo "  --vehicle-name <NAME>            Vehicle name, by default will be read from ${VEHICLE_NAME_FILENAME}"
            echo "  --timestream-db-name <NAME>      Timestream database name, by default will be read from ${TIMESTREAM_DB_NAME_FILENAME}"
            echo "  --timestream-table-name <NAME>   Timetream table name, default: ${TIMESTREAM_TABLE_NAME}"
            echo "  --service-role-arn <ARN>         Service role ARN for accessing Timestream, by default will be read from ${SERVICE_ROLE_ARN_FILENAME}"
            echo "  --endpoint-url <URL>             The endpoint URL used for AWS CLI calls"
            echo "  --region <REGION>                The region used for AWS CLI calls, default: ${REGION}"
            echo "  --service-principal <PRINCIPAL>  AWS service principal for policies, default: ${SERVICE_PRINCIPAL}"
            exit 0
            ;;
        esac
        shift
    done

    if [ "${ENDPOINT_URL}" != "" ]; then
        ENDPOINT_URL_OPTION="--endpoint-url ${ENDPOINT_URL}"
    fi

    if [ "${VEHICLE_NAME}" == "" ]; then
        if [ -f "${VEHICLE_NAME_FILENAME}" ]; then
            VEHICLE_NAME=`cat ${VEHICLE_NAME_FILENAME}`
        else
            echo "Error: provision.sh script not run"
            exit -1
        fi
    fi

    if [ "${TIMESTREAM_DB_NAME}" == "" ]; then
        if [ -f "${TIMESTREAM_DB_NAME_FILENAME}" ]; then
            TIMESTREAM_DB_NAME=`cat ${TIMESTREAM_DB_NAME_FILENAME}`
        fi
    fi

    if [ "${SERVICE_ROLE_ARN}" == "" ]; then
        if [ -f "${SERVICE_ROLE_ARN_FILENAME}" ]; then
            SERVICE_ROLE_ARN=`cat ${SERVICE_ROLE_ARN_FILENAME}`
        fi
    fi
}

echo "================================"
echo "AWS IoT FleetWise Android Script"
echo "================================"

mkdir -p config
parse_args "$@"

echo "Getting AWS account ID..."
ACCOUNT_ID=`aws sts get-caller-identity --query "Account" --output text`
echo ${ACCOUNT_ID}

if [ "${TIMESTREAM_DB_NAME}" != "" ] && [ "${SERVICE_ROLE_ARN}" != "" ]; then
    TIMESTREAM_TABLE_ARN="arn:aws:timestream:${REGION}:${ACCOUNT_ID}:database/${TIMESTREAM_DB_NAME}/table/${TIMESTREAM_TABLE_NAME}"
else
    TIMESTREAM_DB_NAME=${DEFAULT_TIMESTREAM_DB_NAME}

    echo "Creating Timestream database..."
    aws timestream-write create-database \
        --region ${REGION} \
        --database-name ${TIMESTREAM_DB_NAME} | jq -r .Database.Arn

    echo "Creating Timestream table..."
    TIMESTREAM_TABLE_ARN=$( aws timestream-write create-table \
        --region ${REGION} \
        --database-name ${TIMESTREAM_DB_NAME} \
        --table-name ${TIMESTREAM_TABLE_NAME} \
        --retention-properties "{\"MemoryStoreRetentionPeriodInHours\":2, \
            \"MagneticStoreRetentionPeriodInDays\":2}" | jq -r .Table.Arn )

    echo "Creating service role..."
    SERVICE_ROLE_TRUST_POLICY=$(cat << EOF
{
    "Version": "2012-10-17",
    "Statement": [
        {
        "Effect": "Allow",
        "Principal": {
            "Service": [
                "$SERVICE_PRINCIPAL"
            ]
        },
        "Action": "sts:AssumeRole"
        }
    ]
}
EOF
)
    SERVICE_ROLE_ARN=`aws iam create-role \
        --role-name "${SERVICE_ROLE}" \
        --assume-role-policy-document "${SERVICE_ROLE_TRUST_POLICY}" | jq -r .Role.Arn`
    echo ${SERVICE_ROLE_ARN}

    echo "Waiting for role to be created..."
    aws iam wait role-exists \
        --role-name "${SERVICE_ROLE}"

    echo "Creating service role policy..."
    SERVICE_ROLE_POLICY=$(cat <<'EOF'
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Sid": "timestreamIngestion",
            "Effect": "Allow",
            "Action": [
                "timestream:WriteRecords",
                "timestream:Select"
            ]
        },
        {
            "Sid": "timestreamDescribeEndpoint",
            "Effect": "Allow",
            "Action": [
                "timestream:DescribeEndpoints"
            ],
            "Resource": "*"
        }
    ]
}
EOF
)
    SERVICE_ROLE_POLICY=`echo "${SERVICE_ROLE_POLICY}" \
        | jq ".Statement[0].Resource=\"arn:aws:timestream:${REGION}:${ACCOUNT_ID}:database/${TIMESTREAM_DB_NAME}/*\""`
    SERVICE_ROLE_POLICY_ARN=`aws iam create-policy \
        --policy-name ${SERVICE_ROLE}-policy \
        --policy-document "${SERVICE_ROLE_POLICY}" | jq -r .Policy.Arn`
    echo ${SERVICE_ROLE_POLICY_ARN}

    echo "Waiting for policy to be created..."
    aws iam wait policy-exists \
        --policy-arn "${SERVICE_ROLE_POLICY_ARN}"

    echo "Attaching policy to service role..."
    aws iam attach-role-policy \
        --policy-arn ${SERVICE_ROLE_POLICY_ARN} \
        --role-name "${SERVICE_ROLE}"

    echo ${TIMESTREAM_DB_NAME} > ${TIMESTREAM_DB_NAME_FILENAME}
    echo ${SERVICE_ROLE_ARN} > ${SERVICE_ROLE_ARN_FILENAME}
fi

NAME="${VEHICLE_NAME}-${TIMESTAMP}"
echo ${TIMESTAMP} > ${TIMESTAMP_FILENAME}

echo "Deleting vehicle ${VEHICLE_NAME} if it already exists..."
aws iotfleetwise delete-vehicle \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --vehicle-name "${VEHICLE_NAME}" | jq -r .arn

VEHICLE_NODE=`cat ../../cloud/vehicle-node.json`
OBD_NODES=`cat ../../cloud/obd-nodes.json`
DBC_NODES=`cat externalGpsNodes.json`

echo "Checking for existing signal catalog..."
SIGNAL_CATALOG_LIST=`aws iotfleetwise list-signal-catalogs \
    ${ENDPOINT_URL_OPTION} --region ${REGION}`
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
OBD_SIGNAL_DECODERS=`cat ../../cloud/obd-decoders.json`
DECODER_MANIFEST_ARN=`aws iotfleetwise create-decoder-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-decoder-manifest \
    --model-manifest-arn ${MODEL_MANIFEST_ARN} \
    --network-interfaces "${NETWORK_INTERFACES}" \
    --signal-decoders "${OBD_SIGNAL_DECODERS}" | jq -r .arn`
echo ${DECODER_MANIFEST_ARN}

SIGNAL_DECODERS=`cat externalGpsDecoders.json`
aws iotfleetwise update-decoder-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-decoder-manifest \
    --signal-decoders-to-add "${SIGNAL_DECODERS}" | jq -r .arn

echo "Activating decoder manifest..."
aws iotfleetwise update-decoder-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-decoder-manifest \
    --status ACTIVE | jq -r .arn

echo "Creating vehicle ${VEHICLE_NAME}..."
aws iotfleetwise create-vehicle \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --decoder-manifest-arn ${DECODER_MANIFEST_ARN} \
    --association-behavior ValidateIotThingExists \
    --model-manifest-arn ${MODEL_MANIFEST_ARN} \
    --vehicle-name "${VEHICLE_NAME}" | jq -r .arn

echo "Creating fleet..."
FLEET_ARN=`aws iotfleetwise create-fleet \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --fleet-id ${NAME}-fleet \
    --description "Description is required" \
    --signal-catalog-arn ${SIGNAL_CATALOG_ARN} | jq -r .arn`
echo ${FLEET_ARN}

echo "Associating vehicle ${VEHICLE_NAME}..."
aws iotfleetwise associate-vehicle-fleet \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --fleet-id ${NAME}-fleet \
    --vehicle-name "${VEHICLE_NAME}"

echo "Creating campaign from ${CAMPAIGN_FILE}..."
CAMPAIGN=`cat ${CAMPAIGN_FILE} \
    | jq .name=\"${NAME}-campaign\" \
    | jq .signalCatalogArn=\"${SIGNAL_CATALOG_ARN}\" \
    | jq .targetArn=\"${FLEET_ARN}\"`
aws iotfleetwise create-campaign \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --cli-input-json "${CAMPAIGN}" --data-destination-configs "[{\"timestreamConfig\":{\"timestreamTableArn\":\"${TIMESTREAM_TABLE_ARN}\",\"executionRoleArn\":\"${SERVICE_ROLE_ARN}\"}}]" | jq -r .arn

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

echo "========="
echo "Finished!"
echo "========="
