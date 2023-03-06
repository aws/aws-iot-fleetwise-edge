#!/usr/bin/python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

# This script converts Timestream query result data to MDF format.
# To install the required dependencies, follow these instructions:
# 1. For hosts with `apt`:
#
#        sudo apt install -y build-essential python3-dev
#
# 2. For hosts with `yum`:
#
#        sudo yum groupinstall -y 'Development Tools'
#        sudo yum install -y python3-devel
#
# 3. Then install the following Python packages:
#
#        pip3 install pandas asammdf
#

import json
import sys

import asammdf
import pandas as pd

if len(sys.argv) < 3:
    print("Usage: python3 " + sys.argv[0] + " <TIMESTREAM_RESULT_JSON_FILE> <OUTPUT_MDF_FILE>")
    exit(-1)

with open(sys.argv[1]) as fp:
    data = json.load(fp)

columns = {}
i = 0
for column in data["ColumnInfo"]:
    columns[column["Name"]] = i
    i += 1


def get_val(row, column):
    return (
        None
        if "ScalarValue" not in row["Data"][columns[column]]
        else row["Data"][columns[column]]["ScalarValue"]
    )


df = pd.DataFrame()
for row in data["Rows"]:
    ts = get_val(row, "time")
    ts = pd.Timestamp(ts).value / 10**9
    signal_name = get_val(row, "measure_name")
    val = get_val(row, "measure_value::double")
    if val is None:
        val = get_val(row, "measure_value::bigint")
    df.at[ts, signal_name] = float(val)
df.sort_index(inplace=True)

with asammdf.MDF(version="4.10") as mdf4:
    mdf4.append(df)
    mdf4.save(sys.argv[2], overwrite=True)
