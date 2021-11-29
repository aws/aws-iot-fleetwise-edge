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

# Install packages
apt update && apt install -y build-essential dkms can-utils git linux-modules-extra-`uname -r`

# Install can-isotp kernel module:
git clone https://github.com/hartkopp/can-isotp.git
cd can-isotp
git checkout beb4650660179963a8ed5b5cbf2085cc1b34f608
cd ..
sudo mv can-isotp /usr/src/can-isotp-1.0
sudo sed -e s/else// -e s/shell\ uname\ \-r/KERNELRELEASE/ -i /usr/src/can-isotp-1.0/Makefile
sudo tee /usr/src/can-isotp-1.0/dkms.conf > /dev/null <<EOT
PACKAGE_NAME="can-isotp"
PACKAGE_VERSION="1.0"
MAKE[0]="make modules"
CLEAN="make clean"
BUILT_MODULE_NAME[0]="can-isotp"
DEST_MODULE_LOCATION[0]="/kernel/drivers/net/can"
BUILT_MODULE_LOCATION[0]="./net/can"
AUTOINSTALL="yes"
EOT
sudo dkms add -m can-isotp -v 1.0
sudo dkms build -m can-isotp -v 1.0
sudo dkms install -m can-isotp -v 1.0
sudo cp /usr/src/can-isotp-1.0/include/uapi/linux/can/isotp.h /usr/include/linux/can

# Load CAN modules, also at startup:
sudo modprobe can-isotp can-gw
printf "can-isotp\ncan-gw\n" | sudo tee /etc/modules-load.d/can.conf > /dev/null

# Install setup-socketcan
for ((i=0; i<${BUS_COUNT}; i++)); do
    printf "ip link add dev vcan${i} type vcan; ip link set up vcan${i}\n" \
        | sudo tee -a /usr/local/bin/setup-socketcan.sh > /dev/null
done
sudo tee /lib/systemd/system/setup-socketcan.service > /dev/null <<EOT
[Unit]
Description=Setup SocketCAN interfaces
After=multi-user.target
[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh /usr/local/bin/setup-socketcan.sh
[Install]
WantedBy=multi-user.target
EOT
sudo systemctl start setup-socketcan
sudo systemctl enable setup-socketcan
