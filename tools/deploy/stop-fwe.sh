#!/bin/sh
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

FLEET_SIZE=`ls -1 /etc/aws-iot-fleetwise/*.json | wc -l`
LAST_SERVICE=`expr ${FLEET_SIZE} - 1`
SERVICES=""
for i in `seq 0 ${LAST_SERVICE}`; do
   SERVICES="${SERVICES} fwe@$i"
done
echo "Stopping${SERVICES}"
systemctl stop ${SERVICES} 2> /dev/null || true
