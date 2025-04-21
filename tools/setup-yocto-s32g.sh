#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -eo pipefail

# 'repo' requires that Git is configured, otherwise it interactively prompts for the configuration:
git config user.name > /dev/null || git config --global user.name "ubuntu"
git config user.email > /dev/null || git config --global user.email "ubuntu@`hostname`"
git config color.ui || git config --global color.ui false

python3  $(which repo) init -b release/bsp38.0 -u https://github.com/nxp-auto-linux/auto_yocto_bsp.git
python3  $(which repo) sync

SCRIPTPATH=`dirname $0`
cp -r ${SCRIPTPATH}/yocto/* .
