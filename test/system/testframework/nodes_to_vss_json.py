#!/usr/bin/python3
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json

PRIMITIVE_TYPES_TO_VSS = {
    "INT8": "int8",
    "INT16": "int16",
    "INT32": "int32",
    "INT64": "int64",
    "UINT8": "uint8",
    "UINT16": "uint16",
    "UINT32": "uint32",
    "UINT64": "uint64",
    "BOOLEAN": "boolean",
    "FLOAT": "float",
    "DOUBLE": "double",
    "STRING": "string",
}


def convert_nodes_to_vss(nodes):
    vss_converted_types = []
    vss_converted_sensor = []
    for n in nodes:
        vss_type = list(n.keys())[0]
        fqn = n[vss_type]["fullyQualifiedName"]
        vss = {fqn: {"type": vss_type}}
        if "dataType" in n[vss_type]:
            is_array = False
            datatype = n[vss_type]["dataType"]
            if datatype.endswith("_ARRAY"):
                is_array = True
                datatype = datatype[0:-6]  # Remove _ARRAY
            if n[vss_type]["dataType"].startswith("STRUCT"):
                vss[fqn]["datatype"] = n[vss_type]["structFullyQualifiedName"]
            else:
                vss[fqn]["datatype"] = PRIMITIVE_TYPES_TO_VSS[datatype]
            if is_array:
                vss[fqn]["datatype"] = vss[fqn]["datatype"] + "[]"
        if "dataEncoding" in n[vss_type]:
            vss[fqn]["dataEncoding"] = (
                "binary" if n[vss_type]["dataEncoding"] == "BINARY" else "typed"
            )
        if "description" in n[vss_type]:
            vss[fqn]["description"] = n[vss_type]["description"]
        if vss_type == "sensor":
            vss_converted_sensor.append(vss)
        else:
            vss_converted_types.append(vss)
    vss_nodes = []
    for n in vss_converted_types:
        fqn = list(n.keys())[0]
        parent_fqn = fqn.rpartition(".")[0]
        parent = list(filter(lambda p: list(p.keys())[0] == parent_fqn, vss_converted_types))
        if len(parent) > 0:
            for p in parent:
                if "children" in list(p[list(p.keys())[0]]):
                    p[list(p.keys())[0]]["children"].append(n)
                else:
                    p[list(p.keys())[0]]["children"] = [n]
        else:
            vss_nodes.append(n)
    # remove top level branches without children
    vss_nodes[:] = [
        n
        for n in vss_nodes
        if not (
            n[list(n.keys())[0]]["type"] == "branch" and not n[list(n.keys())[0]].get("children")
        )
    ]

    output = {}
    # create missing branches for sensor
    for s in vss_converted_sensor:
        sensor_fqn = list(s.keys())[0]
        branches = sensor_fqn.split(".")
        current_node = output
        for i in range(len(branches) - 1):
            if branches[i] not in current_node:
                current_node[branches[i]] = {"type": "branch"}
            if "children" not in current_node[branches[i]]:
                current_node[branches[i]]["children"] = {}
            current_node = current_node[branches[i]]["children"]
        current_node.update({branches[-1]: s[sensor_fqn]})
    output.update({"ComplexDataTypes": remove_vss_fqn_prefix(vss_nodes)})
    return output


def remove_vss_fqn_prefix(nodes):
    output_dict = {}
    for n in nodes:
        fqn = list(n.keys())[0]
        last_part_fqn = fqn.rpartition(".")[2]
        if "children" not in n[fqn]:
            output_dict.update({last_part_fqn: n[fqn]})
        else:
            new_node = n[fqn]
            new_node["children"] = remove_vss_fqn_prefix(n[fqn]["children"])
            output_dict.update({last_part_fqn: new_node})
    return output_dict


if __name__ == "__main__":
    import argparse

    default_output_filename = "vision-system-data-signal-catalog-vss.json"
    parser = argparse.ArgumentParser(
        description="""Converts the json signal catalog nodes for example the output of
        ros2-to-nodes.py to vss json which can be used for ImportSignalCatalog"""
    )
    parser.add_argument(
        "-n",
        "--nodes-json",
        metavar="FILE",
        required=True,
        help="json signal catalog nodes output for example the output of ros2-to-nodes.py",
    )
    parser.add_argument(
        "-o",
        "--output",
        metavar="FILE",
        default=default_output_filename,
        help=f"Output filename, default: {default_output_filename}",
    )

    args = parser.parse_args()
    with open(args.nodes_json) as fp:
        nodes_json = json.load(fp)
        vss_json = convert_nodes_to_vss(nodes_json)
        with open(args.output, "w") as output:
            json.dump(vss_json, output, indent=2)
