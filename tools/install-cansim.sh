#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -eo pipefail

BUS_COUNT=1

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --bus-count)
            BUS_COUNT=$2
            shift
            ;;
        --help)
            echo "Usage: $0 [OPTION]"
            echo "  --bus-count <NUM>  Number of CAN buses"
            exit 0
            ;;
        esac
        shift
    done
}

parse_args "$@"

# Install Python 3 and pip
apt update && apt install -y python3 python3-pip

# Install pip packages
pip3 install \
    wrapt==1.10.0 \
    cantools==36.4.0 \
    prompt_toolkit==3.0.21 \
    python-can==3.3.4 \
    can-isotp==1.7 \
    matplotlib==3.4.3

# Install CAN Simulator
cp -r tools/cansim /usr/share
cp tools/cansim/cansim@.service /lib/systemd/system/
systemctl daemon-reload
systemctl enable `seq -f "cansim@%.0f" 0 $((BUS_COUNT-1))`
systemctl start `seq -f "cansim@%.0f" 0 $((BUS_COUNT-1))`

# Check that the simulators started correctly and are generating data
for SEQUENCE in `seq 0 $((BUS_COUNT-1))`; do
    DEVICE=vcan${SEQUENCE}
    echo Checking if device ${DEVICE} is receiving data;
    NUM_MESSAGES=$(candump -n 10 -T 5000 ${DEVICE} | wc -l)
    echo "Received ${NUM_MESSAGES} messages";
    if [ ${NUM_MESSAGES} -ne 10 ]; then
        echo "Received fewer messages than expected. Most likely the CAN simulator #${SEQUENCE} didn't start correctly. See the log below:";
        journalctl -u cansim@${SEQUENCE}.service | tail -n100
        exit 1
    fi
done
