#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

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
/usr/bin/python3 /usr/share/cansim/cansim.py --interface "vcan$1" `(($1>0)) && echo "--only-obd"`
