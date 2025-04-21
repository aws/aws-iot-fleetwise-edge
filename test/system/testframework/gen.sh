#!/bin/bash
# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

set -e

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
mkdir -p ${SCRIPT_DIR}/gen
protoc -I=${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/edgeToCloud --python_out ${SCRIPT_DIR}/gen ${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/edgeToCloud/checkin.proto
protoc -I=${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/edgeToCloud --python_out ${SCRIPT_DIR}/gen ${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/edgeToCloud/vehicle_data.proto
protoc -I=${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/edgeToCloud --python_out ${SCRIPT_DIR}/gen ${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/edgeToCloud/command_response.proto
protoc -I=${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/edgeToCloud --python_out ${SCRIPT_DIR}/gen ${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/edgeToCloud/last_known_state_data.proto
protoc -I=${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/cloudToEdge --python_out ${SCRIPT_DIR}/gen ${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/cloudToEdge/common_types.proto
protoc -I=${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/cloudToEdge --python_out ${SCRIPT_DIR}/gen ${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/cloudToEdge/decoder_manifest.proto
protoc -I=${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/cloudToEdge --python_out ${SCRIPT_DIR}/gen ${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/cloudToEdge/collection_schemes.proto
protoc -I=${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/cloudToEdge --python_out ${SCRIPT_DIR}/gen ${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/cloudToEdge/command_request.proto
protoc -I=${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/cloudToEdge --python_out ${SCRIPT_DIR}/gen ${SCRIPT_DIR}/../../../interfaces/protobuf/schemas/cloudToEdge/state_templates.proto
protoc -I=${SCRIPT_DIR} --python_out ${SCRIPT_DIR}/gen ${SCRIPT_DIR}/collection_schemes_incorrect_format.proto
if [ ${AMENT_PREFIX_PATH+x} ]; then
    python3 ${SCRIPT_DIR}/../../../tools/cloud/ros2-to-decoders.py --config ${SCRIPT_DIR}/ros2-config.json --output ${SCRIPT_DIR}/gen/vision-system-data-decoder-manifest.json
fi
