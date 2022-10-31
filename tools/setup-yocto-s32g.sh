#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -eo pipefail

repo init -b release/bsp28.0 -u https://source.codeaurora.org/external/autobsps32/auto_yocto_bsp
repo sync

git clone git://git.openembedded.org/meta-python2 sources/meta-python2
cd sources/meta-python2 && git checkout c43c29e57f16af4e77441b201855321fbd546661 && cd ../..

SCRIPTPATH=`dirname $0`
cp -r ${SCRIPTPATH}/yocto/* .
