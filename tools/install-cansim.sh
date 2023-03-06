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
