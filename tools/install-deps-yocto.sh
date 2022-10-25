#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

export DEBIAN_FRONTEND="noninteractive"

apt update
apt install -y \
    gawk wget git-core diffstat unzip texinfo \
    build-essential chrpath socat cpio python3 python3-pip python3-pexpect \
    xz-utils debianutils iputils-ping python3-git python3-jinja2 libegl1-mesa libsdl1.2-dev \
    pylint3 xterm curl dosfstools mtools parted syslinux-common tree zip python ca-certificates

if [ `dpkg --print-architecture` == "amd64" ]; then
    apt install -y gcc-multilib
fi

# Google 'repo' tool, see https://source.android.com/setup/develop/repo
curl https://storage.googleapis.com/git-repo-downloads/repo > /usr/local/bin/repo
chmod +x /usr/local/bin/repo
# 'repo' requires that Git is configured, otherwise it interactively prompts for the configuration:
git config user.name > /dev/null || git config --global user.name "ubuntu"
git config user.email > /dev/null || git config --global user.email "ubuntu@`hostname`"
git config color.ui || git config --global color.ui false
