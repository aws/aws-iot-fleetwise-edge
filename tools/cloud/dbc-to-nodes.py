#!/usr/bin/python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import sys

import cantools

FQN_KEY = "fullyQualifiedName"

if len(sys.argv) < 2:
    print("Usage: python3 " + sys.argv[0] + " <INPUT_DBC_FILE> [<OUTPUT_JSON_FILE>]")
    exit(-1)

db = cantools.database.load_file(sys.argv[1])

vehicle_branch = {FQN_KEY: "Vehicle"}
nodes = []

signals = {}
for message in db.messages:
    message_text = message.name if message.name else message.frame_id
    message_branch = {FQN_KEY: f"{vehicle_branch[FQN_KEY]}.{message_text}"}
    nodes.append({"branch": message_branch})
    for signal in message.signals:
        if message_text not in signals:
            signals[message_text] = set()
        if signal.name in signals[message_text]:
            print(
                f"Signal {signal.name} occurs multiple times in the message {message_text}, only"
                " the first occurrence will be used"
            )
            continue
        signals[message_text].add(signal.name)
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

if len(sys.argv) < 3:
    print(out)
else:
    with open(sys.argv[2], "w") as fp:
        fp.write(out)
