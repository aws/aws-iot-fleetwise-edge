#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -eo pipefail

source /opt/ros/humble/setup.bash
ros2 bag play --loop rosbag2_vision_system_data_demo.db3
