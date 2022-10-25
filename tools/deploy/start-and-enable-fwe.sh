#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

# Start and enable the FWE service at startup:
systemctl daemon-reload
FLEET_SIZE=`ls -1 /etc/aws-iot-fleetwise/*.json | wc -l`
systemctl enable `seq -f "fwe@%.0f" 0 $((FLEET_SIZE-1))`
systemctl start `seq -f "fwe@%.0f" 0 $((FLEET_SIZE-1))`
