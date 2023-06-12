#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

ENDPOINT_URL=""
ENDPOINT_URL_OPTION=""
REGION="us-east-1"
VEHICLE_NAME_FILENAME="config/vehicle-name.txt"
VEHICLE_NAME=""
THING_POLICY_FILENAME="config/thing-policy.txt"
THING_POLICY=""
SERVICE_ROLE_ARN_FILENAME="config/service-role-arn.txt"
SERVICE_ROLE_ARN=""
TIMESTAMP_FILENAME="config/timestamp.txt"
TIMESTAMP=""

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --vehicle-name)
            VEHICLE_NAME=$2
            shift
            ;;
        --timestamp)
            TIMESTAMP=$2
            shift
            ;;
        --service-role-arn)
            SERVICE_ROLE_ARN=$2
            shift
            ;;
        --thing-policy)
            THING_POLICY=$2
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
            echo "  --vehicle-name <NAME>     Vehicle name, by default will be read from ${VEHICLE_NAME_FILENAME}"
            echo "  --timestamp <TS>          Timestamp of setup-iotfleetwise.sh script, by default will be read from ${TIMESTAMP_FILENAME}"
            echo "  --service-role-arn <ARN>  Service role ARN for accessing Timestream, by default will be read from ${SERVICE_ROLE_ARN_FILENAME}"
            echo "  --thing-policy <NAME>     Name of IoT thing policy, by default will be read from ${THING_POLICY_FILENAME}"
            echo "  --endpoint-url <URL>      The endpoint URL used for AWS CLI calls"
            echo "  --region <REGION>         The region used for AWS CLI calls, default: ${REGION}"
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
            echo "Error: vehicle name not provided"
            exit -1
        fi
    fi

    if [ "${TIMESTAMP}" == "" ]; then
        if [ -f "${TIMESTAMP_FILENAME}" ]; then
            TIMESTAMP=`cat ${TIMESTAMP_FILENAME}`
        fi
    fi

    if [ "${SERVICE_ROLE_ARN}" == "" ]; then
        if [ -f "${SERVICE_ROLE_ARN_FILENAME}" ]; then
            SERVICE_ROLE_ARN=`cat ${SERVICE_ROLE_ARN_FILENAME}`
        fi
    fi

    if [ "${THING_POLICY}" == "" ]; then
        if [ -f "${THING_POLICY_FILENAME}" ]; then
            THING_POLICY=`cat ${THING_POLICY_FILENAME}`
        fi
    fi
}

echo "========================================="
echo "AWS IoT FleetWise Android Clean-up Script"
echo "========================================="

parse_args "$@"

echo "Getting AWS account ID..."
ACCOUNT_ID=`aws sts get-caller-identity --query "Account" --output text`
echo ${ACCOUNT_ID}

if [ "${TIMESTAMP}" != "" ]; then
    NAME="${VEHICLE_NAME}-${TIMESTAMP}"

    echo "Suspending campaign..."
    aws iotfleetwise update-campaign \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${NAME}-campaign \
        --action SUSPEND | jq -r .arn || true

    echo "Deleting campaign..."
    aws iotfleetwise delete-campaign \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${NAME}-campaign | jq -r .arn || true

    echo "Disassociating vehicle ${VEHICLE_NAME}..."
    aws iotfleetwise disassociate-vehicle-fleet \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --fleet-id ${NAME}-fleet \
        --vehicle-name "${VEHICLE_NAME}" || true

    echo "Deleting vehicle ${VEHICLE_NAME}..."
    aws iotfleetwise delete-vehicle \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --vehicle-name "${VEHICLE_NAME}" | jq -r .arn || true

    echo "Deleting fleet..."
    aws iotfleetwise delete-fleet \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --fleet-id ${NAME}-fleet | jq -r .arn || true

    echo "Deleting decoder manifest..."
    aws iotfleetwise delete-decoder-manifest \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${NAME}-decoder-manifest | jq -r .arn || true

    echo "Deleting model manifest..."
    aws iotfleetwise delete-model-manifest \
        ${ENDPOINT_URL_OPTION} --region ${REGION} \
        --name ${NAME}-model-manifest | jq -r .arn || true

    rm -f ${TIMESTAMP_FILENAME}
fi

if [ "${SERVICE_ROLE_ARN}" != "" ]; then
    SERVICE_ROLE=`echo "${SERVICE_ROLE_ARN}" | cut -d "/" -f 2`
    SERVICE_ROLE_POLICY_ARN="arn:aws:iam::${ACCOUNT_ID}:policy/${SERVICE_ROLE}-policy"
    echo "Detatching policy..."
    aws iam detach-role-policy --role-name ${SERVICE_ROLE} --policy-arn ${SERVICE_ROLE_POLICY_ARN} --region ${REGION} || true
    echo "Deleting policy..."
    aws iam delete-policy --policy-arn ${SERVICE_ROLE_POLICY_ARN} --region ${REGION} || true
    echo "Deleting role..."
    aws iam delete-role --role-name ${SERVICE_ROLE} --region ${REGION} || true

    rm -f ${SERVICE_ROLE_ARN_FILENAME}
fi

echo "Listing thing principals..."
THING_PRINCIPAL=`aws iot list-thing-principals --region ${REGION} --thing-name ${VEHICLE_NAME} | jq -r ".principals[0]" || true`
echo "Detaching thing principal..."
aws iot detach-thing-principal --region ${REGION} --thing-name ${VEHICLE_NAME} --principal ${THING_PRINCIPAL} || true
echo "Detaching thing policy..."
aws iot detach-policy --region ${REGION} --policy-name ${THING_POLICY} --target ${THING_PRINCIPAL} || true
echo "Deleting thing..."
aws iot delete-thing --region ${REGION} --thing-name ${VEHICLE_NAME} || true
echo "Deleting thing policy..."
aws iot delete-policy --region ${REGION} --policy-name ${THING_POLICY} || true
CERTIFICATE_ID=`echo "${THING_PRINCIPAL}" | cut -d "/" -f 2 || true`
echo "Updating certificate as INACTIVE..."
aws iot update-certificate --region ${REGION} --certificate-id ${CERTIFICATE_ID} --new-status INACTIVE || true
echo "Deleting certificate..."
aws iot delete-certificate --region ${REGION} --certificate-id ${CERTIFICATE_ID} --force-delete || true

rm -f ${VEHICLE_NAME_FILENAME}
rm -f ${THING_POLICY_FILENAME}
rm -f config/certificate.pem
rm -f config/private-key.key
rm -f config/creds.json
rm -f config/endpoint.txt
rm -f config/provisioning-qr-code.png

echo "========="
echo "Finished!"
echo "========="
