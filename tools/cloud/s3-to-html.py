#!/usr/bin/python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import os

import boto3
import pandas as pd
import plotly.graph_objects as go


def plot_df(df, filename=None):
    fig = go.Figure()
    for measurement, group in df.groupby("measure_name"):
        fig.add_trace(
            go.Scatter(
                x=group["time"], y=group["measure_value_DOUBLE"], name=measurement, mode="markers"
            )
        )
    if filename is None:
        fig.show()
    else:
        with open(filename, "w") as fp:
            fp.write(fig.to_html())


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Creates plots for collected data from S3")
    parser.add_argument(
        "--bucket",
        required=True,
        help="S3 bucket name where the data was uploaded",
    )
    parser.add_argument(
        "--prefix",
        required=True,
        help="S3 object prefix where the data was uploaded",
    )
    parser.add_argument(
        "--json-output-filename",
        metavar="FILE",
        help="Filename to plot results collected in the JSON format",
    )
    parser.add_argument(
        "--parquet-output-filename",
        metavar="FILE",
        help="Filename to plot results collected in the parquet format",
    )

    args = parser.parse_args()

    json_dfs = []
    parquet_df = pd.DataFrame()

    s3_client = boto3.client("s3")
    files = s3_client.list_objects(Bucket=args.bucket, Prefix=args.prefix + "/processed-data/")
    if "Contents" in files:
        for file in files["Contents"]:
            results_file = "./" + os.path.basename(file["Key"])
            s3_client.download_file(args.bucket, file["Key"], results_file)

            if results_file.endswith(".json"):
                with open(results_file) as f:
                    lines = f.readlines()
                    for line in lines:
                        data = json.loads(line)
                        json_dfs.append(pd.DataFrame([data]))
            else:
                new_parquet = pd.read_parquet(results_file)
                parquet_df = pd.concat([parquet_df, pd.read_parquet(results_file)])

    json_df = pd.concat(json_dfs, ignore_index=True, sort=False)
    json_df["time"] = pd.to_datetime(json_df["time"], unit="ms")
    parquet_df["time"] = pd.to_datetime(parquet_df["time"], unit="ms")

    plot_df(json_df, args.json_output_filename)
    plot_df(parquet_df, args.parquet_output_filename)
