#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

CONFIG_FILE="/etc/aws-iot-fleetwise/config-0.json"

mkdir -p `dirname ${CONFIG_FILE}`
/usr/bin/configure-fwe.sh \
    --input-config-file /usr/share/aws-iot-fleetwise/static-config.json \
    --output-config-file ${CONFIG_FILE} \
    $@
CAN_IF=`jq -r .networkInterfaces[0].canInterface.interfaceName ${CONFIG_FILE}`
PERSISTENCY_PATH=`jq -r .staticConfig.persistency.persistencyPath ${CONFIG_FILE}`

mkdir -p ${PERSISTENCY_PATH}

function ifup {
    typeset output
    output=$(ip link show "$1" up) && [[ -n $output ]]
}

while true; do
    if ifup ${CAN_IF}; then
        break
    fi
    echo "Waiting for $CAN_IF"
    sleep 3
done

/usr/bin/aws-iot-fleetwise-edge ${CONFIG_FILE}
