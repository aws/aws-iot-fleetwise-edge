#!/usr/bin/python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import sys

import pandas as pd
import plotly.graph_objects as go

if len(sys.argv) < 2:
    print("Usage: python3 " + sys.argv[0] + " <TIMESTREAM_RESULT_JSON_FILE> [<OUTPUT_HTML_FILE>]")
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
        if column not in columns or "ScalarValue" not in row["Data"][columns[column]]
        else row["Data"][columns[column]]["ScalarValue"]
    )


df = pd.DataFrame()
for row in data["Rows"]:
    ts = get_val(row, "time")
    signal_name = get_val(row, "measure_name")
    val = get_val(row, "measure_value::double")
    if val is None:
        val = get_val(row, "measure_value::bigint")
    if val is None:
        val = get_val(row, "measure_value::boolean") != "false"
    df.at[ts, signal_name] = float(val)

fig = go.Figure()
for column in df.columns:
    fig.add_trace(go.Scatter(x=df.index, y=df[column], mode="markers", name=column))

if len(sys.argv) < 3:
    fig.show()
else:
    with open(sys.argv[2], "w") as fp:
        fp.write(fig.to_html())
