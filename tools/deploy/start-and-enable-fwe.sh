#!/bin/sh
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -e

# Start and enable the FWE service at startup:
systemctl daemon-reload
FLEET_SIZE=`ls -1 /etc/aws-iot-fleetwise/*.json | wc -l`
LAST_SERVICE=0 # expr returns error code when the expression result is 0, so default to 0
if [ "${FLEET_SIZE}" != "1" ]; then
    LAST_SERVICE=`expr ${FLEET_SIZE} - 1`
fi
SERVICES=""
for i in `seq 0 ${LAST_SERVICE}`; do
    SERVICES="${SERVICES} fwe@$i"
done
echo "Starting and enabling${SERVICES}"
systemctl enable ${SERVICES}
START_TIME=$(date -u '+%Y-%m-%d %H:%M:%S')
systemctl start ${SERVICES}

SUCCESS_MESSAGE="Started successfully"
for SERVICE_NAME in ${SERVICES}; do
    # sed will quit as soon as it finds the first match and this will make `journalctl -f` quit too
    if timeout 5s bash -c "journalctl -u ${SERVICE_NAME} -f --no-tail --since \"${START_TIME}\" | sed '/${SUCCESS_MESSAGE}/ q' > /dev/null"; then
        echo "${SERVICE_NAME} started successfully"
    else
        echo "${SERVICE_NAME} didn't start correctly. Please check the logs below:"
        journalctl -u ${SERVICE_NAME} --since "${START_TIME}"
        exit 1
    fi
done
