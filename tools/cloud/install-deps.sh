#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -eo pipefail

# On Ubuntu install Python 3 and pip
if command -v apt &> /dev/null; then
    apt-get update
    apt-get install -y python3 python3-pip
fi

# Install pip packages
# Note: CloudShell currently only supports pyarrow 12.0.1

python3 -m pip install importlib_metadata==8.5.0

python3 -m pip install \
    wrapt==1.10.0 \
    plotly==5.3.1 \
    pandas==2.1.4 \
    cantools==36.4.0 \
    pyarrow==12.0.1 \
    boto3==1.20.34 \
    protobuf==3.20.2 \
    awsiotsdk==1.17.0 \
    packaging==24.0
