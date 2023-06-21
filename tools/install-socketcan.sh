#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

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

if ! command -v cansend > /dev/null; then
    # Install packages
    apt update && apt install -y can-utils
fi

# For EC2, the SocketCAN modules vcan and can-gw are included in a separate package:
if uname -r | grep -q aws; then
    apt update && apt install -y linux-modules-extra-aws
fi

# Install can-isotp kernel module if not installed
# can-isotp module is part of the mainline Linux kernel since version 5.10
MODULE="can_isotp"
echo "Installing kernel module: $MODULE"
if modinfo $MODULE &> /dev/null ; then
    echo "$MODULE is already in system. There is no need to install it."
else
    apt update && apt install -y build-essential dkms git
    git clone https://github.com/hartkopp/can-isotp.git
    cd can-isotp
    git checkout beb4650660179963a8ed5b5cbf2085cc1b34f608
    cd ..
    if [ -d /usr/src/can-isotp-1.0 ]; then
        dkms remove can-isotp/1.0 --all
        rm -rf /usr/src/can-isotp-1.0
    fi
    mv can-isotp /usr/src/can-isotp-1.0
    sed -e s/else// -e s/shell\ uname\ \-r/KERNELRELEASE/ -i /usr/src/can-isotp-1.0/Makefile
    cat > /usr/src/can-isotp-1.0/dkms.conf <<EOT
PACKAGE_NAME="can-isotp"
PACKAGE_VERSION="1.0"
MAKE[0]="make modules"
CLEAN="make clean"
BUILT_MODULE_NAME[0]="can-isotp"
DEST_MODULE_LOCATION[0]="/kernel/drivers/net/can"
BUILT_MODULE_LOCATION[0]="./net/can"
AUTOINSTALL="yes"
EOT
    dkms add -m can-isotp -v 1.0
    dkms build -m can-isotp -v 1.0
    dkms install -m can-isotp -v 1.0
    cp /usr/src/can-isotp-1.0/include/uapi/linux/can/isotp.h /usr/include/linux/can
fi

# Load CAN modules, also at startup:
modprobe -a can-isotp can-gw
printf "can-isotp\ncan-gw\n" > /etc/modules-load.d/can.conf

# Install setup-socketcan
for ((i=0; i<${BUS_COUNT}; i++)); do
    printf "ip link add dev vcan${i} type vcan; ip link set up vcan${i}\n" \
        >> /usr/local/bin/setup-socketcan.sh
done
cat > /lib/systemd/system/setup-socketcan.service <<EOT
[Unit]
Description=Setup SocketCAN interfaces
After=network.target
[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh /usr/local/bin/setup-socketcan.sh
[Install]
WantedBy=multi-user.target
EOT
systemctl enable setup-socketcan
systemctl start setup-socketcan
