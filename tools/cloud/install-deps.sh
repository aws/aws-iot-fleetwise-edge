#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -eo pipefail

# Install Python 3 and pip
apt update && apt install -y python3 python3-pip

# Install pip packages
pip3 install \
    wrapt==1.10.0 \
    plotly==5.3.1 \
    pandas==1.3.4 \
    cantools==36.4.0
