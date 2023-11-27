#!/usr/bin/env python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import importlib
import json
import re


class Ros2SignalCatalog:
    PRIMITIVE_TYPES = {
        "boolean": "BOOLEAN",
        "float": "FLOAT",
        "double": "DOUBLE",
        "int8": "INT8",
        "int16": "INT16",
        "int32": "INT32",
        "int64": "INT64",
        "uint8": "UINT8",
        "uint16": "UINT16",
        "uint32": "UINT32",
        "uint64": "UINT64",
        "byte": "UINT8",
        "char": "UINT8",
        "string": "STRING",
        "wstring": "STRING",
        "octet": "UINT8",
    }

    def __init__(self, config):
        self.nodes = [{"branch": {"fullyQualifiedName": "Types"}}]
        self.branches = []
        self.types = []
        for message in config["messages"]:
            self.expand_ros2_message(message["type"])
            branches = message["fullyQualifiedName"].split(".")
            fqn = ""
            for i in range(len(branches) - 1):
                if fqn != "":
                    fqn += "."
                fqn += branches[i]
                if fqn not in self.branches:
                    self.branches.append(fqn)
                    node = {"branch": {"fullyQualifiedName": fqn, "description": fqn}}
                    self.nodes.append(node)
            type_fqn = f"Types.{self.sanitize(message['type'])}"
            self.nodes.append(
                {
                    "sensor": {
                        "fullyQualifiedName": message["fullyQualifiedName"],
                        "dataType": "STRUCT",
                        "structFullyQualifiedName": type_fqn,
                    }
                }
            )

    def sanitize(self, message_type):
        return re.sub("[^a-zA-Z0-9]", "_", message_type)

    def expand_ros2_message(self, message_type, fqn=None):
        if message_type in self.PRIMITIVE_TYPES:
            self.nodes.append(
                {
                    "property": {
                        "fullyQualifiedName": fqn,
                        "dataType": self.PRIMITIVE_TYPES[message_type],
                        "dataEncoding": "TYPED",
                    }
                }
            )
            return
        # Limited length strings:
        fixed_string_match = re.match(r"^((|w)string)<(\d+)>$", message_type)
        if fixed_string_match:
            member_type = fixed_string_match.group(1)
            self.nodes.append(
                {
                    "property": {
                        "fullyQualifiedName": fqn,
                        "dataType": self.PRIMITIVE_TYPES[member_type],
                        "dataEncoding": "TYPED",
                    }
                }
            )
            return
        # Unlimited or limited length lists:
        list_match = re.match(r"^sequence<([\w/]+)(|, (\d+))>$", message_type)
        if list_match:
            member_type = list_match.group(1)
        else:
            # Fixed length lists:
            list_match = re.match(r"^([\w/]+)\[(\d+)\]$", message_type)
            if list_match:
                member_type = list_match.group(1)
        if list_match:
            if member_type in self.PRIMITIVE_TYPES:
                self.nodes.append(
                    {
                        "property": {
                            "fullyQualifiedName": fqn,
                            "dataType": f"{self.PRIMITIVE_TYPES[member_type]}_ARRAY",
                            "dataEncoding": "BINARY"
                            if self.PRIMITIVE_TYPES[member_type] == "UINT8"
                            else "TYPED",
                        }
                    }
                )
                return
            if "/" in member_type:
                self.expand_ros2_message(member_type)
                type_fqn = f"Types.{self.sanitize(member_type)}"
                self.nodes.append(
                    {
                        "property": {
                            "fullyQualifiedName": fqn,
                            "dataType": "STRUCT_ARRAY",
                            "structFullyQualifiedName": type_fqn,
                        }
                    }
                )
                return
            raise Exception("Unknown message type " + member_type)
        if "/" in message_type:
            if message_type not in self.types:
                self.types.append(message_type)
                self.nodes.append(
                    {"struct": {"fullyQualifiedName": f"Types.{self.sanitize(message_type)}"}}
                )
                message_info = message_type.split("/")
                module_name = message_info[0] + ".msg"
                message_type_name = message_info[-1]
                module = importlib.import_module(module_name)
                message = getattr(module, message_type_name)()
                fields = message.get_fields_and_field_types()
                for field_name, field_type in fields.items():
                    self.expand_ros2_message(
                        field_type,
                        f"Types.{self.sanitize(message_type)}.{field_name}",
                    )
            if fqn:
                type_fqn = f"Types.{self.sanitize(message_type)}"
                self.nodes.append(
                    {
                        "property": {
                            "fullyQualifiedName": fqn,
                            "dataType": "STRUCT",
                            "structFullyQualifiedName": type_fqn,
                        }
                    }
                )
            return
        raise Exception("Unknown message type " + message_type)


if __name__ == "__main__":
    import argparse

    default_output_filename = "vision-system-data-signal-catalog.json"
    parser = argparse.ArgumentParser(
        description="Converts ROS2 message information to AWS IoT FleetWise JSON nodes format"
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
    r2sc = Ros2SignalCatalog(config)
    with open(args.output, "w") as fp:
        json.dump(r2sc.nodes, fp, indent=2)
