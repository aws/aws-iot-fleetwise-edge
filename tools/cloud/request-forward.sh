#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
ACCOUNT_ID=`aws sts get-caller-identity --query "Account" --output text`
DISAMBIGUATOR=""
ENDPOINT_URL_OPTION=""
REGION="us-east-1"
VEHICLE_NAME=""
BUCKET_NAME=""
S3_UPLOAD=false
S3_SUFFIX=""
CAMPAIGN_NAME=""
END_TIME=""
END_TIME_ARGUMENT=""
FLEET_SIZE=1
INCLUDE_SIGNALS=""
EXCLUDE_SIGNALS=""
TIMESTREAM_DB_NAME=""
JOB_STATUS_CHECK_RETRIES=360 # About 30mins

if [ -f demo.env ]; then
    source demo.env
fi

# The demo script uses an iotfleetwise endpoint, this script uses an iot endpoint.
# So we discard any ENDPOINT_URL from demo.env.
unset ENDPOINT_URL
ENDPOINT_URL=""

# If multiple campaigns were used in demo.sh, read just the first one
read CAMPAIGN_NAME __ <<< $(echo $CAMPAIGN_NAMES | tr "," " ")

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
        --endpoint-url)
            ENDPOINT_URL=$2
            shift
            ;;
        --enable-s3-upload)
            S3_UPLOAD=true
            ;;
        --s3-suffix)
            S3_SUFFIX=$2
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
        --timestream-db-name)
            TIMESTREAM_DB_NAME="$2"
            shift
            ;;
        --timestream-table-name)
            TIMESTREAM_TABLE_NAME="$2"
            shift
            ;;
        --disambiguator)
            DISAMBIGUATOR=$2
            shift
            ;;
        --bucket-name)
            BUCKET_NAME=$2
            shift
            ;;
        --region)
            REGION=$2
            shift
            ;;
        --campaign-name)
            CAMPAIGN_NAME=$2
            shift
            ;;
         --end-time)
            END_TIME=$2
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --vehicle-name <NAME>          The vehicle name used in the fleet"
            echo "  --fleet-size <SIZE>            Size of fleet, default: ${FLEET_SIZE}. When greater than 1,"
            echo "                                 the instance number will be appended to each"
            echo "                                 Vehicle name after a '-', e.g. fwdemo-42"
            echo "  --disambiguator <STRING>       The unique string used by the demo.sh script to avoid resource name conflicts."
            echo "                                 Used to retrieve data after job execution."
            echo "  --enable-s3-upload             Use demo data uploaded to S3 rather than Amazon Timestream"
            echo "  --s3-suffix <SUFFIX>           The suffix of the bucket arn where the demo data is stored in S3"
            echo "  --include-signals <CSV>        Comma separated list of signals to include in HTML plot"
            echo "  --exclude-signals <CSV>        Comma separated list of signals to exclude from HTML plot"
            echo "  --timestream-db-name <NAME>    The name of the timestream database where the demo data is stored"
            echo "  --timestream-table-name <NAME> The name of the timestream table where the demo data is stored"
            echo "  --bucket-name <STRING>         The name of the bucket created by the demo script where data should be forwarded to."
            echo "                                 Used to retrieve data after job execution."
            echo "  --campaign-name <NAME>         The campaign name for which data forward will be requested."
            echo "                                 If none is provided, defaults to the first campaign from demo.sh"
            echo "  --end-time <TIME>              A point in time,"
            echo "                                 where data collected after this point in time will not be forwarded."
            echo "                                 As an ISO 8601 UTC formatted time string, e.g. 2024-05-25T01:21:23Z"
            echo "  --endpoint-url <URL>           The endpoint URL used for AWS CLI calls"
            echo "  --region <REGION>              The region used for AWS CLI calls, default: ${REGION}"
            exit 0
            ;;
        esac
        shift
    done

    if [ "${VEHICLE_NAME}" == "" ]; then
        echo "Error: Vehicle name not provided"
        exit -1
    fi
    if [ "${CAMPAIGN_NAME}" == "" ]; then
        echo "Error: Campaign name not provided"
        exit -1
    fi
    if [ "${DISAMBIGUATOR}" == "" ]; then
        echo "Error: Disambiguator not provided"
        exit -1
    fi
    if [ $S3_UPLOAD == true ] && [ "${BUCKET_NAME}" == "" ]; then
        echo "Error: Bucket name not provided"
        exit -1
    fi
}

echo "========================================"
echo "AWS IoT FleetWise Request Forward Script"
echo "========================================"

parse_args "$@"

if [ "${END_TIME}" != "" ]; then
    END_TIME_ARGUMENT=",\"endTime\": \"${END_TIME}\""
fi

if [ "${ENDPOINT_URL}" != "" ]; then
    ENDPOINT_URL_OPTION="--endpoint-url ${ENDPOINT_URL}"
fi

if [ ${FLEET_SIZE} -gt 1 ]; then
    VEHICLES=( $(seq -f "${VEHICLE_NAME}-%g" 0 $((${FLEET_SIZE}-1))) )
else
    VEHICLES=( ${VEHICLE_NAME} )
fi

declare -a QUOTED_VEHICLE_THING_ARNS=()
for i in $(seq 0 $((${#VEHICLES[@]} - 1)));
do
    QUOTED_VEHICLE_THING_ARNS[$i]="\"arn:aws:iot:${REGION}:${ACCOUNT_ID}:thing/${VEHICLES[$i]}\""
done

NAME="${VEHICLE_NAME}-${DISAMBIGUATOR}"

UUID="$(cat /proc/sys/kernel/random/uuid)"

DOCUMENT_STRING=$(cat << EOF
'{
    "version": "1.0",
    "parameters": {
        "campaignArn": "arn:aws:iotfleetwise:${REGION}:${ACCOUNT_ID}:campaign/${CAMPAIGN_NAME}"
        $END_TIME_ARGUMENT  }
}'
EOF
)

echo -n "Date: "
date --rfc-3339=seconds

echo "Vehicle name: ${VEHICLE_NAME}"
echo "Fleet size: ${FLEET_SIZE}"
echo "Vehicles: ${VEHICLES[@]}"
echo "Job ID: ${UUID}"
echo "Job Document: ${DOCUMENT_STRING}"


echo "Creating job to pull data for $CAMPAIGN_NAME, targeting the whole fleet"

echo "Executing the following command: \\
aws iot create-job \\
    ${ENDPOINT_URL_OPTION} --region ${REGION} \\
    --job-id ${UUID} \\
    --targets ${QUOTED_VEHICLE_THING_ARNS[@]} \\
    --document ${DOCUMENT_STRING}"

eval aws iot create-job \
    ${ENDPOINT_URL_OPTION} --region ${REGION} \
    --job-id ${UUID} \
    --targets ${QUOTED_VEHICLE_THING_ARNS[@]} \
    --document ${DOCUMENT_STRING}


echo "Verifying that the job has completed"

echo "Executing the following command repeatedly until job.status shows COMPLETED:
aws iot describe-job \\
    ${ENDPOINT_URL_OPTION} --region ${REGION} \\
    --job-id ${UUID}"

for ((k=0; k<${JOB_STATUS_CHECK_RETRIES}; k++)); do
    JOB_STATUS=$(aws iot describe-job \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --job-id ${UUID} | jq -r ".job.status")

    if [ "${JOB_STATUS}" == "COMPLETED" ]; then
        break
    fi

    sleep 5
done
if ((k>=JOB_STATUS_CHECK_RETRIES)); then
    echo "Error: Job status check timeout for job ${UUID}" >&2
    exit -1
fi
echo "The job has COMPLETED"


COLLECTED_DATA_DIR="collected-data-${DISAMBIGUATOR}-after-request-forward/"
mkdir -p ${COLLECTED_DATA_DIR}

if ${S3_UPLOAD}; then
    DELAY=1500
    SLEEP_TIME=300
    echo "Waiting `expr ${DELAY} / 60` minutes for data to be collected and uploaded to S3..."
    for ((i=DELAY; i>0; i-=SLEEP_TIME)); do
        sleep $((i > SLEEP_TIME ? SLEEP_TIME : i))
        echo "..." # Print something to prevent the terminal timing out
    done

    echo "Downloading files from S3..."
    S3_URL="s3://${BUCKET_NAME}/${DISAMBIGUATOR}-s3-${S3_SUFFIX}/processed-data/"
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
else # Data destination is Timestream
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
fi
