#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -eo pipefail

ENDPOINT_URL=""
ENDPOINT_URL_OPTION=""
REGION="us-east-1"
TIMESTAMP=`date +%s`
DEFAULT_VEHICLE_NAME="fwdemo"
VEHICLE_NAME=""
CERT_OUT_FILE="certificate.pem"
PRIVATE_KEY_OUT_FILE="private-key.key"
ENDPOINT_URL_OUT_FILE=""
VEHICLE_NAME_OUT_FILE=""
THING_POLICY_OUT_FILE=""
ONLY_CLEAN_UP=false

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --vehicle-name)
            VEHICLE_NAME=$2
            shift
            ;;
        --certificate-pem-outfile)
            CERT_OUT_FILE=$2
            shift
            ;;
        --private-key-outfile)
            PRIVATE_KEY_OUT_FILE=$2
            shift
            ;;
        --endpoint-url-outfile)
            ENDPOINT_URL_OUT_FILE=$2
            shift
            ;;
        --vehicle-name-outfile)
            VEHICLE_NAME_OUT_FILE=$2
            shift
            ;;
        --thing-policy-outfile)
            THING_POLICY_OUT_FILE=$2
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
        --only-clean-up)
            ONLY_CLEAN_UP=true
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --vehicle-name <NAME>                 Vehicle name"
            echo "  --certificate-pem-outfile <FILENAME>  Certificate output file, default: ${CERT_OUT_FILE}"
            echo "  --private-key-outfile <FILENAME>      Private key output file, default: ${PRIVATE_KEY_OUT_FILE}"
            echo "  --endpoint-url-outfile <FILENAME>     Endpoint URL for MQTT connections output file"
            echo "  --vehicle-name-outfile <FILENAME>     Vehicle name output file"
            echo "  --thing-policy-outfile <FILENAME>     Thing policy output file"
            echo "  --endpoint-url <URL>                  The endpoint URL used for AWS CLI calls"
            echo "  --region                              The region used for AWS CLI calls, default: ${REGION}"
            echo "  --only-clean-up                       Clean up resources created by previous runs of this script"
            exit 0
            ;;
        esac
        shift
    done

    if [ "${ENDPOINT_URL}" != "" ]; then
        ENDPOINT_URL_OPTION="--endpoint-url ${ENDPOINT_URL}"
    fi
}

echo "=============================="
echo "AWS IoT FleetWise Provisioning"
echo "=============================="

parse_args "$@"

if [ "${VEHICLE_NAME}" == "" ]; then
    echo -n "Enter Vehicle name [${DEFAULT_VEHICLE_NAME}]: "
    read VEHICLE_NAME
    if [ "${VEHICLE_NAME}" == "" ]; then
        VEHICLE_NAME=${DEFAULT_VEHICLE_NAME}
    fi
fi

NAME="${VEHICLE_NAME}-${TIMESTAMP}"

if [ ${ONLY_CLEAN_UP} == true ]; then

    PRINCIPAL_ARN=$(aws iot list-thing-principals --thing-name ${VEHICLE_NAME} --region ${REGION} | jq -r ".principals[0]")
    CERTIFICATE_ID=$(echo ${PRINCIPAL_ARN} | cut -d'/' -f2 )
    POLICY_NAME=$(aws iot list-principal-policies --principal ${PRINCIPAL_ARN} --region ${REGION} | jq -r ".policies[0].policyName")

    RETRY_COUNTER=10
    while ! aws iot delete-thing --thing-name ${VEHICLE_NAME} --region ${REGION}
    do
        # Delete depending resources
        aws iot detach-thing-principal --thing-name ${VEHICLE_NAME} --principal ${PRINCIPAL_ARN} --region ${REGION} || true
        aws iot detach-principal-policy --policy-name "${POLICY_NAME}" --principal ${PRINCIPAL_ARN} --region ${REGION} || true
        aws iot delete-policy --policy-name "${POLICY_NAME}" --region ${REGION} || true
        aws iot update-certificate --certificate-id ${CERTIFICATE_ID} --new-status INACTIVE --region ${REGION} || true
        aws iot delete-certificate --certificate-id ${CERTIFICATE_ID} --region ${REGION} || true

        echo "Wait one second before next retry"
        sleep 1
        if [ "${RETRY_COUNTER}" -eq "0" ]; then
            echo "Failed aws iot delete-thing"
            exit 1
        fi
        ((RETRY_COUNTER--))
    done
    echo "Successfully deleted iot thing"
    exit 0

fi

echo -n "Date: "
date --rfc-3339=seconds

echo "Vehicle name: ${VEHICLE_NAME}"

echo "Getting AWS account ID..."
ACCOUNT_ID=`aws sts get-caller-identity | jq -r .Account`
echo ${ACCOUNT_ID}

echo "Creating IoT thing..."
aws iot create-thing \
    ${ENDPOINT_URL_OPTION} \
    --region ${REGION} \
    --thing-name ${VEHICLE_NAME} \
    | jq -r .thingArn

IOT_POLICY=`cat <<EOF
{
    "Version": "2012-10-17",
    "Statement": [
        {
            "Effect": "Allow",
            "Action": [
                "iot:Connect",
                "iot:Subscribe",
                "iot:Publish",
                "iot:Receive"
            ],
            "Resource": [
            ]
        }
    ]
}
EOF
`
IOT_POLICY=`echo "${IOT_POLICY}" \
    | jq ".Statement[0].Resource[0]=\"arn:aws:iot:${REGION}:${ACCOUNT_ID}:client/${VEHICLE_NAME}\"" \
    | jq ".Statement[0].Resource[1]=\"arn:aws:iot:${REGION}:${ACCOUNT_ID}:topic/*\"" \
    | jq ".Statement[0].Resource[2]=\"arn:aws:iot:${REGION}:${ACCOUNT_ID}:topicfilter/*\""`

echo "Creating policy..."
aws iot create-policy \
    ${ENDPOINT_URL_OPTION} \
    --region ${REGION} \
    --policy-name "${NAME}-IoT-Policy" \
    --policy-document "${IOT_POLICY}" \
    | jq -r .policyArn

echo "Creating keys and certificate..."
CERT_ARN=`aws iot create-keys-and-certificate \
    ${ENDPOINT_URL_OPTION} \
    --region ${REGION} \
    --set-as-active \
    --certificate-pem-outfile "${CERT_OUT_FILE}" \
    --private-key-outfile "${PRIVATE_KEY_OUT_FILE}" \
    | jq -r .certificateArn`
echo ${CERT_ARN}

echo "Attaching policy..."
aws iot attach-policy \
    ${ENDPOINT_URL_OPTION} \
    --region ${REGION} \
    --policy-name "${NAME}-IoT-Policy" \
    --target ${CERT_ARN}

echo "Attaching thing principal..."
aws iot attach-thing-principal \
    ${ENDPOINT_URL_OPTION} \
    --region ${REGION} \
    --thing-name ${VEHICLE_NAME} \
    --principal ${CERT_ARN}

echo "Getting endpoint..."
ENDPOINT=`aws iot describe-endpoint \
    ${ENDPOINT_URL_OPTION} \
    --region ${REGION} \
    --endpoint-type "iot:Data-ATS" \
    | jq -j .endpointAddress`
echo ${ENDPOINT}

if [ "${ENDPOINT_URL_OUT_FILE}" != "" ]; then
    echo ${ENDPOINT} > ${ENDPOINT_URL_OUT_FILE}
fi

if [ "${VEHICLE_NAME_OUT_FILE}" != "" ]; then
    echo -n $VEHICLE_NAME > ${VEHICLE_NAME_OUT_FILE}
fi

if [ "${THING_POLICY_OUT_FILE}" != "" ]; then
    echo -n ${NAME}-IoT-Policy > ${THING_POLICY_OUT_FILE}
fi
