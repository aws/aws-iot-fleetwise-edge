#!/usr/bin/python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import sys
import cantools
import json

if len(sys.argv) < 2:
    print("Usage: python3 "+sys.argv[0]+" <INPUT_DBC_FILE> [<OUTPUT_JSON_FILE>]")
    exit(-1)

db = cantools.database.load_file(sys.argv[1])

nodes = []
signals = []
for message in db.messages:
    for signal in message.signals:
        if signal.name in signals:
            print(f"Signal {signal.name} occurs multiple times in the DBC file, only the first occurrence will be used")
            continue
        signals.append(signal.name)

        if signal.choices and len(signal.choices) <= 2 \
            and ((0 in signal.choices and str(signal.choices[0]).lower() == "false") \
            or (1 in signal.choices and str(signal.choices[1]).lower() == "true")):
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
                "fullyQualifiedName": "Vehicle."+signal.name,
            }
        }
        if signal.comment:
            node["sensor"]["description"] = signal.comment
        if signal.unit:
            node["sensor"]["unit"] = signal.unit
        if not signal.minimum is None and datatype != "BOOLEAN":
            node["sensor"]["min"] = signal.minimum
        if not signal.maximum is None and datatype != "BOOLEAN":
            node["sensor"]["max"] = signal.maximum
        nodes.append(node)

out = json.dumps(nodes, indent=4, sort_keys=True)

if len(sys.argv) < 3:
    print(out)
else:
    with open(sys.argv[2], "w") as fp:
        fp.write(out)
