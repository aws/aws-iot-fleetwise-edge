#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import importlib
import json
import re


class Ros2DecoderManifest:
    PRIMITIVE_TYPES = {
        "boolean": "BOOL",
        "float": "FLOAT32",
        "double": "FLOAT64",
        "int8": "INT8",
        "int16": "INT16",
        "int32": "INT32",
        "int64": "INT64",
        "uint8": "UINT8",
        "uint16": "UINT16",
        "uint32": "UINT32",
        "uint64": "UINT64",
        "byte": "BYTE",
        "char": "CHAR",
        "string": "STRING",
        "wstring": "WSTRING",
        "octet": "BYTE",
    }

    def __init__(self, config):
        self.decoders = []
        for message in config["messages"]:
            self.decoders.append(
                {
                    "fullyQualifiedName": message["fullyQualifiedName"],
                    "type": "MESSAGE_SIGNAL",
                    "interfaceId": message["interfaceId"],
                    "messageSignal": {
                        "topicName": message["topic"] + ":" + message["type"],
                        "structuredMessage": self.expand_ros2_message(message["type"]),
                    },
                }
            )

    def expand_ros2_message(self, message_type):
        if message_type in self.PRIMITIVE_TYPES:
            return {
                "primitiveMessageDefinition": {
                    "ros2PrimitiveMessageDefinition": {
                        "primitiveType": self.PRIMITIVE_TYPES[message_type]
                    }
                }
            }
        # Limited length strings:
        fixed_string_match = re.match(r"^((|w)string)<(\d+)>$", message_type)
        if fixed_string_match:
            member_type = fixed_string_match.group(1)
            upper_bound = fixed_string_match.group(3)
            return {
                "primitiveMessageDefinition": {
                    "ros2PrimitiveMessageDefinition": {
                        "primitiveType": self.PRIMITIVE_TYPES[member_type],
                        "upperBound": upper_bound,
                    }
                }
            }
        # Unlimited or limited length lists:
        list_match = re.match(r"^sequence<([\w/]+)(|, (\d+))>$", message_type)
        if list_match:
            member_type = list_match.group(1)
            list_length = list_match.group(3)
            if not list_length:
                list_length = 0
                list_type = "DYNAMIC_UNBOUNDED_CAPACITY"
            else:
                list_length = int(list_length)
                list_type = "DYNAMIC_BOUNDED_CAPACITY"
        else:
            # Fixed length lists:
            list_match = re.match(r"^([\w/]+)\[(\d+)\]$", message_type)
            if list_match:
                list_type = "FIXED_CAPACITY"
                member_type = list_match.group(1)
                list_length = int(list_match.group(2))
        if list_match:
            return {
                "structuredMessageListDefinition": {
                    "name": "listType",
                    "memberType": self.expand_ros2_message(member_type),
                    "capacity": list_length,
                    "listType": list_type,
                }
            }
        if "/" in message_type:
            message_info = message_type.split("/")
            module_name = message_info[0] + ".msg"
            message_type_name = message_info[-1]
            module = importlib.import_module(module_name)
            message = getattr(module, message_type_name)()
            fields = message.get_fields_and_field_types()
            struct_def = {"structuredMessageDefinition": []}
            for field_name, field_type in fields.items():
                struct_def["structuredMessageDefinition"].append(
                    {
                        "fieldName": field_name,
                        "dataType": self.expand_ros2_message(field_type),
                    }
                )
            return struct_def
        raise Exception("Unknown message type " + message_type)


if __name__ == "__main__":
    import argparse

    default_output_filename = "vision-system-data-decoder-manifest.json"
    parser = argparse.ArgumentParser(
        description="Converts ROS2 message information to AWS IoT FleetWise JSON decoder format"
    )
    parser.add_argument("-c", "--config", metavar="FILE", required=True, help="Config filename")
    parser.add_argument(
        "-o",
        "--output",
        metavar="FILE",
        default=default_output_filename,
        help=f"Output filename, default: {default_output_filename}",
    )
    args = parser.parse_args()
    with open(args.config) as fp:
        config = json.load(fp)
    r2dm = Ros2DecoderManifest(config)
    with open(args.output, "w") as fp:
        json.dump(r2dm.decoders, fp, indent=2)
