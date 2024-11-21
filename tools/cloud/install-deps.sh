#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -eo pipefail

# On Ubuntu install Python 3 and pip
if command -v apt &> /dev/null; then
    apt update
    apt install -y python3 python3-pip
fi

# Install pip packages
# Note: CloudShell currently only supports pyarrow 12.0.1

python3 -m pip install importlib_metadata==8.5.0

python3 -m pip install \
    wrapt==1.10.0 \
    plotly==5.3.1 \
    pandas==1.3.5 \
    cantools==36.4.0 \
    pyarrow==12.0.1 \
    boto3==1.18.60 \
    protobuf==3.20.2 \
    awsiotsdk==1.17.0 \
    packaging==20.3
