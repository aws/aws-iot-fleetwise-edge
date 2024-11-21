#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -feuo pipefail

SERVICE_ROLE=""
SERVICE_PRINCIPAL=""
ACTIONS=()
RESOURCES=()
CLEAN_UP="false"

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --service-role)
            SERVICE_ROLE=$2
            shift
            ;;
        --service-principal)
            SERVICE_PRINCIPAL=$2
            shift
            ;;
        --actions)
            ACTIONS+=("$2")
            shift
            ;;
        --resources)
            RESOURCES+=("$2")
            shift
            ;;
        --clean-up)
            CLEAN_UP="true"
            ;;
        --help)
            echo "Usage: $0 [OPTION]" >&2
            echo "  --service-role <NAME>       Service role name" >&2
            echo "  --service-principal <NAME>  Service principal" >&2
            echo "  --actions <ACTIONS>         CSV of actions to allow. Specify multiple times for multiple statements." >&2
            echo "  --resources <RESOURCES>     CSV of resources to allow. Specify multiple times for multiple statements." >&2
            echo "  --clean-up                  Clean up the service role" >&2
            exit 0
            ;;
        esac
        shift
    done

    if [ "${SERVICE_ROLE}" == "" ]; then
        echo "Error: No service role provided" >&2
        exit -1
    fi
    if ! ${CLEAN_UP} && [ "${SERVICE_PRINCIPAL}" == "" ]; then
        echo "Error: No service principal provided" >&2
        exit -1
    fi
    if ! ${CLEAN_UP} && [ ${#ACTIONS[@]} -eq 0 ]; then
        echo "Error: No actions provided" >&2
        exit -1
    fi
    if ! ${CLEAN_UP} && [ ${#ACTIONS[@]} -ne ${#RESOURCES[@]} ]; then
        echo "Error: Number of actions doesn't match number of resources" >&2
        exit -1
    fi
}

parse_args "$@"

if $CLEAN_UP; then
    echo "Checking if role ${SERVICE_ROLE} exists..." >&2
    if ! GET_ROLE_OUTPUT=`aws iam get-role --role-name ${SERVICE_ROLE} 2>&1`; then
        if ! echo ${GET_ROLE_OUTPUT} | grep -q "NoSuchEntity"; then
            echo ${GET_ROLE_OUTPUT} >&2
            exit -1
        fi
        exit 0
    fi

    INLINE_POLICIES=$(
        aws iam list-role-policies \
            --role-name ${SERVICE_ROLE} --query 'PolicyNames[]' --output text
    )
    for POLICY_NAME in $INLINE_POLICIES; do
        echo "Deleting inline policy: $POLICY_NAME from role: $SERVICE_ROLE"
        aws iam delete-role-policy \
            --role-name ${SERVICE_ROLE} --policy-name ${POLICY_NAME}
    done

    MAX_ATTEMPTS=60
    for ((i=0; ; i++)); do
        echo "Deleting service role ${SERVICE_ROLE}..." >&2
        if DELETE_ROLE_OUTPUT=`aws iam delete-role --role-name ${SERVICE_ROLE} 2>&1`; then
            break
        elif ((i >= MAX_ATTEMPTS)); then
            echo "Error: timeout deleting role: ${DELETE_ROLE_OUTPUT}" >&2
            exit -1
        else
            sleep 1
        fi
    done
    exit 0
fi

echo "Checking if role ${SERVICE_ROLE} already exists..." >&2
if GET_ROLE_OUTPUT=`aws iam get-role --role-name ${SERVICE_ROLE} 2>&1`; then
    echo ${GET_ROLE_OUTPUT} | jq -r .Role.Arn
    exit 0
fi
if ! echo ${GET_ROLE_OUTPUT} | grep -q "NoSuchEntity"; then
    echo ${GET_ROLE_OUTPUT} >&2
    exit -1
fi

SERVICE_ROLE_TRUST_POLICY=$(cat << EOF
{
    "Version": "2012-10-17",
    "Statement": [
        {
        "Effect": "Allow",
        "Principal": {
            "Service": [
                "${SERVICE_PRINCIPAL}"
            ]
        },
        "Action": "sts:AssumeRole"
        }
    ]
}
EOF
)
echo "Creating role ${SERVICE_ROLE}..." >&2
SERVICE_ROLE_ARN=`aws iam create-role \
    --role-name "${SERVICE_ROLE}" \
    --assume-role-policy-document "${SERVICE_ROLE_TRUST_POLICY}" | jq -r .Role.Arn`
echo "Waiting for role to be created..." >&2
aws iam wait role-exists --role-name "${SERVICE_ROLE}"
SERVICE_ROLE_POLICY='{"Version": "2012-10-17", "Statement": []}'
i=0
for ACTION_LIST in "${ACTIONS[@]}"; do
    SERVICE_ROLE_POLICY=`echo "${SERVICE_ROLE_POLICY}" | jq '.Statement+=[{"Effect": "Allow", "Action": [], "Resource": []}]'`
    for ACTION in `echo ${ACTION_LIST} | tr ',' ' '`; do
        SERVICE_ROLE_POLICY=`echo "${SERVICE_ROLE_POLICY}" | jq ".Statement[${i}].Action+=[\"${ACTION}\"]"`
    done
    ((++i))
done
i=0
for RESOURCE_LIST in "${RESOURCES[@]}"; do
    for RESOURCE in `echo ${RESOURCE_LIST} | tr ',' ' '`; do
        SERVICE_ROLE_POLICY=`echo "${SERVICE_ROLE_POLICY}" | jq ".Statement[${i}].Resource+=[\"${RESOURCE}\"]"`
    done
    ((++i))
done
echo "Putting role policy..." >&2
aws iam put-role-policy \
    --role-name "${SERVICE_ROLE}" \
    --policy-name ${SERVICE_ROLE}-policy \
    --policy-document "${SERVICE_ROLE_POLICY}"

echo ${SERVICE_ROLE_ARN}
