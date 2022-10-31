#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

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
