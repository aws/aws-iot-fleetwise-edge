# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import time
from datetime import timedelta
from pathlib import Path
from random import Random

import pytest
from testframework.context import Context

log = logging.getLogger(__name__)


class TestLongAvgComplexData:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, request: pytest.FixtureRequest, worker_number: int):
        # Explicitly map test to metric to force us to set new metric string when adding new tests.
        test_name_to_metric_map = {
            self.test_long_avg_complex_data.__name__: "long_avg_test_complex_data"
        }
        metrics_name = test_name_to_metric_map[request.node.name]

        self.image_topic = f"ImageTopic{worker_number}"
        self.point_field_topic = f"PointFieldTopic{worker_number}"
        self.different_types_test_topic = f"DifferentTypesTestTopic{worker_number}"
        self.context_kwargs = {
            "ros2_enabled": True,
        }
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            **self.context_kwargs,
            background_metrics=True,
            metrics_name=metrics_name,
        )
        self.context.connect_to_cloud()

        self.context.start_cyclic_ros_messages()

        # Setting the initial values
        self.image_size_bytes = 5000000
        for i in range(0, self.image_size_bytes):
            self.context.rosigen.set_value([self.image_topic, "data", "[" + str(i) + "]"], i % 256)
        self.context.rosigen.set_value([self.image_topic, "height"], 222)
        self.context.rosigen.set_value([self.image_topic, "step"], 173)

        self.context.rosigen.set_value([self.point_field_topic, "offset"], 100)
        self.context.rosigen.set_value([self.point_field_topic, "datatype"], 1)
        self.context.rosigen.set_value([self.point_field_topic, "count"], 10)

        self.context.rosigen.set_value([self.different_types_test_topic, "value_bool"], True)
        self.context.rosigen.set_value([self.different_types_test_topic, "value_byte"], 1)
        self.context.rosigen.set_value([self.different_types_test_topic, "value_char"], 2)
        self.context.rosigen.set_value([self.different_types_test_topic, "value_float"], 3.4)
        self.context.rosigen.set_value([self.different_types_test_topic, "value_double"], 5.6)
        self.context.rosigen.set_value([self.different_types_test_topic, "value_int8"], -7)
        self.context.rosigen.set_value([self.different_types_test_topic, "value_uint8"], 8)
        self.context.rosigen.set_value([self.different_types_test_topic, "value_int16"], 574)
        self.context.rosigen.set_value([self.different_types_test_topic, "value_uint16"], 234)
        self.context.rosigen.set_value([self.different_types_test_topic, "value_int32"], -15000)
        self.context.rosigen.set_value([self.different_types_test_topic, "value_uint32"], 25789)
        self.context.rosigen.set_value([self.different_types_test_topic, "value_int64"], 87654326)
        self.context.rosigen.set_value([self.different_types_test_topic, "value_uint64"], 274748594)
        self.context.rosigen.set_value(
            [self.different_types_test_topic, "value_string"], "Hello FleetWise"
        )
        self.context.rosigen.set_value(
            [self.different_types_test_topic, "value_wstring"],
            "Hello FleetWise \U0001F01F",
        )
        self.context.rosigen.set_value(
            [self.different_types_test_topic, "nested_message", "nested_value_bool"],
            True,
        )
        self.context.rosigen.set_value(
            [
                self.different_types_test_topic,
                "nested_message",
                "nested_message_2",
                "nested_value_uint8",
            ],
            1,
        )
        self.random = Random(9837234)
        self.create_collection_schemes(0, self.random)

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_ros_messages()
        self.context.destroy_aws_resources()
        self.context.stop_fwe()

    def create_collection_schemes(self, arn_counter, random: Random):
        self.collection_schemes = []
        available_signals = [
            f"{self.image_topic}.height",
            f"{self.image_topic}.step",
            f"{self.point_field_topic}.offset",
            f"{self.point_field_topic}.count",
            f"{self.different_types_test_topic}.value_float",
            f"{self.image_topic}.data[1]",
            f"{self.point_field_topic}.datatype",
            f"{self.different_types_test_topic}.value_bool",
            f"{self.different_types_test_topic}.value_byte",
            f"{self.different_types_test_topic}.value_char",
            f"{self.different_types_test_topic}.value_double",
            f"{self.different_types_test_topic}.value_int8",
            f"{self.different_types_test_topic}.value_uint8",
            f"{self.different_types_test_topic}.value_int16",
            f"{self.different_types_test_topic}.value_uint16",
            f"{self.different_types_test_topic}.value_int32",
            f"{self.different_types_test_topic}.value_uint32",
            f"{self.different_types_test_topic}.value_int64",
            f"{self.different_types_test_topic}.value_uint64",
            f"{self.different_types_test_topic}.value_string",
            f"{self.different_types_test_topic}.value_wstring",
            f"{self.different_types_test_topic}.nested_message.nested_value_bool",
            f"{self.different_types_test_topic}.nested_message.nested_message_2.nested_value_uint8",
        ]

        # heartbeat campaigns
        for i in range(1, 6):
            heartbeat_id = "heartbeat_" + str(i) + "_" + str(arn_counter)
            heartbeat_prefix = f"{heartbeat_id}/raw-data/{self.context.fwe_id}/"
            heartbeat = {
                "campaignSyncId": f"{heartbeat_id}",
                "collectionScheme": {
                    "timeBasedCollectionScheme": {"periodMs": 20000},
                },
                "signalsToCollect": [
                    {"name": self.image_topic, "maxSampleCount": 1},
                    {"name": self.different_types_test_topic, "maxSampleCount": 1},
                ],
                "s3UploadMetadata": {
                    "bucketName": os.environ["TEST_S3_BUCKET_NAME"],
                    "bucketOwner": os.environ["TEST_S3_BUCKET_OWNER"],
                    "prefix": heartbeat_prefix,
                    "region": os.environ["TEST_S3_BUCKET_REGION"],
                },
            }
            for signal in available_signals:
                if random.random() < 0.75:
                    heartbeat["signalsToCollect"].append({"name": signal})
            self.collection_schemes.append(heartbeat)

        # conditional campaigns
        for i in range(1, 6):
            conditional_id = "condition_based_" + str(i) + "_" + str(arn_counter)
            conditional_prefix = f"{conditional_id}/raw-data/{self.context.fwe_id}/"
            conditional = {
                "campaignSyncId": f"{conditional_id}",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "conditionLanguageVersion": 1,
                        "expression": f"{self.image_topic}.step == {self.random.randint(3, 9)}",
                        "minimumTriggerIntervalMs": 100,
                        "triggerMode": "RISING_EDGE",
                    },
                },
                "signalsToCollect": [
                    {"name": self.point_field_topic, "maxSampleCount": 1},
                    {"name": self.image_topic, "maxSampleCount": 1},
                    {"name": self.different_types_test_topic, "maxSampleCount": 1},
                ],
                "postTriggerCollectionDuration": 0,
                "s3UploadMetadata": {
                    "bucketName": os.environ["TEST_S3_BUCKET_NAME"],
                    "bucketOwner": os.environ["TEST_S3_BUCKET_OWNER"],
                    "prefix": conditional_prefix,
                    "region": os.environ["TEST_S3_BUCKET_REGION"],
                },
            }
            self.collection_schemes.append(conditional)

    def test_long_avg_complex_data(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(self.collection_schemes)

        start_time = time.time()
        i = 0
        different_campaigns_counter = 0

        # Run for more than 24 hours. Our scheduled pipeline runs every 24 hours, and the next run
        # will stop the current one and upload its logs.
        while (time.time() - start_time) < timedelta(hours=25).total_seconds():
            i = i + 1
            self.context.rosigen.set_value([self.image_topic, "step"], i % 10)
            # Sending new campaigns every 30 seconds
            if i % 30 == 0:
                different_campaigns_counter = different_campaigns_counter + 1
                self.create_collection_schemes(different_campaigns_counter, self.random)
                self.context.send_decoder_manifest()
                self.context.send_collection_schemes(self.collection_schemes)
            time.sleep(1)
