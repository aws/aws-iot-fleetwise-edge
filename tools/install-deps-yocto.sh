#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

export DEBIAN_FRONTEND="noninteractive"

apt update -o DPkg::Lock::Timeout=120
apt install -y -o DPkg::Lock::Timeout=120 \
    gawk wget git-core diffstat unzip texinfo \
    build-essential chrpath socat cpio python3 python3-pip python3-pexpect \
    xz-utils debianutils iputils-ping python3-git python3-jinja2 libegl1-mesa libsdl1.2-dev \
    pylint3 xterm curl dosfstools mtools parted syslinux-common tree zip python ca-certificates \
    zstd lz4

if [ `dpkg --print-architecture` == "amd64" ]; then
    apt install -y -o DPkg::Lock::Timeout=120 gcc-multilib
fi

# Google 'repo' tool, see https://source.android.com/setup/develop/repo
curl https://raw.githubusercontent.com/GerritCodeReview/git-repo/v2.36.1/repo > /usr/local/bin/repo
chmod +x /usr/local/bin/repo
