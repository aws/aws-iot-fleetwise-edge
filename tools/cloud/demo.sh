#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ACCOUNT_ID=`aws sts get-caller-identity --query "Account" --output text`
UUID="$(cat /proc/sys/kernel/random/uuid)"
# A short string to avoid resource name conflicts. Since most resources names have a small
# characters limit, using a full uuid would not be possible. As those names just need to be unique
# in the same account, truncating the uuid should be enough.
DISAMBIGUATOR="${UUID:0:8}"
S3_SUFFIX="${ACCOUNT_ID}-${DISAMBIGUATOR}"
ENDPOINT_URL=""
ENDPOINT_URL_OPTION=""
REGION="us-east-1"
DEFAULT_VEHICLE_NAME="fwdemo"
VEHICLE_NAME=""
TIMESTREAM_DB_NAME="IoTFleetWiseDB-${DISAMBIGUATOR}"
TIMESTREAM_TABLE_NAME="VehicleDataTable"
SERVICE_ROLE="IoTFleetWiseServiceRole"
SERVICE_ROLE_POLICY_ARN=""
SERVICE_PRINCIPAL="iotfleetwise.amazonaws.com"
BUCKET_NAME=""
SKIP_S3_POLICY=true
CAMPAIGN_FILE=""
DEFAULT_CAMPAIGN_FILE="campaign-brake-event.json"
DBC_FILE=""
DEFAULT_DBC_FILE="hscan.dbc"
CLEAN_UP=false
S3_UPLOAD=false
FLEET_SIZE=1
BATCH_SIZE=$((`nproc`*4))
HEALTH_CHECK_RETRIES=360 # About 30mins
MAX_ATTEMPTS_ON_REGISTRATION_FAILURE=5
FORCE_REGISTRATION=false
MIN_CLI_VERSION="aws-cli/2.11.24"
CREATED_SIGNAL_CATALOG_NAME=""

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
        --clean-up)
            CLEAN_UP=true
            ;;
        --enable-s3-upload)
            S3_UPLOAD=true
            ;;
        --campaign-file)
            CAMPAIGN_FILE=$2
            shift
            ;;
        --dbc-file)
            DBC_FILE=$2
            shift
            ;;
        --bucket-name)
            BUCKET_NAME=$2
            shift
            ;;
        --set-bucket-policy)
            SKIP_S3_POLICY=false
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
            echo "  --vehicle-name <NAME>   Vehicle name"
            echo "  --fleet-size <SIZE>     Size of fleet, default: ${FLEET_SIZE}. When greater than 1,"
            echo "                          the instance number will be appended to each"
            echo "                          Vehicle name after a '-', e.g. ${DEFAULT_VEHICLE_NAME}-42"
            echo "  --campaign-file <FILE>  Campaign JSON file, default: ${DEFAULT_CAMPAIGN_FILE}"
            echo "  --dbc-file <FILE>       DBC file, default: ${DEFAULT_DBC_FILE}"
            echo "  --bucket-name <NAME>    S3 bucket name, if not specified a new bucket will be created"
            echo "  --set-bucket-policy     Sets the required bucket policy"
            echo "  --clean-up              Delete created resources"
            echo "  --enable-s3-upload      Create campaigns to upload data to S3"
            echo "  --endpoint-url <URL>    The endpoint URL used for AWS CLI calls"
            echo "  --service-principal <PRINCIPAL>    AWS service principal for policies, default: ${SERVICE_PRINCIPAL}"
            echo "  --region <REGION>       The region used for AWS CLI calls, default: ${REGION}"
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

if [ "${DBC_FILE}" != "" ] && [ "${CAMPAIGN_FILE}" == "" ]; then
    echo -n "Enter campaign file name: "
    read CAMPAIGN_FILE
    if [ "${CAMPAIGN_FILE}" == "" ]; then
        echo "Error: Please provide campaign file name for custom DBC file" >&2
        exit -1
    fi
fi

if [ "${CAMPAIGN_FILE}" == "" ]; then
    CAMPAIGN_FILE=${DEFAULT_CAMPAIGN_FILE}
fi

if [ ${S3_UPLOAD} == true ] && [ "${BUCKET_NAME}" == "" ]; then
    BUCKET_NAME="iot-fleetwise-demo-${S3_SUFFIX}"
    SKIP_S3_POLICY=false
fi

if [ ${FLEET_SIZE} -gt 1 ]; then
    VEHICLES=( $(seq -f "${VEHICLE_NAME}-%g" 0 $((${FLEET_SIZE}-1))) )
else
    VEHICLES=( ${VEHICLE_NAME} )
fi

NAME="${VEHICLE_NAME}-${DISAMBIGUATOR}"
SERVICE_ROLE="${SERVICE_ROLE}-${REGION}-${DISAMBIGUATOR}"

echo -n "Date: "
date +%Y-%m-%dT%H:%M:%S%z
echo "Disambiguator: ${DISAMBIGUATOR}"
echo "Vehicle name: ${VEHICLE_NAME}"
echo "Fleet Size: ${FLEET_SIZE}"
echo "Vehicles: ${VEHICLES[@]}"

# AWS CLI v1.x has a double base64 encoding issue
echo "Checking AWS CLI version..."
CLI_VERSION=`aws --version | grep -Eo "aws-cli/[[:digit:]]+.[[:digit:]]+.[[:digit:]]+"`
echo "${CLI_VERSION}"

if [[ "${CLI_VERSION}" < "${MIN_CLI_VERSION}" ]]; then
    echo "Error: Please update AWS CLI to ${MIN_CLI_VERSION} or newer" >&2
    exit -1
fi

save_variables() {
    # Export some variables to a .env file so that they can be referenced by other scripts
    echo "
VEHICLE_NAME=${VEHICLE_NAME}
FLEET_SIZE=${FLEET_SIZE}
DISAMBIGUATOR=${DISAMBIGUATOR}
ENDPOINT_URL=${ENDPOINT_URL}
REGION=${REGION}
SIGNAL_CATALOG=${CREATED_SIGNAL_CATALOG_NAME}
TIMESTREAM_DB_NAME=${TIMESTREAM_DB_NAME}
TIMESTREAM_TABLE_NAME=${TIMESTREAM_TABLE_NAME}
BUCKET_NAME=${BUCKET_NAME}
" > demo.env
}

cleanup() {
    save_variables
    if [ ${CLEAN_UP} == true ]; then
        ./clean-up.sh
    fi
}

register_account() {
    echo "Registering account..."
    aws iotfleetwise register-account \
        ${ENDPOINT_URL_OPTION} --region ${REGION} | jq -r .registerAccountStatus
    echo "Waiting for account to be registered..."
}

get_account_status() {
    aws iotfleetwise get-register-account-status ${ENDPOINT_URL_OPTION} --region ${REGION}
}

# $1 is the campaign name
approve_campaign() {
    echo "Waiting for campaign to become ready for approval..."
    while true; do
        sleep 5
        CAMPAIGN_STATUS=`aws iotfleetwise get-campaign \
            ${ENDPOINT_URL_OPTION} --region ${REGION} \
            --name $1`
        echo ${CAMPAIGN_STATUS} | jq -r .arn
        if [ `echo ${CAMPAIGN_STATUS} | jq -r .status` == "WAITING_FOR_APPROVAL" ]; then
            break
        fi
    done

    echo "Approving campaign..."
    aws iotfleetwise update-campaign \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name $1 \
        --action APPROVE | jq -r .arn
}

trap cleanup EXIT

echo "Getting AWS account ID..."
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
if [ "${ACCOUNT_STATUS}" == "REGISTRATION_SUCCESS" ]; then
    echo "Account is already registered"
elif [ "${ACCOUNT_STATUS}" == "REGISTRATION_PENDING" ]; then
    echo "Waiting for account to be registered..."
else
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

for ((i=0; i<${FLEET_SIZE}; i+=${BATCH_SIZE})); do
    for ((j=0; j<${BATCH_SIZE} && i+j<${FLEET_SIZE}; j++)); do
        VEHICLE=${VEHICLES[$((i+j))]}
        echo "Deleting vehicle ${VEHICLE} if it already exists..."
        # This output group is run in a background process. Note that stderr is redirected to stream 3 and back,
        # to print stderr from the output group, but not info about the background process.
        { \
            aws iotfleetwise delete-vehicle \
                ${ENDPOINT_URL_OPTION} --region ${REGION} \
                --vehicle-name "${VEHICLE}" | jq -r .arn \
        2>&3 &} 3>&2 2>/dev/null
    done
    # Wait for all background processes to finish
    wait
done

if [ ${S3_UPLOAD} == true ]; then
    echo "S3 upload is enabled"
    echo "Checking if S3 bucket exists..."

    BUCKET_LIST=$( aws s3 ls )
    if grep -q "$BUCKET_NAME" <<< "$BUCKET_LIST"; then
        echo "S3 bucket already exists"
    else
        echo "Creating S3 bucket..."
        aws s3 mb s3://$BUCKET_NAME --region $REGION
    fi
    if [ ${SKIP_S3_POLICY} == false ]; then
        echo "Adding S3 bucket policy..."
        cat << EOF > s3-bucket-policy.json
{
    "Statement": [
        {
            "Effect": "Allow",
            "Principal": {
                "Service": "${SERVICE_PRINCIPAL}"
            },
            "Action": "s3:ListBucket",
            "Resource": "arn:aws:s3:::${BUCKET_NAME}"
        },
        {
            "Effect": "Allow",
            "Principal": {
                "Service": "${SERVICE_PRINCIPAL}"
            },
            "Action": ["s3:GetObject", "s3:PutObject"],
            "Resource": "arn:aws:s3:::${BUCKET_NAME}/*"
        }
    ]
}
EOF
        aws s3api put-bucket-policy --bucket $BUCKET_NAME --policy file://s3-bucket-policy.json
    else
        echo "Skipping S3 bucket policy. Since bucket already existed it needs to be manually configured."
    fi
    echo "Getting S3 bucket region..."
    BUCKET_REGION=`aws s3api get-bucket-location --bucket ${BUCKET_NAME} | jq -r .LocationConstraint`
    if [ -z "${BUCKET_REGION}" ] || [ "${BUCKET_REGION}" == "null" ]; then
        BUCKET_REGION="us-east-1"
    fi
    echo ${BUCKET_REGION}
    if [ "${BUCKET_REGION}" != "${REGION}" ]; then
        echo "Error: S3 bucket ${BUCKET_NAME} is not in region ${REGION}. Cross-region not yet supported."
        exit -1
    fi
    echo "Checking bucket ACLs are disabled..."
    if ! OWNERSHIP_CONTROLS=`aws s3api get-bucket-ownership-controls --bucket ${BUCKET_NAME} 2> /dev/null` \
        || [ "`echo ${OWNERSHIP_CONTROLS} | jq -r '.OwnershipControls.Rules[0].ObjectOwnership'`" != "BucketOwnerEnforced" ]; then
        echo "Error: ACLs are enabled for bucket ${BUCKET_NAME}. Disable them at https://s3.console.aws.amazon.com/s3/bucket/${BUCKET_NAME}/property/oo/edit"
        exit -1
    fi
fi

VEHICLE_NODE=`cat vehicle-node.json`
OBD_NODES=`cat obd-nodes.json`
if [ "${DBC_FILE}" == "" ]; then
    DBC_NODES=`python3 dbc-to-nodes.py ${DEFAULT_DBC_FILE}`
else
    DBC_NODES=`python3 dbc-to-nodes.py ${DBC_FILE}`
fi

if SIGNAL_CATALOG_ARN=`aws iotfleetwise create-signal-catalog \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${NAME}-signal-catalog \
        --nodes "${VEHICLE_NODE}" 2>/dev/null | jq -r .arn`; then

    echo "Created new signal catalog: ${SIGNAL_CATALOG_ARN}"
    CREATED_SIGNAL_CATALOG_NAME="${NAME}-signal-catalog"

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

    echo "Add an attribute to signal catalog..."
    aws iotfleetwise update-signal-catalog \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${NAME}-signal-catalog \
        --description "DBC Attributes" \
        --nodes-to-add '[{
            "attribute": {
                "dataType": "STRING",
                "description": "Color",
                "fullyQualifiedName": "Vehicle.Color",
                "defaultValue":"Red"
            }}
        ]' | jq -r .arn
else
    echo "Checking for existing signal catalogs..."
    SIGNAL_CATALOG_LIST=`aws iotfleetwise list-signal-catalogs \
        ${ENDPOINT_URL_OPTION} --region ${REGION}`
    SIGNAL_CATALOG_NAME=`echo ${SIGNAL_CATALOG_LIST} | jq -r .summaries[0].name`
    SIGNAL_CATALOG_ARN=`echo ${SIGNAL_CATALOG_LIST} | jq -r .summaries[0].arn`
    echo "Reusing existing signal catalog: ${SIGNAL_CATALOG_ARN}"

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

    echo "Updating color attribute"
    if UPDATE_SIGNAL_CATALOG_STATUS=`aws iotfleetwise update-signal-catalog \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${SIGNAL_CATALOG_NAME} \
        --description "DBC Attributes" \
        --nodes-to-update '[{
            "attribute": {
                "dataType": "STRING",
                "description": "Color",
                "fullyQualifiedName": "Vehicle.Color",
                "defaultValue":"Red"
            }}
        ]' 2>&1`; then
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

echo "Updating attribute in model manifest..."
MODEL_MANIFEST_ARN=`aws iotfleetwise update-model-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-model-manifest \
    --nodes-to-add 'Vehicle.Color' | jq -r .arn`
echo ${MODEL_MANIFEST_ARN}

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
if [ "${DBC_FILE}" == "" ]; then
    DBC=`cat ${DEFAULT_DBC_FILE} | base64 -w0`
    # Make map of node name to DBC signal name, i.e. {"Vehicle.SignalName":"SignalName"...}
    NODE_TO_DBC_MAP=`echo ${DBC_NODES} | jq '.[].sensor.fullyQualifiedName//""|match("Vehicle\\\\.\\\\w+\\\\.(.+)")|{(.captures[0].string):.string}'|jq -s add`
    NETWORK_FILE_DEFINITIONS=`echo [] \
        | jq .[0].canDbc.signalsMap="${NODE_TO_DBC_MAP}" \
        | jq .[0].canDbc.networkInterface="\"1\"" \
        | jq .[0].canDbc.canDbcFiles[0]="\"${DBC}\""`
    aws iotfleetwise import-decoder-manifest \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${NAME}-decoder-manifest \
        --network-file-definitions "${NETWORK_FILE_DEFINITIONS}" | jq -r .arn
else
    SIGNAL_DECODERS=`python3 dbc-to-json.py ${DBC_FILE}`
    aws iotfleetwise update-decoder-manifest \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${NAME}-decoder-manifest \
        --signal-decoders-to-add "${SIGNAL_DECODERS}" | jq -r .arn
fi

echo "Activating decoder manifest..."
aws iotfleetwise update-decoder-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-decoder-manifest \
    --status ACTIVE | jq -r .arn

echo "Waiting for decoder manifest to become active..."
while true; do
    sleep 5
    DECODER_MANIFEST_STATUS=`aws iotfleetwise get-decoder-manifest \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${NAME}-decoder-manifest`
    echo ${DECODER_MANIFEST_STATUS} | jq -r .arn
    if [ `echo ${DECODER_MANIFEST_STATUS} | jq -r .status` == "ACTIVE" ]; then
        break
    fi
done

for ((i=0; i<${FLEET_SIZE}; i+=${BATCH_SIZE})); do
    for ((j=0; j<${BATCH_SIZE} && i+j<${FLEET_SIZE}; j++)); do
        VEHICLE=${VEHICLES[$((i+j))]}
        echo "Creating vehicle ${VEHICLE}..."
        # This output group is run in a background process. Note that stderr is redirected to stream 3 and back,
        # to print stderr from the output group, but not info about the background process.
        { \
            aws iotfleetwise create-vehicle \
                ${ENDPOINT_URL_OPTION} --region ${REGION} \
                --decoder-manifest-arn ${DECODER_MANIFEST_ARN} \
                --association-behavior ValidateIotThingExists \
                --model-manifest-arn ${MODEL_MANIFEST_ARN} \
                --attributes '{"Vehicle.Color":"Red"}' \
                --vehicle-name "${VEHICLE}" >/dev/null \
        2>&3 &} 3>&2 2>/dev/null
    done
    # Wait for all background processes to finish
    wait
done

echo "Creating fleet..."
FLEET_ARN=`aws iotfleetwise create-fleet \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --fleet-id ${NAME}-fleet \
    --description "Description is required" \
    --signal-catalog-arn ${SIGNAL_CATALOG_ARN} | jq -r .arn`
echo ${FLEET_ARN}

for ((i=0; i<${FLEET_SIZE}; i+=${BATCH_SIZE})); do
    for ((j=0; j<${BATCH_SIZE} && i+j<${FLEET_SIZE}; j++)); do
        VEHICLE=${VEHICLES[$((i+j))]}
        echo "Associating vehicle ${VEHICLE}..."
        # This output group is run in a background process. Note that stderr is redirected to stream 3 and back,
        # to print stderr from the output group, but not info about the background process.
        { \
            aws iotfleetwise associate-vehicle-fleet \
                ${ENDPOINT_URL_OPTION} --region ${REGION} \
                --fleet-id ${NAME}-fleet \
                --vehicle-name "${VEHICLE}" \
        2>&3 &} 3>&2 2>/dev/null
    done
    # Wait for all background processes to finish
    wait
done

echo "Creating campaign from ${CAMPAIGN_FILE}..."
CAMPAIGN=`cat ${CAMPAIGN_FILE} \
    | jq .name=\"${NAME}-campaign\" \
    | jq .signalCatalogArn=\"${SIGNAL_CATALOG_ARN}\" \
    | jq .targetArn=\"${FLEET_ARN}\"`
aws iotfleetwise create-campaign \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --cli-input-json "${CAMPAIGN}" --data-destination-configs "[{\"timestreamConfig\":{\"timestreamTableArn\":\"${TIMESTREAM_TABLE_ARN}\",\"executionRoleArn\":\"${SERVICE_ROLE_ARN}\"}}]"| jq -r .arn

approve_campaign ${NAME}-campaign

if [ ${S3_UPLOAD} == true ]; then
    echo "Creating campaign from ${CAMPAIGN_FILE} for S3..."
    CAMPAIGN=`cat ${CAMPAIGN_FILE} \
        | jq .name=\"${NAME}-campaign-s3-json\" \
        | jq .signalCatalogArn=\"${SIGNAL_CATALOG_ARN}\" \
        | jq .targetArn=\"${FLEET_ARN}\"`
    aws iotfleetwise create-campaign \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --cli-input-json "${CAMPAIGN}" --data-destination-configs "[{\"s3Config\":{\"bucketArn\":\"arn:aws:s3:::${BUCKET_NAME}\",\"prefix\":\"${NAME}-campaign-s3-${S3_SUFFIX}\",\"dataFormat\":\"JSON\",\"storageCompressionFormat\":\"NONE\"}}]"| jq -r .arn

    approve_campaign ${NAME}-campaign-s3-json

    echo "Creating campaign from ${CAMPAIGN_FILE} for S3..."
    CAMPAIGN=`cat ${CAMPAIGN_FILE} \
        | jq .name=\"${NAME}-campaign-s3-parquet\" \
        | jq .signalCatalogArn=\"${SIGNAL_CATALOG_ARN}\" \
        | jq .targetArn=\"${FLEET_ARN}\"`
    aws iotfleetwise create-campaign \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --cli-input-json "${CAMPAIGN}" --data-destination-configs "[{\"s3Config\":{\"bucketArn\":\"arn:aws:s3:::${BUCKET_NAME}\",\"prefix\":\"${NAME}-campaign-s3-${S3_SUFFIX}\",\"dataFormat\":\"PARQUET\",\"storageCompressionFormat\":\"NONE\"}}]"| jq -r .arn

    approve_campaign ${NAME}-campaign-s3-parquet
fi

# The following two actions(Suspending, Resuming) are only for demo purpose, it won't affect the campaign status
sleep 2
echo "Suspending campaign..."
aws iotfleetwise update-campaign \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-campaign \
    --action SUSPEND | jq -r .arn

sleep 2
echo "Resuming campaign..."
aws iotfleetwise update-campaign \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-campaign \
    --action RESUME | jq -r .arn

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

for ((i=0; i<${FLEET_SIZE}; i+=${BATCH_SIZE})); do
    for ((j=0; j<${BATCH_SIZE} && i+j<${FLEET_SIZE}; j++)); do
        VEHICLE=${VEHICLES[$((i+j))]}
        echo "Waiting until status of vehicle ${VEHICLE} is healthy..."
        # This output group is run in a background process. Note that stderr is redirected to stream 3 and back,
        # to print stderr from the output group, but not info about the background process.
        { \
            check_vehicle_healthy "${VEHICLE}" \
        2>&3 &} 3>&2 2>/dev/null
    done
    # Wait for all background processes to finish
    wait
done

DELAY=30
echo "Waiting ${DELAY} seconds for data to be collected..."
sleep ${DELAY}

echo "The DB Name is ${TIMESTREAM_DB_NAME}"
echo "The DB Table is ${TIMESTREAM_TABLE_NAME}"

COLLECTED_DATA_DIR="collected-data-${DISAMBIGUATOR}/"
mkdir -p ${COLLECTED_DATA_DIR}

for VEHICLE in ${VEHICLES[@]}; do
    echo "Querying Timestream for vehicle ${VEHICLE}..."
    aws timestream-query query \
        --region ${REGION} \
        --query-string "SELECT * FROM \"${TIMESTREAM_DB_NAME}\".\"${TIMESTREAM_TABLE_NAME}\" \
            WHERE vehicleName = '${VEHICLE}' \
            AND time between ago(1m) and now() ORDER BY time ASC" \
        > ${COLLECTED_DATA_DIR}${VEHICLE}-timestream-result.json
done

if [ "${DBC_FILE}" != "" ]; then
    echo "----------------------------------------------------------------------------------------"
    echo "| Note: A custom DBC file was used. If an error occurs below due to no collected data, |"
    echo "|       you need to modify cansim.py or run FWE with a real vehicle.                   |"
    echo "----------------------------------------------------------------------------------------"
fi

OUTPUT_FILES=()
for VEHICLE in ${VEHICLES[@]}; do
    echo "Converting to HTML..."
    OUTPUT_FILE_HTML="${COLLECTED_DATA_DIR}${VEHICLE}.html"
    OUTPUT_FILES+=(${OUTPUT_FILE_HTML})
    python3 timestream-to-html.py \
        ${COLLECTED_DATA_DIR}${VEHICLE}-timestream-result.json \
        ${OUTPUT_FILE_HTML}
done

echo "You can now view the collected data."
echo "----------------------------------"
echo "| Collected data in HTML format: |"
echo "----------------------------------"
for FILE in ${OUTPUT_FILES[@]}; do
    echo `realpath ${FILE}`
done

if [ ${S3_UPLOAD} == true ]; then
    DELAY=1200
    echo "Waiting 20 minutes for data to be collected and uploaded to S3..."
    sleep ${DELAY}

    echo "Downloading files from S3..."
    S3_URL="s3://${BUCKET_NAME}/${NAME}-campaign-s3-${S3_SUFFIX}/processed-data/"
    if ! COLLECTED_FILES=`aws s3 ls --recursive ${S3_URL}`; then
        echo "Error: no collected data was found at ${S3_URL}"
        exit -1
    fi
    echo "${COLLECTED_FILES}" | while read LINE; do
        KEY=`echo ${LINE} | cut -d ' ' -f4`
        aws s3 cp s3://${BUCKET_NAME}/${KEY} ${COLLECTED_DATA_DIR}
    done

    OUTPUT_FILES=()
    for VEHICLE in ${VEHICLES[@]}; do
        echo "Converting from JSON to HTML..."
        OUTPUT_FILE_HTML="${COLLECTED_DATA_DIR}${VEHICLE}-json.html"
        OUTPUT_FILES+=(${OUTPUT_FILE_HTML})
        python3 firehose-to-html.py \
            --vehicle-name ${VEHICLE} \
            --files ${COLLECTED_DATA_DIR}part-*.json \
            --html-filename ${OUTPUT_FILE_HTML}

        echo "Converting from Parquet to HTML..."
        OUTPUT_FILE_HTML="${COLLECTED_DATA_DIR}${VEHICLE}-parquet.html"
        OUTPUT_FILES+=(${OUTPUT_FILE_HTML})
        python3 firehose-to-html.py \
            --vehicle-name ${VEHICLE} \
            --files ${COLLECTED_DATA_DIR}part-*.parquet \
            --html-filename ${OUTPUT_FILE_HTML}
    done

    echo "You can now view the collected data."
    echo "----------------------------------"
    echo "| Collected data in HTML format: |"
    echo "----------------------------------"
    for FILE in ${OUTPUT_FILES[@]}; do
        echo `realpath ${FILE}`
    done
fi
