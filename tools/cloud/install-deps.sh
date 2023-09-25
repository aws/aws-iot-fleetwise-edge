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
pip3 install \
    wrapt==1.10.0 \
    plotly==5.3.1 \
    pandas==1.3.5 \
    cantools==36.4.0 \
    fastparquet==0.8.1
