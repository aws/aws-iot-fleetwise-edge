#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
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
TIMESTREAM_DB_NAME=""
TIMESTREAM_TABLE_NAME="VehicleDataTable"
SERVICE_ROLE="IoTFleetWiseServiceRole"
SERVICE_ROLE_POLICY_ARN=""
SERVICE_PRINCIPAL="iotfleetwise.amazonaws.com"
BUCKET_NAME=""
SKIP_S3_POLICY=true
CAMPAIGN_FILES=()
NODE_FILES=()
DECODER_FILES=()
NETWORK_INTERFACE_FILES=()
S3_FORMAT="JSON"
SKIP_ACCOUNT_REGISTRATION=false
CLEAN_UP=false
FLEET_SIZE=1
BATCH_SIZE=$((`nproc`*4))
HEALTH_CHECK_RETRIES=360 # About 30mins
MAX_ATTEMPTS_ON_REGISTRATION_FAILURE=5
FORCE_REGISTRATION=false
MIN_CLI_VERSION="2.22.3"
CREATED_SIGNAL_CATALOG_NAME=""
CREATED_S3_BUCKET=""
CREATED_CAMPAIGN_NAMES=""
INCLUDE_SIGNALS=""
EXCLUDE_SIGNALS=""
DATA_DESTINATION="S3"
IOT_TOPIC="iotfleetwise-data-${DISAMBIGUATOR}"

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
        --data-destination)
            DATA_DESTINATION=$2
            shift
            ;;
        --campaign-file)
            CAMPAIGN_FILES+=("$2")
            shift
            ;;
        --s3-format)
            S3_FORMAT=$2
            shift
            ;;
        --node-file)
            NODE_FILES+=("$2")
            shift
            ;;
        --decoder-file)
            DECODER_FILES+=("$2")
            shift
            ;;
        --network-interface-file)
            NETWORK_INTERFACE_FILES+=("$2")
            shift
            ;;
        --bucket-name)
            BUCKET_NAME=$2
            shift
            ;;
        --set-bucket-policy)
            SKIP_S3_POLICY=false
            ;;
        --skip-account-registration)
            SKIP_ACCOUNT_REGISTRATION=true
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
        --include-signals)
            INCLUDE_SIGNALS="$2"
            shift
            ;;
        --exclude-signals)
            EXCLUDE_SIGNALS="$2"
            shift
            ;;
        --iot-topic)
            IOT_TOPIC="$2"
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --vehicle-name <NAME>             Vehicle name"
            echo "  --fleet-size <SIZE>               Size of fleet, default: ${FLEET_SIZE}. When greater than 1,"
            echo "                                    the instance number will be appended to each"
            echo "                                    Vehicle name after a '-', e.g. ${DEFAULT_VEHICLE_NAME}-42"
            echo "  --node-file <FILE>                Node JSON file. Can be used multiple times for multiple files."
            echo "  --decoder-file <FILE>             Decoder JSON file. Can be used multiple times for multiple files."
            echo "  --network-interface-file <FILE>   Network interface JSON file. Can be used multiple times for multiple files."
            echo "  --campaign-file <FILE>            Campaign JSON file. Can be used multiple times for multiple files."
            echo "  --data-destination <DESTINATION>  Data destination, either S3, TIMESTREAM or IOT_TOPIC, default: ${DATA_DESTINATION}."
            echo "                                    NOTE: TIMESTREAM can only be used if your account is already onboarded in that region."
            echo "                                    See https://docs.aws.amazon.com/timestream/latest/developerguide/AmazonTimestreamForLiveAnalytics-availability-change.html"
            echo "  --s3-format <FORMAT>              Either JSON or PARQUET, default: ${S3_FORMAT}"
            echo "  --bucket-name <NAME>              S3 bucket name, if not specified a new bucket will be created"
            echo "  --set-bucket-policy               Sets the required bucket policy"
            echo "  --clean-up                        Delete created resources"
            echo "  --skip-account-registration       Don't check account registration nor try to register it. Most features should work without registration."
            echo "  --include-signals <CSV>           Comma separated list of signals to include in HTML plot"
            echo "  --exclude-signals <CSV>           Comma separated list of signals to exclude from HTML plot"
            echo "  --endpoint-url <URL>              The endpoint URL used for AWS CLI calls"
            echo "  --service-principal <PRINCIPAL>   AWS service principal for policies, default: ${SERVICE_PRINCIPAL}"
            echo "  --region <REGION>                 The region used for AWS CLI calls, default: ${REGION}"
            echo "  --iot-topic <TOPIC>               The IoT topic to publish vehicle data to, default: ${IOT_TOPIC} when --data-destination IOT_TOPIC is used"
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
echo "Fleet size: ${FLEET_SIZE}"
echo "Vehicles: ${VEHICLES[@]}"

# AWS CLI v1.x has a double base64 encoding issue
echo "Checking AWS CLI version..."
CLI_VERSION=`aws --version | sed -nE 's#^aws-cli/([0-9]+\.[0-9]+\.[0-9]+).*$#\1#p'`
echo "${CLI_VERSION}"
CLI_VERSION_OK=`python3 -c "from packaging.version import Version;print(Version('${CLI_VERSION}') >= Version('${MIN_CLI_VERSION}'))" 2> /dev/null`
if [ "${CLI_VERSION_OK}" != "True" ]; then
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
CREATED_S3_BUCKET=${CREATED_S3_BUCKET}
CAMPAIGN_NAMES=${CREATED_CAMPAIGN_NAMES}
BUCKET_NAME=${BUCKET_NAME}
DATA_DESTINATION=${DATA_DESTINATION}
S3_SUFFIX=${S3_SUFFIX}
INCLUDE_SIGNALS=\"${INCLUDE_SIGNALS}\"
EXCLUDE_SIGNALS=\"${EXCLUDE_SIGNALS}\"
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

# $1 is the vehicle name , $2 is the expected campaign name
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
            # If the campaign was found
            elif [ "${CAMPAIGN_NAME}" == "$2" ]; then
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

trap cleanup EXIT

echo "Getting AWS account ID..."
echo ${ACCOUNT_ID}

if ! ${SKIP_ACCOUNT_REGISTRATION}; then
    echo "Getting account registration status..."
    if REGISTER_ACCOUNT_STATUS=`get_account_status 2>&1`; then
        ACCOUNT_STATUS=`echo "${REGISTER_ACCOUNT_STATUS}" | jq -r .accountStatus`
    elif ! echo ${REGISTER_ACCOUNT_STATUS} | grep -q "ResourceNotFoundException"; then
        echo "${REGISTER_ACCOUNT_STATUS}" >&2
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
fi

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

if [ ${#CAMPAIGN_FILES[@]} -eq 0 ]; then
    echo "No campaign file(s) provided, so no data destination to setup"
elif [ "${DATA_DESTINATION}" == "S3" ]; then
    echo "S3 upload is enabled"
    if [ "${BUCKET_NAME}" == "" ]; then
        CREATED_S3_BUCKET="iot-fleetwise-demo-${S3_SUFFIX}"
        BUCKET_NAME=${CREATED_S3_BUCKET}
        SKIP_S3_POLICY=false
    fi
    echo "Checking if S3 bucket exists..."

    BUCKET_LIST=$( aws s3 ls )
    if grep -q "$BUCKET_NAME" <<< "$BUCKET_LIST"; then
        echo "S3 bucket already exists"
    else
        echo "Creating S3 bucket..."
        aws s3 mb s3://$BUCKET_NAME --region $REGION
        SKIP_S3_POLICY=false
    fi
    if ! ${SKIP_S3_POLICY}; then
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
elif [ "${DATA_DESTINATION}" == "TIMESTREAM" ]; then # Timestream
    TIMESTREAM_DB_NAME="IoTFleetWiseDB-${DISAMBIGUATOR}"
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

    SERVICE_ROLE_ARN=`${SCRIPT_DIR}/manage-service-role.sh \
        --service-role ${SERVICE_ROLE} \
        --service-principal ${SERVICE_PRINCIPAL} \
        --actions "timestream:WriteRecords,timestream:Select,timestream:DescribeTable" \
        --resources "arn:aws:timestream:${REGION}:${ACCOUNT_ID}:database/${TIMESTREAM_DB_NAME}/*" \
        --actions "timestream:DescribeEndpoints" \
        --resources "*"`
elif [ "${DATA_DESTINATION}" == "IOT_TOPIC" ]; then
    TOPIC_ARN=arn:aws:iot:${REGION}:${ACCOUNT_ID}:topic/${IOT_TOPIC}
    echo "IoT topic destination: ${TOPIC_ARN}"
    SERVICE_ROLE_ARN=`${SCRIPT_DIR}/manage-service-role.sh \
        --service-role ${SERVICE_ROLE} \
        --service-principal ${SERVICE_PRINCIPAL} \
        --actions "iot:Publish" \
        --resources ${TOPIC_ARN}`
else
    echo "Error: Unknown data destination ${DATA_DESTINATION}"
    exit -1
fi
VEHICLE_NODE=`cat ${SCRIPT_DIR}/vehicle-node.json`

if SIGNAL_CATALOG_ARN=`aws iotfleetwise create-signal-catalog \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-signal-catalog \
    --nodes "${VEHICLE_NODE}" 2>/dev/null | jq -r .arn`; then

    echo "Created new signal catalog: ${SIGNAL_CATALOG_ARN}"
    SIGNAL_CATALOG_NAME="${NAME}-signal-catalog"
    CREATED_SIGNAL_CATALOG_NAME="${SIGNAL_CATALOG_NAME}"
else
    echo "Checking for existing signal catalogs..."
    SIGNAL_CATALOG_LIST=`aws iotfleetwise list-signal-catalogs \
        ${ENDPOINT_URL_OPTION} --region ${REGION}`
    SIGNAL_CATALOG_NAME=`echo ${SIGNAL_CATALOG_LIST} | jq -r .summaries[0].name`
    SIGNAL_CATALOG_ARN=`echo ${SIGNAL_CATALOG_LIST} | jq -r .summaries[0].arn`
    echo "Reusing existing signal catalog: ${SIGNAL_CATALOG_ARN}"
fi

if [ ${#NODE_FILES[@]} -eq 0 ]; then
    echo "No node file(s) provided, so finishing now"
    exit 0
fi
ALL_NODES="[]"
for NODE_FILE in "${NODE_FILES[@]}"; do
    echo "Updating nodes in signal catalog from ${NODE_FILE}"
    NODES=`cat ${NODE_FILE}`
    ALL_NODES=`echo "${ALL_NODES}" | jq ".+=${NODES}"`
    NODE_COUNT=`echo $NODES | jq length`
    for ((i=0; i<${NODE_COUNT}; i+=500)); do
        NODES_SUBSET=`echo $NODES | jq .[$i:$(($i+500))]`
        if UPDATE_SIGNAL_CATALOG_STATUS=`aws iotfleetwise update-signal-catalog \
            ${ENDPOINT_URL_OPTION} --region ${REGION} \
            --name ${SIGNAL_CATALOG_NAME} \
            --nodes-to-update "${NODES_SUBSET}" 2>&1`; then
            echo ${UPDATE_SIGNAL_CATALOG_STATUS} | jq -r .arn
        elif ! echo ${UPDATE_SIGNAL_CATALOG_STATUS} | grep -q "InvalidSignalsException"; then
            echo ${UPDATE_SIGNAL_CATALOG_STATUS} >&2
            exit -1
        else
            echo "Signals exist and are in use, continuing"
        fi
    done
done

echo "Creating model manifest..."
aws iotfleetwise create-model-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-model-manifest \
    --signal-catalog-arn ${SIGNAL_CATALOG_ARN} \
    --nodes "[]" | jq -r .arn

# Make a list of all node names:
NODE_LIST=`echo "${ALL_NODES}" | jq -r ".[] | .actuator,.sensor | .fullyQualifiedName" | grep Vehicle\\. | jq -Rn [inputs]`
NODE_COUNT=`echo ${NODE_LIST} | jq length`

echo "Updating model manifest with ${NODE_COUNT} nodes in batches..."
for ((i=0; i<${NODE_COUNT}; i+=499)); do
    NODES_SUBSET=`echo ${NODE_LIST} | jq .[$i:$(($i+499))]`
    echo "Processing batch starting at index $i..."
    while true; do
        if UPDATE_MODEL_MANIFEST_STATUS=`aws iotfleetwise update-model-manifest \
            ${ENDPOINT_URL_OPTION} --region ${REGION} \
            --name ${NAME}-model-manifest \
            --nodes-to-add "${NODES_SUBSET}"`; then
            echo ${UPDATE_MODEL_MANIFEST_STATUS} | jq -r .arn
            break
        elif ! echo ${UPDATE_MODEL_MANIFEST_STATUS} | grep -q "ConflictException"; then
            echo "${UPDATE_MODEL_MANIFEST_STATUS}" >&2
            exit -1
        else
            DELAY=5
            echo "Signal catalog in use, waiting for ${DELAY} seconds and retrying..."
            sleep ${DELAY}
        fi
    done
done

echo "Activating model manifest..."
while true; do
    if UPDATE_MODEL_MANIFEST_STATUS=`aws iotfleetwise update-model-manifest \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${NAME}-model-manifest \
        --status ACTIVE`; then
        MODEL_MANIFEST_ARN=`echo ${UPDATE_MODEL_MANIFEST_STATUS} | jq -r .arn`
        echo ${MODEL_MANIFEST_ARN}
        break
    elif ! echo ${UPDATE_MODEL_MANIFEST_STATUS} | grep -q "ConflictException"; then
        echo "${UPDATE_MODEL_MANIFEST_STATUS}" >&2
        exit -1
    else
        DELAY=5
        echo "Signal catalog in use, waiting for ${DELAY} seconds and retrying..."
        sleep ${DELAY}
    fi
done

if [ ${#NETWORK_INTERFACE_FILES[@]} -eq 0 ]; then
    echo "No network interface file(s) provided, so finishing now"
    exit 0
fi
NETWORK_INTERFACES="[]"
for NETWORK_INTERFACE_FILE in "${NETWORK_INTERFACE_FILES[@]}"; do
    echo "Reading network interfaces from ${NETWORK_INTERFACE_FILE}"
    NEXT_NETWORK_INTERFACE=`cat ${NETWORK_INTERFACE_FILE}`
    NETWORK_INTERFACES=`echo "${NETWORK_INTERFACES}" | jq ".+=${NEXT_NETWORK_INTERFACE}"`
done
echo "Creating decoder manifest with network interfaces..."
DECODER_MANIFEST_ARN=`aws iotfleetwise create-decoder-manifest \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --name ${NAME}-decoder-manifest \
    --model-manifest-arn ${MODEL_MANIFEST_ARN} \
    --network-interfaces "${NETWORK_INTERFACES}" | jq -r .arn`
echo ${DECODER_MANIFEST_ARN}

if [ ${#DECODER_FILES[@]} -eq 0 ]; then
    echo "No decoder file(s) provided, so finishing now"
    exit 0
fi
for DECODER_FILE in "${DECODER_FILES[@]}"; do
    echo "Updating decoders in decoder manifest from ${DECODER_FILE}..."
    DECODERS=`cat ${DECODER_FILE}`
    DECODER_COUNT=`echo $DECODERS | jq length`
    for ((i=0; i<${DECODER_COUNT}; i+=200)); do
        DECODER_SUBSET=`echo $DECODERS | jq .[$i:$(($i+200))]`
        aws iotfleetwise update-decoder-manifest \
            ${ENDPOINT_URL_OPTION} --region ${REGION} \
            --name ${NAME}-decoder-manifest \
            --signal-decoders-to-add "${DECODER_SUBSET}" | jq -r .arn
    done
done

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

CAMPAIGN_COUNTER=0
if [ ${#CAMPAIGN_FILES[@]} -eq 0 ]; then
    echo "No campaign file(s) provided, so finishing now."
else
    for CAMPAIGN_FILE in "${CAMPAIGN_FILES[@]}"; do
        CAMPAIGN_COUNTER=$((${CAMPAIGN_COUNTER}+1))
        CAMPAIGN_NAME=${NAME}-campaign-${CAMPAIGN_COUNTER}
        echo "Creating campaign from ${CAMPAIGN_FILE} as ${CAMPAIGN_NAME}"
        CAMPAIGN=`cat ${CAMPAIGN_FILE} \
            | jq .name=\"${CAMPAIGN_NAME}\" \
            | jq .signalCatalogArn=\"${SIGNAL_CATALOG_ARN}\" \
            | jq .targetArn=\"${FLEET_ARN}\"`
        if [ "${DATA_DESTINATION}" == "S3" ]; then
            echo "(Data destination is S3 with format ${S3_FORMAT})"
            CAMPAIGN=`echo "${CAMPAIGN}" \
                | jq ".dataDestinationConfigs=[{\"s3Config\":{\"bucketArn\":\"arn:aws:s3:::${BUCKET_NAME}\",\"prefix\":\"iot-fleetwise-demo-${DISAMBIGUATOR}-s3-${S3_SUFFIX}\",\"dataFormat\":\"${S3_FORMAT}\",\"storageCompressionFormat\":\"NONE\"}}]"`
        elif [ "${DATA_DESTINATION}" == "TIMESTREAM" ]; then
            CAMPAIGN=`echo "${CAMPAIGN}" \
                | jq ".dataDestinationConfigs=[{\"timestreamConfig\":{\"timestreamTableArn\":\"${TIMESTREAM_TABLE_ARN}\",\"executionRoleArn\":\"${SERVICE_ROLE_ARN}\"}}]"`
        elif [ "${DATA_DESTINATION}" == "IOT_TOPIC" ]; then
            CAMPAIGN=`echo "${CAMPAIGN}" \
                | jq ".dataDestinationConfigs=[{\"mqttTopicConfig\":{\"mqttTopicArn\":\"${TOPIC_ARN}\",\"executionRoleArn\":\"${SERVICE_ROLE_ARN}\"}}]"`
        else
            echo "Error: Unknown data destination ${DATA_DESTINATION}"
            exit -1
        fi
        aws iotfleetwise create-campaign \
            ${ENDPOINT_URL_OPTION} --region ${REGION} \
            --cli-input-json "${CAMPAIGN}" | jq -r .arn
        if (( CAMPAIGN_COUNTER > 1 )); then
            CREATED_CAMPAIGN_NAMES+=","
        fi
        CREATED_CAMPAIGN_NAMES+=$CAMPAIGN_NAME

        approve_campaign ${CAMPAIGN_NAME}

        for ((i=0; i<${FLEET_SIZE}; i+=${BATCH_SIZE})); do
            for ((j=0; j<${BATCH_SIZE} && i+j<${FLEET_SIZE}; j++)); do
                VEHICLE=${VEHICLES[$((i+j))]}
                echo "Waiting until the status of ${CAMPAIGN_NAME} on vehicle ${VEHICLE} is healthy..."
                # This output group is run in a background process. Note that stderr is redirected to stream 3 and back,
                # to print stderr from the output group, but not info about the background process.
                { \
                    check_vehicle_healthy "${VEHICLE}" "${CAMPAIGN_NAME}"\
                2>&3 &} 3>&2 2>/dev/null
            done
            # Wait for all background processes to finish
            wait
        done
    done

    COLLECTED_DATA_DIR="collected-data-${DISAMBIGUATOR}/"
    mkdir -p ${COLLECTED_DATA_DIR}

    if [ "${DATA_DESTINATION}" == "S3" ]; then
        DELAY=1200
        SLEEP_TIME=300
        echo "Waiting `expr ${DELAY} / 60` minutes for data to be collected and uploaded to S3..."
        for ((i=DELAY; i>0; i-=SLEEP_TIME)); do
            sleep $((i > SLEEP_TIME ? SLEEP_TIME : i))
            echo "..." # Print something to prevent the terminal timing out
        done

        echo "Downloading files from S3..."
        S3_URL="s3://${BUCKET_NAME}/iot-fleetwise-demo-${DISAMBIGUATOR}-s3-${S3_SUFFIX}/processed-data/"
        if ! COLLECTED_FILES=`aws s3 ls --recursive ${S3_URL}`; then
            echo "Error: no collected data was found at ${S3_URL}"
            exit -1
        fi
        echo "${COLLECTED_FILES}" | while read LINE; do
            KEY=`echo ${LINE} | cut -d ' ' -f4`
            aws s3 cp s3://${BUCKET_NAME}/${KEY} ${COLLECTED_DATA_DIR}
        done

        for VEHICLE in ${VEHICLES[@]}; do
            echo "Converting from Firehose ${S3_FORMAT} to HTML..."
            OUTPUT_FILE_HTML="${COLLECTED_DATA_DIR}${VEHICLE}.html"
            OUTPUT_FILE_S3_LINKS="${COLLECTED_DATA_DIR}${VEHICLE}-s3-links.txt"
            python3 ${SCRIPT_DIR}/firehose-to-html.py \
                --vehicle-name ${VEHICLE} \
                --files ${COLLECTED_DATA_DIR}part-*.* \
                --html-filename ${OUTPUT_FILE_HTML} \
                --s3-links-filename ${OUTPUT_FILE_S3_LINKS} \
                --include-signals "${INCLUDE_SIGNALS}" \
                --exclude-signals "${EXCLUDE_SIGNALS}"

            if [ -s ${OUTPUT_FILE_S3_LINKS} ]; then
                echo "Downloading the first 10 linked files..."
                i=0
                cat ${OUTPUT_FILE_S3_LINKS} | while read LINE; do
                    if ((i < 10)); then
                    IMAGE_FILE=`basename ${LINE}.jpg`
                    # Remove random prefix so that filenames begin with date
                    IMAGE_FILE=`echo ${IMAGE_FILE} | sed -E 's/^[0-9a-f]{8}\-[0-9a-f]{4}\-[0-9a-f]{4}\-[0-9a-f]{4}\-[0-9a-f]{12}\-//'`
                        aws s3 cp ${LINE} ${COLLECTED_DATA_DIR}${IMAGE_FILE}
                    fi
                    i=$((i+1))
                done
            fi
        done

        echo "Zipping up collected data..."
        zip ${COLLECTED_DATA_DIR}${NAME}.zip ${COLLECTED_DATA_DIR}*

        echo "You can now view the collected data."
        echo "----------------------------------------------------"
        echo "| Collected S3 data for all campaigns for ${NAME}: |"
        echo "----------------------------------------------------"
        echo `realpath ${COLLECTED_DATA_DIR}${NAME}.zip`
    elif [ "${DATA_DESTINATION}" == "TIMESTREAM" ]; then # Data destination is Timestream
        DELAY=30
        echo "Waiting ${DELAY} seconds for data to be collected..."
        sleep ${DELAY}

        echo "The DB Name is ${TIMESTREAM_DB_NAME}"
        echo "The DB Table is ${TIMESTREAM_TABLE_NAME}"

        OUTPUT_FILES=()

        for VEHICLE in ${VEHICLES[@]}; do
            SQL_QUERY="SELECT * FROM \"${TIMESTREAM_DB_NAME}\".\"${TIMESTREAM_TABLE_NAME}\" WHERE vehicleName='${VEHICLE}' AND time between ago(1m) and now() ORDER BY time ASC"
            echo "Querying Timestream for vehicle ${VEHICLE}..."
            echo "${SQL_QUERY}"
            TIMESTREAM_JSON_FILE="${COLLECTED_DATA_DIR}${VEHICLE}-timestream-result.json"
            if aws timestream-query query \
                --region ${REGION} \
                --query-string "${SQL_QUERY}" \
                > ${TIMESTREAM_JSON_FILE}; then
                echo "Saved timestream results to ${TIMESTREAM_JSON_FILE}"
                OUTPUT_FILE_HTML="${COLLECTED_DATA_DIR}${VEHICLE}.html"
                OUTPUT_FILES+=(${OUTPUT_FILE_HTML})
                echo "Converting from Timestream JSON to HTML..."
                python3 ${SCRIPT_DIR}/timestream-to-html.py \
                    --vehicle-name ${VEHICLE} \
                    --files ${TIMESTREAM_JSON_FILE} \
                    --html-filename ${OUTPUT_FILE_HTML} \
                    --include-signals "${INCLUDE_SIGNALS}" \
                    --exclude-signals "${EXCLUDE_SIGNALS}"
            else
                echo "WARNING: Could not save timestream results for ${VEHICLE}, was any data collected?"
            fi
        done

        if [ ${#OUTPUT_FILES[@]} -eq 0 ]; then
            echo "Error: No output files saved, was any data collected on any vehicles?"
            exit -1
        else
            echo "You can now view the collected data."
            echo "----------------------------------------------------------------"
            echo "| Collected data for all campaigns for ${NAME} in HTML format: |"
            echo "----------------------------------------------------------------"
            for FILE in ${OUTPUT_FILES[@]}; do
                echo `realpath ${FILE}`
            done
        fi
    elif [ "${DATA_DESTINATION}" == "IOT_TOPIC" ]; then
        echo "Publishing to ${IOT_TOPIC}"

        OUTPUT_FILES=()

        for VEHICLE in ${VEHICLES[@]}; do
            TOPIC_NAME=${IOT_TOPIC}
            TOPIC_JSON_FILE="${COLLECTED_DATA_DIR}${VEHICLE}-iot-topic-result.json"
            SUBSCRIBE_TIME=30
            echo "Subscribing to IoT topic '${TOPIC_NAME}' for vehicle ${VEHICLE} for ${SUBSCRIBE_TIME} seconds.."
            if python3 ${SCRIPT_DIR}/iot-topic-subscribe.py \
                --client-id ${VEHICLE}-subscriber \
                --region ${REGION} \
                --output-file ${TOPIC_JSON_FILE} \
                --vehicle-name ${VEHICLE} \
                --iot-topic ${IOT_TOPIC} \
                --run-time ${SUBSCRIBE_TIME}; then
                echo "Saved IoT topic results to ${TOPIC_JSON_FILE}"
                OUTPUT_FILE_HTML="${COLLECTED_DATA_DIR}${VEHICLE}.html"
                OUTPUT_FILES+=(${OUTPUT_FILE_HTML})
                echo "Converting from IoT topic JSON to HTML..."
                python3 ${SCRIPT_DIR}/iot-topic-to-html.py \
                    --vehicle-name ${VEHICLE} \
                    --files ${TOPIC_JSON_FILE} \
                    --html-filename ${OUTPUT_FILE_HTML} \
                    --include-signals "${INCLUDE_SIGNALS}" \
                    --exclude-signals "${EXCLUDE_SIGNALS}"
            else
                echo "WARNING: Could not save IoT topic results for ${VEHICLE}, was any data collected?"
            fi
        done

        if [ ${#OUTPUT_FILES[@]} -eq 0 ]; then
            echo "Error: No output files saved, was any data collected on any vehicles?"
            exit -1
        else
            echo "You can now view the collected data."
            echo "----------------------------------------------------------------"
            echo "| Collected data for all campaigns for ${NAME} in HTML format: |"
            echo "----------------------------------------------------------------"
            for FILE in ${OUTPUT_FILES[@]}; do
                echo `realpath ${FILE}`
            done
        fi
    else
        echo "Error: Unknown data destination ${DATA_DESTINATION}"
        exit -1
    fi
fi
