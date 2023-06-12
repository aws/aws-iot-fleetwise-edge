#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

S3_BUCKET=""
S3_KEY_PREFIX=""
S3_PRESIGNED_URL_EXPIRY=86400 # One day
ENDPOINT_URL=""
ENDPOINT_URL_OPTION=""
REGION="us-east-1"
TOPIC_PREFIX="\$aws/iotfleetwise/"
TIMESTAMP=`date +%s`
VEHICLE_NAME="fwdemo-android-${TIMESTAMP}"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --s3-bucket)
            S3_BUCKET=$2
            shift
            ;;
        --s3-key-prefix)
            S3_KEY_PREFIX=$2
            shift
            ;;
        --s3-presigned-url-expiry)
            S3_PRESIGNED_URL_EXPIRY=$2
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
        --topic-prefix)
            TOPIC_PREFIX=$2
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --s3-bucket <NAME>                   Existing S3 bucket name"
            echo "  --s3-key-prefix <PREFIX>             S3 bucket prefix"
            echo "  --s3-presigned-url-expiry <SECONDS>  S3 presigned URL expiry, default: ${S3_PRESIGNED_URL_EXPIRY}"
            echo "  --endpoint-url <URL>                 The endpoint URL used for AWS CLI calls"
            echo "  --region                             The region used for AWS CLI calls, default: ${REGION}"
            echo "  --topic-prefix <PREFIX>              IoT MQTT topic prefix, default: ${TOPIC_PREFIX}"
            exit 0
            ;;
        esac
        shift
    done

    if [ "${ENDPOINT_URL}" != "" ]; then
        ENDPOINT_URL_OPTION="--endpoint-url ${ENDPOINT_URL}"
    fi
}

parse_args "$@"

if [ "${S3_BUCKET}" == "" ]; then
    echo -n "Enter name of existing S3 bucket: "
    read S3_BUCKET
    if [ "${S3_BUCKET}" == "" ]; then
        echo "Error: S3 bucket name required"
        exit -1
    fi
fi

mkdir -p config
../../provision.sh \
    --vehicle-name ${VEHICLE_NAME} \
    ${ENDPOINT_URL_OPTION} \
    --region ${REGION} \
    --certificate-pem-outfile config/certificate.pem \
    --private-key-outfile config/private-key.key \
    --endpoint-url-outfile config/endpoint.txt \
    --vehicle-name-outfile config/vehicle-name.txt \
    --thing-policy-outfile config/thing-policy.txt

CERTIFICATE=`cat config/certificate.pem`
PRIVATE_KEY=`cat config/private-key.key`
MQTT_ENDPOINT_URL=`cat config/endpoint.txt`
echo {} | jq ".vehicle_name=\"${VEHICLE_NAME}\"" \
    | jq ".endpoint_url=\"${MQTT_ENDPOINT_URL}\"" \
    | jq ".certificate=\"${CERTIFICATE}\"" \
    | jq ".private_key=\"${PRIVATE_KEY}\"" \
    | jq ".mqtt_topic_prefix=\"${TOPIC_PREFIX}\"" \
    > config/creds.json

S3_URL="s3://${S3_BUCKET}/${S3_KEY_PREFIX}${VEHICLE_NAME}-creds.json"
echo "Uploading credentials to S3..."
aws s3 cp --region ${REGION} config/creds.json ${S3_URL}
echo "Creating S3 pre-signed URL..."
S3_PRESIGNED_URL=`aws s3 presign --region ${REGION} --expires-in ${S3_PRESIGNED_URL_EXPIRY} ${S3_URL}`
PROVISIONING_LINK="https://fleetwise-app.automotive.iot.aws.dev/config#url=`echo ${S3_PRESIGNED_URL} | jq -s -R -r @uri`"
echo "Provisioning link:"
echo ${PROVISIONING_LINK}
QR_CODE_FILENAME="config/provisioning-qr-code.png"
segno --scale 5 --output ${QR_CODE_FILENAME} "${PROVISIONING_LINK}"

echo
echo "You can now download the provisioning QR code."
echo "----------------------------------"
echo "| Provisioning QR code filename: |"
echo "----------------------------------"
echo `realpath ${QR_CODE_FILENAME}`

echo
echo "Optional: After you have downloaded and scanned the QR code, you can delete the credentials for security by running the following command: aws s3 rm ${S3_URL}"
