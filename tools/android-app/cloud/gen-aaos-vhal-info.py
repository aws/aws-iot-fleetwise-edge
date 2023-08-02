# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import re

# Before running this script to generate the output files, firstly create a file called
# aaos-vhal-types.h with the content from:
# https://cs.android.com/android/platform/superproject/main/+/main:prebuilts/vndk/v30/x86/include/generated-headers/hardware/interfaces/automotive/vehicle/2.0/android.hardware.automotive.vehicle@2.0_genc++_headers/gen/android/hardware/automotive/vehicle/2.0/types.h
# The output in aaos-vhal-props-table.txt can then be copied to
# `tools/android-app/app/src/main/java/com/aws/iotfleetwise/AaosVehicleProperties.java`

with open("aaos-vhal-types.h") as fp:
    text = fp.read()

result_sizes = {
    "INFO_FUEL_TYPE": 4,
    "INFO_EV_CONNECTOR_TYPE": 4,
    "INFO_EXTERIOR_DIMENSIONS": 8,
    "INFO_MULTI_EV_PORT_LOCATIONS": 2,
    "WHEEL_TICK": 5,
    "HVAC_FAN_DIRECTION_AVAILABLE": 4,
    "AP_POWER_STATE_REQ": 2,
    "AP_POWER_STATE_REPORT": 2,
    "CLUSTER_DISPLAY_STATE": 9,
}

area_sizes = {
    "SEAT": 9,
    "WHEEL": 4,
    "WINDOW": 10,
    "DOOR": 8,
    "MIRROR": 3,
}

ignore = [
    "HW_ROTARY_INPUT",
    "HW_KEY_INPUT",
    "HW_CUSTOM_INPUT",
    "SUPPORT_CUSTOMIZE_VENDOR_PERMISSION",
    "EVS_SERVICE_REQUEST",
    "CLUSTER_SWITCH_UI",
    "CLUSTER_REQUEST_DISPLAY",
]

decoders = []
nodes = [
    {"branch": {"fullyQualifiedName": "Vehicle.VHAL", "description": "Android Automotive OS VHAL"}}
]
fqns = ""
matches = re.findall(
    r"    ([A-Z0-9_]+) = ([0-9]+) \/\*.+VehiclePropertyType:"
    r"([A-Z0-9_]+).+VehicleArea:([A-Z]+).+\*\/",
    text,
)
for match in matches:
    prop_name = match[0]
    prop_id = int(match[1])
    prop_type = match[2]
    prop_area = match[3]
    if prop_name == "INFO_DRIVER_SEAT":  # Special case for INFO_DRIVER_SEAT
        prop_area = "GLOBAL"
    if (
        prop_type not in ["BOOLEAN", "INT32", "FLOAT", "INT32_VEC", "INT64_VEC"]
        or prop_name in ignore
    ):
        print(f"Ignoring {prop_name}: {prop_id}, {prop_type}")
        continue
    if "VEC" in prop_type and prop_name not in result_sizes:
        print(f"Error: no size defined for {prop_name}")
        exit(-1)
    for area_index in [-1] if prop_area not in area_sizes else range(area_sizes[prop_area]):
        for result_index in (
            [-1] if prop_name not in result_sizes else range(result_sizes[prop_name])
        ):
            full_name = (
                prop_name
                + ("" if area_index == -1 else f"_{area_index}")
                + ("" if result_index == -1 else f"_{result_index}")
            )
            fqn = "Vehicle.VHAL." + full_name
            fqns += fqn + "\n"
            decoders.append(
                {
                    "fullyQualifiedName": fqn,
                    "type": "CAN_SIGNAL",
                    "interfaceId": "AAOS-VHAL-CAN",
                    "canSignal": {
                        "messageId": 1,
                        "isBigEndian": True,
                        "isSigned": True,
                        "startBit": (0 if area_index == -1 else area_index),
                        "offset": prop_id,
                        "factor": 1,
                        "length": (0 if result_index == -1 else result_index),
                    },
                }
            )
            nodes.append(
                {
                    "sensor": {
                        "dataType": "DOUBLE",
                        "fullyQualifiedName": fqn,
                        "description": full_name,
                    }
                }
            )
with open("aaosVhalDecoders.json", "w") as fp:
    json.dump(decoders, fp, indent=2)
with open("aaosVhalNodes.json", "w") as fp:
    json.dump(nodes, fp, indent=2)
with open("aaos-vhal-fqns.txt", "w") as fp:
    fp.write(fqns)
