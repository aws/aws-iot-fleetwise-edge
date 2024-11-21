#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import argparse
import json

import pandas as pd
import plotly.graph_objects as go


def is_included(name, include_list, exclude_list):
    if include_list:
        for include in include_list:
            if include and include in name:
                break
        else:
            return False
    for exclude in exclude_list:
        if exclude and exclude in name:
            return False
    return True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Creates plots for collected IoT topic data")
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
    parser.add_argument(
        "--include-signals",
        metavar="SIGNAL_LIST",
        help="Comma separated list of signals to include",
    )
    parser.add_argument(
        "--exclude-signals",
        metavar="SIGNAL_LIST",
        help="Comma separated list of signals to exclude",
    )

    args = parser.parse_args()

    df = pd.DataFrame()
    include_list = (
        [i.strip() for i in args.include_signals.split(",")] if args.include_signals else []
    )
    exclude_list = (
        [i.strip() for i in args.exclude_signals.split(",")] if args.exclude_signals else []
    )

    for file in args.files:
        try:
            with open(file.name) as fp:
                data = json.load(fp)

            for row in data:
                if row["vehicleName"] != args.vehicle_name:
                    continue

                for measure_name, measure_value in row["signals"].items():
                    if not is_included(measure_name, include_list, exclude_list):
                        continue
                    for signal in measure_value:
                        timestamp = signal["time"]
                        df.at[timestamp, measure_name] = signal["value"]
        except Exception as e:
            raise Exception(str(e) + f" in {file.name}")

    if df.empty:
        raise Exception("No data found")
    df["time"] = pd.to_datetime(df.index, unit="ms")
    fig = go.Figure()
    for column in df:
        if column != "time":
            fig.add_trace(go.Scatter(x=df["time"], y=df[column], mode="markers", name=column))

    with open(args.html_filename, "w") as fp:
        fp.write(fig.to_html())
