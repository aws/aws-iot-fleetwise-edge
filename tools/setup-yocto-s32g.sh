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

repo init -b release/bsp28.0 -u https://source.codeaurora.org/external/autobsps32/auto_yocto_bsp
repo sync

git clone git://git.openembedded.org/meta-python2 sources/meta-python2
cd sources/meta-python2 && git checkout c43c29e57f16af4e77441b201855321fbd546661 && cd ../..

SCRIPTPATH=`dirname $0`
cp -r ${SCRIPTPATH}/yocto/* .
