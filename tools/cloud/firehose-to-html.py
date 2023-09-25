#!/usr/bin/python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import argparse
import json

import pandas as pd
import plotly.graph_objects as go


def process_row(vehicle_name, row, data):
    expanded_data = {"time": row["time"]}
    if "measure_name" not in row or "time" not in row or "vehicleName" not in row:
        raise Exception("Unsupported format")
    if row["vehicleName"] != vehicle_name:
        return
    if "measure_value_DOUBLE" in row:
        expanded_data[row["measure_name"]] = row["measure_value_DOUBLE"]
    else:
        raise Exception("Unsupported format")
    data.append(expanded_data)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Creates plots for collected data from Firehose")
    parser.add_argument(
        "--vehicle-name",
        required=True,
        help="Vehicle name",
    )
    parser.add_argument(
        "--files",
        type=argparse.FileType("r"),
        nargs="+",
        required=True,
        help="List files to process",
    )
    parser.add_argument(
        "--html-filename",
        metavar="FILE",
        required=True,
        help="HTML output filename",
    )

    args = parser.parse_args()

    data = []
    for file in args.files:
        try:
            if file.name.endswith(".json"):
                with open(file.name) as fp:
                    for line in fp:
                        row = json.loads(line)
                        process_row(args.vehicle_name, row, data)
            elif file.name.endswith(".parquet"):
                df = pd.read_parquet(file.name)
                for _, row in df.iterrows():
                    process_row(args.vehicle_name, row, data)
            else:
                raise Exception("Unsupported format")
        except Exception as e:
            raise Exception(e.message + f" in {file.name}")

    if len(data) == 0:
        raise Exception("No data found")
    df = pd.DataFrame(data)
    df["time"] = pd.to_datetime(df["time"], unit="ms")
    fig = go.Figure()
    for column in df:
        if column != "time":
            fig.add_trace(go.Scatter(x=df["time"], y=df[column], mode="markers", name=column))
    with open(args.html_filename, "w") as fp:
        fp.write(fig.to_html())
