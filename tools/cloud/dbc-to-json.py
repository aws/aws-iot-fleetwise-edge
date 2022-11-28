#!/usr/bin/python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import sys
import cantools
import json 

if len(sys.argv) < 2:
    print(
        "Usage: python3 " +
        sys.argv[0] +
        " <INPUT_DBC_FILE> [<OUTPUT_JSON_FILE>]")
    exit(-1)

db = cantools.database.load_file(sys.argv[1])

with open('network-interfaces.json', 'r') as f:
  network_interfaces = json.load(f)
  for interface in network_interfaces:
    if interface["type"] == "CAN_INTERFACE":
        interface_id = interface["interfaceId"]

signalDecodersToAdd = []

for message in db.messages:
    for signal in message.signals:
        signal_to_add = {}
        signal_to_add["name"] = signal.name
        signal_to_add["factor"] = signal.scale
        signal_to_add["isBigEndian"] = signal.byte_order == 'big_endian'
        signal_to_add["isSigned"] = signal.is_signed
        signal_to_add["length"] = signal.length
        signal_to_add["offset"] = signal.offset
        signal_to_add["startBit"] = signal.start
        signal_to_add["messageId"] = message.frame_id
        signalDecodersToAdd.append( 
            { 
                "type": "CAN_SIGNAL",
                "canSignal": signal_to_add,
                "fullyQualifiedName": "Vehicle.{}.{}".format(message.name, signal.name),
                "interfaceId": interface_id
            } 
        )

out = json.dumps(signalDecodersToAdd, indent=4, sort_keys=True)

if len(sys.argv) < 3:
    print(out)
else:
    with open(sys.argv[2], "w") as fp:
        fp.write(out)
