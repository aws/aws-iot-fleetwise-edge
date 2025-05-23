#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

export DEBIAN_FRONTEND="noninteractive"

apt-get update
apt-get install -y \
    gawk wget git-core diffstat unzip texinfo \
    build-essential chrpath socat cpio python3 python3-pip python3-pexpect \
    xz-utils debianutils iputils-ping python3-git python3-jinja2 libegl1-mesa libsdl1.2-dev \
    pylint xterm curl dosfstools mtools parted syslinux-common tree zip ca-certificates \
    zstd lz4 file

if [ `dpkg --print-architecture` == "amd64" ]; then
    apt-get install -y gcc-multilib
fi

# Google 'repo' tool, see https://source.android.com/setup/develop/repo
curl https://raw.githubusercontent.com/GerritCodeReview/git-repo/v2.36.1/repo > /usr/local/bin/repo
chmod +x /usr/local/bin/repo
