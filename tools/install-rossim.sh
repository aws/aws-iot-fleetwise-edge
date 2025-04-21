#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -eo pipefail

# Install ROS2 Simulator
cp -r tools/rossim /usr/share
cp tools/rossim/rossim.service /lib/systemd/system/

# Download Rosbag
aws s3 cp s3://aws-iot-fleetwise/rosbag2_vision_system_data_demo.db3 /usr/share/rossim

systemctl daemon-reload
systemctl enable rossim
systemctl start rossim

# Check that the simulator started correctly and is generating data
source /opt/ros/humble/setup.bash
if ! timeout 5s bash -c "while ! ros2 topic list | grep -q -v -E '(/parameter_events|/rosout)'; do sleep 1; done"; then
    echo "No ROS2 messages are being sent by the simuator"
    exit 1
fi
