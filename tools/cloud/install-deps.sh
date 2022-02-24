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

# Install Python 3.7 and pip
apt update && apt install -y python3.7 python3-setuptools curl
curl https://bootstrap.pypa.io/get-pip.py -o get-pip.py
python3.7 get-pip.py --user
rm get-pip.py

# Install pip packages
python3.7 -m pip install \
    wrapt==1.10.0 \
    plotly==5.3.1 \
    pandas==1.3.4 \
    cantools==36.4.0
