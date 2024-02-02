#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import sys
import logging

import cantools


FQN_KEY = "fullyQualifiedName"

logging.basicConfig(filename='./dbc-to-nodes.log', level=logging.DEBUG)

if len(sys.argv) < 2:
    logging.error("Usage: python3 " + sys.argv[0] + " <INPUT_DBC_FILE> [<OUTPUT_JSON_FILE>]")
    exit(-1)

dbc_file = sys.argv[1]
db = cantools.database.load_file(dbc_file, strict=False)

vehicle_branch = {FQN_KEY: "Vehicle"}
processed_node = set() # deduplicate branch and siganl node

nodes = []
for message in db.messages:
    message_text = message.name if message.name else message.frame_id
    message_branch_path = f"{vehicle_branch[FQN_KEY]}.{message_text}"
    message_branch_node = {"branch": {FQN_KEY: message_branch_path}}

    if not message_branch_path in processed_node:
        nodes.append(message_branch_node)
        processed_node.add(message_branch_path)
    else:
        logging.warning(
            f"Branch {message_text} occurs multiple times in the message, only"
            " the first occurrence will be used"
        )

    for signal in message.signals:
        signal_path = f"{message_branch_path}.{signal.name}"
        if not signal_path in processed_node:
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
                    FQN_KEY: signal_path,
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
            processed_node.add(signal_path)
            
        else:
            logging.warning(
                f"Signal {signal.name} occurs multiple times in the message {message_text}, only"
                " the first occurrence will be used"
            )


out = json.dumps(nodes, indent=4, sort_keys=True)

if len(sys.argv) < 3:
    print(out)
else:
    with open(sys.argv[2], "w") as fp:
        fp.write(out)
