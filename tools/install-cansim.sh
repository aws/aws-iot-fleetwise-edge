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

set -eo pipefail

BUS_COUNT=1

parse_args() {
    while [ "$#" -gt 0 ]; do
        case $1 in
        --bus-count)
            BUS_COUNT=$2
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

# Install Python 3.7 and pip
apt update && apt install -y python3.7 python3-setuptools curl
curl https://bootstrap.pypa.io/get-pip.py -o get-pip.py
python3.7 get-pip.py --user
rm get-pip.py

# Install pip packages
python3.7 -m pip install \
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
