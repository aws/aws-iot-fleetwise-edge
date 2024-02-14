#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import argparse
import json
import sys

import cantools

default_interface_id = "1"
parser = argparse.ArgumentParser(
    description=(
        "Converts a DBC file to AWS IoT FleetWise 'decoders' format for use with "
        "CreateDecoderManifest"
    )
)
parser.add_argument(
    "-p",
    "--permissive",
    action="store_true",
    help="Apply the cantools strict=False option when loading the DBC file",
)
parser.add_argument(
    "-i",
    "--interface-id",
    default=default_interface_id,
    help=(
        f'Network interface ID, default "{default_interface_id}". This must match the ID used in '
        "the static config file."
    ),
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

signal_decoders_to_add = []

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
    processed_signals = set()
    for signal in message.signals:
        signal_fqn = f"Vehicle.{message_text}.{signal.name}"
        if signal.name in processed_signals:
            print(
                f"Signal {signal.name} occurs multiple times in the message {message_text}, only"
                " the first occurrence will be used",
                file=sys.stderr,
            )
            continue
        processed_signals.add(signal.name)
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
                "fullyQualifiedName": signal_fqn,
                "interfaceId": args.interface_id,
            }
        )

out = json.dumps(signal_decoders_to_add, indent=4, sort_keys=True)

args.outfile.write(out)
