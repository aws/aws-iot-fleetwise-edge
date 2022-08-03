#!/usr/bin/python3
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

import sys
import json
import plotly.graph_objects as go
import pandas as pd

if len(sys.argv) < 2:
    print("Usage: python3 "+sys.argv[0]+" <TIMESTREAM_RESULT_JSON_FILE> [<OUTPUT_HTML_FILE>]")
    exit(-1)

with open(sys.argv[1], 'r') as fp:
    data = json.load(fp)

columns={}
i=0
for column in data['ColumnInfo']:
    columns[column['Name']]=i
    i+=1

def get_val(row, column):
    return None if not column in columns or not 'ScalarValue' in row['Data'][columns[column]] \
        else row['Data'][columns[column]]['ScalarValue']

df = pd.DataFrame()
for row in data['Rows']:
    ts=get_val(row, 'time')
    signal_name=get_val(row, 'measure_name')
    val=get_val(row, 'measure_value::double')
    if val == None:
        val=get_val(row, 'measure_value::bigint')
    if val == None:
        val=get_val(row, 'measure_value::boolean') != 'false'
    df.at[ts, signal_name] = float(val)

fig = go.Figure()
for column in df.columns:
    fig.add_trace(go.Scatter(x=df.index, y=df[column], mode='markers', name=column))

if len(sys.argv) < 3:
    fig.show()
else:
    with open(sys.argv[2], "w") as fp:
        fp.write(fig.to_html())
