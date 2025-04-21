#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -xeuo pipefail

# Install basic dependencies
export DEBIAN_FRONTEND="noninteractive"
apt-get update
apt-get install -y \
    python3 \
    python3-pip \
    sudo \
    protobuf-compiler \
    git \
    curl \
    unzip \
    jq \
    can-utils \
    iproute2 \
    iptables \
    nftables \
    iputils-ping \
    build-essential \
    cmake \
    htop \
    linux-modules-extra-aws \
    ros-humble-rosidl-default-generators \
    python3-colcon-common-extensions

# Remove any existing pyOpenSSL installations
apt-get remove -y python3-openssl python3-service-identity || true
apt-get autoremove -y

git clone -b v0.9.10 https://github.com/wolfcw/libfaketime.git
cd libfaketime
make install -j `nproc`
cd ..

# Load SocketCAN modules and configure for startup
modprobe -a can can-raw vcan can-isotp can-gw vxcan
printf "can\ncan-raw\nvcan\ncan-isotp\ncan-gw\nvxcan\n" | sudo tee /etc/modules-load.d/can.conf > /dev/null

# Setup CAN interfaces
tee /usr/local/bin/setup-socketcan.sh > /dev/null <<EOT
#!/bin/bash
for i in {0..17}; do
    ip link add dev vcan\$i type vcan
    ip link set up vcan\$i
done
EOT

chmod +x /usr/local/bin/setup-socketcan.sh

# Create systemd service for SocketCAN
tee /lib/systemd/system/setup-socketcan.service > /dev/null <<EOT
[Unit]
Description=Setup SocketCAN interfaces
After=network.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/bin/setup-socketcan.sh

[Install]
WantedBy=multi-user.target
EOT

# Enable and start SocketCAN service
systemctl enable setup-socketcan
systemctl start setup-socketcan

# Configure network namespace DNS
for i in {0..17}; do
  mkdir -p /etc/netns/ns$i
  echo "nameserver 169.254.169.253" > /etc/netns/ns$i/resolv.conf
done

# Enable coredumps
echo "kernel.core_pattern = core.%p" >> /etc/sysctl.conf
sysctl -p

# Install AWS CLI if not already installed
if ! command -v aws &> /dev/null; then
    echo "AWS CLI not found. Installing..."

    # Detect architecture
    ARCH=$(uname -m)
    if [ "$ARCH" = "aarch64" ] || [ "$ARCH" = "arm64" ]; then
        # ARM64 version
        curl --fail-with-body "https://awscli.amazonaws.com/awscli-exe-linux-aarch64.zip" -o "awscliv2.zip"
    else
        # x86_64 version
        curl --fail-with-body "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip"
    fi

    unzip -q awscliv2.zip
    ./aws/install
    rm -rf ./aws*
    echo "AWS CLI installed successfully"
else
    echo "AWS CLI is already installed, skipping installation"
fi

# Install Greengrass and its dependencies
apt-get install -y default-jre
curl --fail-with-body "https://d2s8p88vqu9w66.cloudfront.net/releases/greengrass-2.14.2.zip" -o "greengrass.zip" \
    && unzip -q -d /usr/local/greengrass greengrass.zip \
    && rm greengrass.zip
