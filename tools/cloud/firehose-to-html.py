#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import math

import numpy
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


def expand(struct_data, expanded_data, name_prefix, s3_links, include_list, exclude_list):
    if type(struct_data) == dict:
        for key, value in struct_data.items():
            expand(
                value, expanded_data, name_prefix + "." + key, s3_links, include_list, exclude_list
            )
    elif type(struct_data) in [list, numpy.ndarray]:
        for i in range(len(struct_data)):
            expand(
                struct_data[i],
                expanded_data,
                f"{name_prefix}[{i}]",
                s3_links,
                include_list,
                exclude_list,
            )
    elif not is_included(name_prefix, include_list, exclude_list):
        return
    elif type(struct_data) == str:
        if "s3://" in struct_data:
            s3_links.append(struct_data)
    else:
        expanded_data[name_prefix] = float(struct_data)


def process_row(vehicle_name, row, data, s3_links, include_list, exclude_list):
    expanded_data = {"time": row["time"]}
    if "measure_name" not in row or "time" not in row or "vehicleName" not in row:
        raise Exception("Unsupported format")
    if row["vehicleName"] != vehicle_name:
        return
    if "measure_value_BOOLEAN" in row:
        if not is_included(row["measure_name"], include_list, exclude_list):
            return
        expanded_data[row["measure_name"]] = 1 if row["measure_value_BOOLEAN"] else 0
    elif "measure_value_DOUBLE" in row and not math.isnan(row["measure_value_DOUBLE"]):
        if not is_included(row["measure_name"], include_list, exclude_list):
            return
        expanded_data[row["measure_name"]] = row["measure_value_DOUBLE"]
    elif "measure_value_BIGINT" in row:
        if not is_included(row["measure_name"], include_list, exclude_list):
            return
        expanded_data[row["measure_name"]] = row["measure_value_BIGINT"]
    elif "measure_value_VARCHAR" in row:
        if not is_included(row["measure_name"], include_list, exclude_list):
            return
        expanded_data[row["measure_name"]] = row["measure_value_VARCHAR"]
    elif "measure_value_STRUCT" in row and row["measure_value_STRUCT"] is not None:
        for struct_data in row["measure_value_STRUCT"].values():
            if struct_data is None:
                continue
            expand(
                struct_data,
                expanded_data,
                row["measure_name"],
                s3_links,
                include_list,
                exclude_list,
            )
            break
    else:
        raise Exception(f"Unsupported format: {row}")
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
    parser.add_argument(
        "--s3-links-filename",
        metavar="FILE",
        help="S3 links output filename",
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

    data = []
    s3_links = []
    include_list = (
        [i.strip() for i in args.include_signals.split(",")] if args.include_signals else []
    )
    exclude_list = (
        [i.strip() for i in args.exclude_signals.split(",")] if args.exclude_signals else []
    )
    for file in args.files:
        try:
            if file.name.endswith(".json"):
                with open(file.name) as fp:
                    for line in fp:
                        row = json.loads(line)
                        process_row(
                            args.vehicle_name, row, data, s3_links, include_list, exclude_list
                        )
            elif file.name.endswith(".parquet"):
                df = pd.read_parquet(file.name, engine="pyarrow")
                for _, row in df.iterrows():
                    process_row(args.vehicle_name, row, data, s3_links, include_list, exclude_list)
            else:
                raise Exception("Unsupported format")
        except Exception as e:
            raise Exception(str(e) + f" in {file.name}")

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
    if args.s3_links_filename:
        with open(args.s3_links_filename, "w") as fp:
            fp.write("\n".join(s3_links))
