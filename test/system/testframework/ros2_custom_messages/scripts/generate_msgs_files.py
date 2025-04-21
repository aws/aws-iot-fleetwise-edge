# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import os

number_of_nested = 45
types_ros2_config = []

# NestedTypes more than 50 json depths can cause coral problems
for x in range(number_of_nested):
    typename = f"LoadTestNested{x:02}"
    types_ros2_config.append(typename)
    with open(os.path.dirname(__file__) + "../msg/" + typename + ".msg", "w") as f:
        f.write(f"int32 normal_int{x:02}\nfloat32[] float_array{x:02}\n")
        if x < number_of_nested - 1:
            f.write(f"LoadTestNested{x+1:02} nested_child{x+1:02}")


number_of_different_types = 500

# They all share the same structure so CDRs are structured the same for all
# as field name is not part of CDR
for x in range(number_of_different_types):
    typename = f"LoadTestDifferentType{x:02}"
    types_ros2_config.append(typename)
    with open(os.path.dirname(__file__) + "../msg/" + typename + ".msg", "w") as f:
        f.write(f"int32 different_int32field{x:02}\n")
        f.write(f"int64 different_int64field{x:02}\n")
        f.write(f"int16 different_int16field{x:02}\n")
        f.write(f"uint16 different_uint16field{x:02}\n")


ros2_config = {"messages": []}
for t in types_ros2_config:
    print('  "msg/' + t + '.msg"')  # These need to be added to CMakeLists.txt set(msg_files
    ros2_config["messages"].append(
        {
            "fullyQualifiedName": "Vehicle." + t + "Topic",
            "interfaceId": "3",
            "topic": t + "Topic",
            "type": "ros2_custom_messages/msg/" + t,
        }
    )

with open(os.path.dirname(__file__) + "../../ros2-loadtest-config.json", "w") as f:
    f.write(json.dumps(ros2_config, indent=2))
