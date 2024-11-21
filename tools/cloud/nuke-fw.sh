#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

CONTINUE=false
ENDPOINT_URL=""
ENDPOINT_URL_OPTION=""
REGION=""

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --continue)
            CONTINUE=true
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
            echo "  --region <REGION>     Region to delete resources in"
            echo "  --endpoint-url <URL>  Endpoint URL"
            echo "  --continue            Don't prompt for confirmation"
            exit 0
            ;;
        esac
        shift
    done
}

parse_args "$@"

if [ "${REGION}" == "" ]; then
    echo "Error: no region specified"
    exit -1
fi

if [ "${ENDPOINT_URL}" != "" ]; then
    ENDPOINT_URL_OPTION=" --endpoint-url ${ENDPOINT_URL}"
fi

ACCOUNT_ID=`aws sts get-caller-identity | jq -r .Account`

echo "This script will delete all AWS IoT FleetWise resources in account ${ACCOUNT_ID} in region ${REGION}."
if [ "${ENDPOINT_URL}" != "" ]; then
    echo "(With endpoint URL ${ENDPOINT_URL})"
fi

if ! ${CONTINUE}; then
    echo -n "Are you sure you want to continue? [y/N]: "
    read CONFIRM
    if [ "${CONFIRM}" != "y" ]; then
        exit -1
    fi
fi

echo "Getting campaigns..."
CAMPAIGNS=`aws iotfleetwise list-campaigns --region ${REGION} ${ENDPOINT_URL_OPTION}`
for ((i=0;;i++)); do
    CAMPAIGN_NAME=`echo "${CAMPAIGNS}" | jq -r .campaignSummaries[$i].name`
    if [ "${CAMPAIGN_NAME}" == "null" ]; then
        break
    fi
    echo "Deleting campaign ${CAMPAIGN_NAME}..."
    aws iotfleetwise delete-campaign --region ${REGION} ${ENDPOINT_URL_OPTION} --name ${CAMPAIGN_NAME}
done

echo "Getting vehicles..."
VEHICLES=`aws iotfleetwise list-vehicles --region ${REGION} ${ENDPOINT_URL_OPTION}`
for ((i=0;;i++)); do
    VEHICLE_NAME=`echo "${VEHICLES}" | jq -r .vehicleSummaries[$i].vehicleName`
    if [ "${VEHICLE_NAME}" == "null" ]; then
        break
    fi
    echo "Deleting vehicle ${VEHICLE_NAME}..."
    aws iotfleetwise delete-vehicle --region ${REGION} ${ENDPOINT_URL_OPTION} --vehicle-name ${VEHICLE_NAME}
done

echo "Getting fleets..."
FLEETS=`aws iotfleetwise list-fleets --region ${REGION} ${ENDPOINT_URL_OPTION}`
for ((i=0;;i++)); do
    FLEET_ID=`echo "${FLEETS}" | jq -r .fleetSummaries[$i].id`
    if [ "${FLEET_ID}" == "null" ]; then
        break
    fi
    echo "Deleting fleet ${FLEET_ID}..."
    aws iotfleetwise delete-fleet --region ${REGION} ${ENDPOINT_URL_OPTION} --fleet-id ${FLEET_ID}
done

echo "Getting decoder manifests..."
DECODER_MANIFESTS=`aws iotfleetwise list-decoder-manifests --region ${REGION} ${ENDPOINT_URL_OPTION}`
for ((i=0;;i++)); do
    DECODER_MANIFEST_NAME=`echo "${DECODER_MANIFESTS}" | jq -r .summaries[$i].name`
    if [ "${DECODER_MANIFEST_NAME}" == "null" ]; then
        break
    fi
    echo "Deleting decoder manifest ${DECODER_MANIFEST_NAME}..."
    aws iotfleetwise delete-decoder-manifest --region ${REGION} ${ENDPOINT_URL_OPTION} --name ${DECODER_MANIFEST_NAME}
done

echo "Getting model manifests..."
MODEL_MANIFESTS=`aws iotfleetwise list-model-manifests --region ${REGION} ${ENDPOINT_URL_OPTION}`
for ((i=0;;i++)); do
    MODEL_MANIFEST_NAME=`echo "${MODEL_MANIFESTS}" | jq -r .summaries[$i].name`
    if [ "${MODEL_MANIFEST_NAME}" == "null" ]; then
        break
    fi
    echo "Deleting model manifest ${MODEL_MANIFEST_NAME}..."
    aws iotfleetwise delete-model-manifest --region ${REGION} ${ENDPOINT_URL_OPTION} --name ${MODEL_MANIFEST_NAME}
done

echo "Getting state templates..."
STATE_TEMPLATES=`aws iotfleetwise list-state-templates --region ${REGION} ${ENDPOINT_URL_OPTION}`
for ((i=0;;i++)); do
    STATE_TEMPLATE=`echo "${STATE_TEMPLATES}" | jq -r .summaries[$i].name`
    if [ "${STATE_TEMPLATE}" == "null" ]; then
        break
    fi
    echo "Deleting state template ${STATE_TEMPLATE}..."
    aws iotfleetwise delete-state-template --region ${REGION} ${ENDPOINT_URL_OPTION} --identifier ${STATE_TEMPLATE}
done

echo "Getting signal catalogs..."
SIGNAL_CATALOGS=`aws iotfleetwise list-signal-catalogs --region ${REGION} ${ENDPOINT_URL_OPTION}`
for ((i=0;;i++)); do
    SIGNAL_CATALOG_NAME=`echo "${SIGNAL_CATALOGS}" | jq -r .summaries[$i].name`
    if [ "${SIGNAL_CATALOG_NAME}" == "null" ]; then
        break
    fi
    echo "Deleting signal catalog ${SIGNAL_CATALOG_NAME}..."
    aws iotfleetwise delete-signal-catalog --region ${REGION} ${ENDPOINT_URL_OPTION} --name ${SIGNAL_CATALOG_NAME}
done

echo "Done!"
