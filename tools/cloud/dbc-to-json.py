#!/usr/bin/python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import sys

import cantools

if len(sys.argv) < 2:
    print("Usage: python3 " + sys.argv[0] + " <INPUT_DBC_FILE> [<OUTPUT_JSON_FILE>]")
    exit(-1)

db = cantools.database.load_file(sys.argv[1])

with open("network-interfaces.json") as f:
    network_interfaces = json.load(f)
    for interface in network_interfaces:
        if interface["type"] == "CAN_INTERFACE":
            interface_id = interface["interfaceId"]

signal_decoders_to_add = []

for message in db.messages:
    for signal in message.signals:
        signal_to_add = {}
        signal_to_add["name"] = signal.name
        signal_to_add["factor"] = signal.scale
        signal_to_add["isBigEndian"] = signal.byte_order == "big_endian"
        signal_to_add["isSigned"] = signal.is_signed
        signal_to_add["length"] = signal.length
        signal_to_add["offset"] = signal.offset
        signal_to_add["messageId"] = message.frame_id

        # In a DBC file, the start bit indicates the LSB for little endian and MSB for big endian
        # signals. AWS IoT Fleetwise considers start bit to always be the LSB regardless of
        # endianess. That is why we need to convert the value obtained from DBC.
        if signal.byte_order == "big_endian":
            pos = 7 - (signal.start % 8) + (signal.length - 1)
            if pos < 8:
                signal_to_add["startBit"] = signal.start - signal.length + 1
            else:
                byte_count = int(pos / 8)
                signal_to_add["startBit"] = int(
                    7 - (pos % 8) + (byte_count * 8) + int(signal.start / 8) * 8
                )
        else:
            signal_to_add["startBit"] = signal.start

        signal_decoders_to_add.append(
            {
                "type": "CAN_SIGNAL",
                "canSignal": signal_to_add,
                "fullyQualifiedName": f"Vehicle.{message.name}.{signal.name}",
                "interfaceId": interface_id,
            }
        )

out = json.dumps(signal_decoders_to_add, indent=4, sort_keys=True)

if len(sys.argv) < 3:
    print(out)
else:
    with open(sys.argv[2], "w") as fp:
        fp.write(out)
