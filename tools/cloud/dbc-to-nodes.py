#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import sys

import cantools

FQN_KEY = "fullyQualifiedName"

parser = argparse.ArgumentParser(
    description=(
        "Converts a DBC file to AWS IoT FleetWise 'nodes' format for use with "
        "CreateSignalCatalog"
    )
)
parser.add_argument(
    "-p",
    "--permissive",
    action="store_true",
    help="Apply the cantools strict=False option when loading the DBC file",
)
parser.add_argument(
    "infile",
    nargs="?",
    type=argparse.FileType("r"),
    default=sys.stdin,
    help="Input DBC file, default stdin",
)
parser.add_argument(
    "outfile",
    nargs="?",
    type=argparse.FileType("w"),
    default=sys.stdout,
    help="Output filename, default stdout",
)
args = parser.parse_args()

db = cantools.database.load(args.infile, strict=not args.permissive)

vehicle_branch = {FQN_KEY: "Vehicle"}
nodes = []

processed_messages = set()
for message in db.messages:
    message_text = message.name if message.name else message.frame_id
    if message_text in processed_messages:
        message_text = f"{message_text}_{message.frame_id}"
        if message_text in processed_messages:
            print(
                f"Message {message.frame_id} occurs multiple times, only the first occurrence "
                "will be used",
                file=sys.stderr,
            )
            continue
    processed_messages.add(message_text)
    message_branch = {FQN_KEY: f"{vehicle_branch[FQN_KEY]}.{message_text}"}
    nodes.append({"branch": message_branch})
    processed_signals = set()
    for signal in message.signals:
        if signal.name in processed_signals:
            print(
                f"Signal {signal.name} occurs multiple times in the message {message_text}, only"
                " the first occurrence will be used",
                file=sys.stderr,
            )
            continue
        processed_signals.add(signal.name)
        if (
            signal.choices
            and len(signal.choices) <= 2
            and (
                (0 in signal.choices and str(signal.choices[0]).lower() == "false")
                or (1 in signal.choices and str(signal.choices[1]).lower() == "true")
            )
        ):
            datatype = "BOOLEAN"
        elif signal.scale != 1 or signal.offset != 0 or signal.length > 64 or signal.is_float:
            datatype = "DOUBLE"
        elif signal.length <= 8:
            datatype = "INT8" if signal.is_signed else "UINT8"
        elif signal.length <= 16:
            datatype = "INT16" if signal.is_signed else "UINT16"
        elif signal.length <= 32:
            datatype = "INT32" if signal.is_signed else "UINT32"
        else:
            datatype = "INT64" if signal.is_signed else "UINT64"
        node = {
            "sensor": {
                "dataType": datatype,
                FQN_KEY: f"{message_branch[FQN_KEY]}.{signal.name}",
            }
        }
        if signal.comment:
            node["sensor"]["description"] = signal.comment
        if signal.unit:
            node["sensor"]["unit"] = signal.unit
        if signal.minimum is not None and datatype != "BOOLEAN":
            node["sensor"]["min"] = signal.minimum
        if signal.maximum is not None and datatype != "BOOLEAN":
            node["sensor"]["max"] = signal.maximum
        nodes.append(node)

out = json.dumps(nodes, indent=4, sort_keys=True)

args.outfile.write(out)
