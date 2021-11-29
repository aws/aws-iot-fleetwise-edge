#!/bin/bash
# Copyright 2020 Amazon.com, Inc. and its affiliates. All Rights Reserved.
# SPDX-License-Identifier: LicenseRef-.amazon.com.-AmznSL-1.0
# Licensed under the Amazon Software License (the "License").
# You may not use this file except in compliance with the License.
# A copy of the License is located at
# http://aws.amazon.com/asl/
# or in the "license" file accompanying this file. This file is distributed
# on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
# express or implied. See the License for the specific language governing
# permissions and limitations under the License.

set -euo pipefail

if (($#<1)); then
    echo "Error: Instance number not provided" >&2
    exit -1
fi

# In order to reduce the CAN simulation overhead for a large fleet of vehicles, generation of CAN
# bus messages for CAN signals is only performed on vcan0, then frames are forwarded to all of the
# other buses using `cangw`. Simulation of the OBD server is however performed on every channel.

# If the instance number is greater than zero check whether the forwarding rule is already applied:
if (($1>0)) && ! cangw -L | grep -q "cangw -A -s vcan0 -d vcan$1 -e"; then
    # If not, add a forwarding rule from vcan0 to this bus, excluding diagnostic frames:
    cangw -A -s "vcan0" -d "vcan$1" -e -f "700~C0000700"
fi

# Start the CAN simulator: for instance numbers greater than zero, only OBD is simulated
/usr/bin/python3.7 /usr/share/cansim/cansim.py --interface "vcan$1" `(($1>0)) && echo "--only-obd"`
