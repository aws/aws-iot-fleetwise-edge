# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
import os
import re
import time
from copy import deepcopy
from pathlib import Path
from typing import List

import boto3
import pytest
import rclpy.serialization
import rosidl_runtime_py.utilities
from tenacity import stop_after_delay
from testframework.common import Retrying, assert_no_log_entries, is_hil
from testframework.context import Context, RawDataBufferConfig, RawDataBufferSignalOverridesConfig

log = logging.getLogger(__name__)


class TestComplexData:
    """This is a test that validate FWE can extract complex data from a ROS2 source"""

    @pytest.fixture(autouse=True, params=[{}], ids=["default_ros2_context"])
    def setup(self, tmp_path: Path, request: pytest.FixtureRequest, worker_number: int):
        self.image_topic = f"ImageTopic{worker_number}"
        self.point_field_topic = f"PointFieldTopic{worker_number}"
        self.different_types_test_topic = f"DifferentTypesTestTopic{worker_number}"

        self.context_kwargs = {
            "ros2_enabled": True,
        }

        self.context_kwargs.update(request.param)
        if "raw_data_buffer_config" in self.context_kwargs:
            for overrides in self.context_kwargs["raw_data_buffer_config"].overrides_per_signal:
                overrides.message_id = overrides.message_id.replace(
                    "ROS2_TOPIC_NUMBER_PLACEHOLDER", str(worker_number)
                )
        self.context = Context(
            tmp_path=tmp_path, worker_number=worker_number, **self.context_kwargs
        )
        self.context.connect_to_cloud()

        self.context.start_cyclic_ros_messages()

        self.image_size_bytes = 1000000
        for i in range(0, self.image_size_bytes):
            self.context.rosigen.set_value([self.image_topic, "data", "[" + str(i) + "]"], i % 256)

        self.context.rosigen.set_value(
            [self.image_topic, "encoding"], "SPECIAL test_complex_data.py encoding"
        )
        self.context.rosigen.set_value([self.image_topic, "height"], 222)
        self.context.rosigen.set_value([self.image_topic, "step"], 173)

        self.context.rosigen.set_value([self.point_field_topic, "offset"], 100)
        self.context.rosigen.set_value([self.point_field_topic, "datatype"], 1)

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

        # This prefix should follow the same pattern as when the collection scheme is sent by the FW
        # service. The service always adds raw-data and thing name to the prefix. If the logic in
        # the service changes, the test should be changed too, to make sure it works with the
        # iot credentials provider example that we provide.
        self.heartbeat1_prefix = f"heartbeat1/raw-data/{self.context.fwe_id}/"
        self.heartbeat1_collection_scheme = {
            "campaignSyncId": "heartbeat1",
            "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 10000}},
            "signalsToCollect": [
                {"name": self.image_topic, "maxSampleCount": 5},
                {"name": f"{self.image_topic}.height"},
                {"name": f"{self.image_topic}.encoding[3]"},
                {"name": f"{self.image_topic}.step"},
                {"name": f"{self.image_topic}.data[1]"},
            ],
            "s3UploadMetadata": {
                "bucketName": os.environ["TEST_S3_BUCKET_NAME"],
                "bucketOwner": os.environ["TEST_S3_BUCKET_OWNER"],
                "prefix": self.heartbeat1_prefix,
                "region": os.environ["TEST_S3_BUCKET_REGION"],
            },
        }

        self.heartbeat2_prefix = f"heartbeat2/raw-data/{self.context.fwe_id}/"
        self.heartbeat2_collection_scheme = {
            "campaignSyncId": "heartbeat2",
            "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 10000}},
            "signalsToCollect": [
                {"name": self.image_topic, "maxSampleCount": 5},
            ],
            "s3UploadMetadata": {
                "bucketName": os.environ["TEST_S3_BUCKET_NAME"],
                "bucketOwner": os.environ["TEST_S3_BUCKET_OWNER"],
                "prefix": self.heartbeat2_prefix,
                "region": os.environ["TEST_S3_BUCKET_REGION"],
            },
        }

        self.heartbeat3_prefix = f"heartbeat3/raw-data/{self.context.fwe_id}/"
        self.heartbeat3_collection_scheme = {
            "campaignSyncId": "heartbeat3",
            "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 10000}},
            "signalsToCollect": [
                {"name": self.different_types_test_topic, "maxSampleCount": 5},
            ],
            "s3UploadMetadata": {
                "bucketName": os.environ["TEST_S3_BUCKET_NAME"],
                "bucketOwner": os.environ["TEST_S3_BUCKET_OWNER"],
                "prefix": self.heartbeat3_prefix,
                "region": os.environ["TEST_S3_BUCKET_REGION"],
            },
        }

        self.heartbeat_more_samples_prefix = (
            f"heartbeat_more_samples/raw-data/{self.context.fwe_id}/"
        )
        self.heartbeat_more_samples_collection_scheme = {
            "campaignSyncId": "heartbeat_more_samples",
            "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 10000}},
            "signalsToCollect": [
                {"name": self.image_topic, "maxSampleCount": 20},
                {"name": f"{self.image_topic}.height"},
                {"name": f"{self.image_topic}.encoding[3]"},
                {"name": f"{self.image_topic}.step"},
                {"name": f"{self.image_topic}.data[1]"},
            ],
            "s3UploadMetadata": {
                "bucketName": os.environ["TEST_S3_BUCKET_NAME"],
                "bucketOwner": os.environ["TEST_S3_BUCKET_OWNER"],
                "prefix": self.heartbeat_more_samples_prefix,
                "region": os.environ["TEST_S3_BUCKET_REGION"],
            },
        }

        self.condition_based1_prefix = f"condition_based1/raw-data/{self.context.fwe_id}/"
        self.condition_based1_collection_scheme = {
            "campaignSyncId": "condition_based_1",
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "conditionLanguageVersion": 1,
                    "expression": f"{self.image_topic}.step == 3",
                    "minimumTriggerIntervalMs": 2000,
                    "triggerMode": "ALWAYS",
                }
            },
            "signalsToCollect": [
                {"name": self.image_topic, "maxSampleCount": 1},
            ],
            "postTriggerCollectionDuration": 0,
            "s3UploadMetadata": {
                "bucketName": os.environ["TEST_S3_BUCKET_NAME"],
                "bucketOwner": os.environ["TEST_S3_BUCKET_OWNER"],
                "prefix": self.condition_based1_prefix,
                "region": os.environ["TEST_S3_BUCKET_REGION"],
            },
        }

        self.condition_based2_prefix = f"condition_based2/raw-data/{self.context.fwe_id}/"
        self.condition_based2_collection_scheme = {
            "campaignSyncId": "condition_based_2",
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "conditionLanguageVersion": 1,
                    "expression": f"{self.point_field_topic}.count == 20",
                    "minimumTriggerIntervalMs": 2000,
                    "triggerMode": "ALWAYS",
                }
            },
            "signalsToCollect": [
                {"name": self.point_field_topic, "maxSampleCount": 1},
            ],
            "postTriggerCollectionDuration": 0,
            "s3UploadMetadata": {
                "bucketName": os.environ["TEST_S3_BUCKET_NAME"],
                "bucketOwner": os.environ["TEST_S3_BUCKET_OWNER"],
                "prefix": self.condition_based2_prefix,
                "region": os.environ["TEST_S3_BUCKET_REGION"],
            },
        }

        # This campaign will validate that all datatypes preceding nested_value_uint8 field are
        # decoded correctly. E.g. wstring characters should be decoded as uint32. If FWE reads
        # some values in-between incorrectly, nested_value_uint8 won't be decoded and campaign
        # won't be triggered
        self.condition_based3_prefix = f"condition_based3/raw-data/{self.context.fwe_id}/"
        self.condition_based3_collection_scheme = {
            "campaignSyncId": "condition_based_3",
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "conditionLanguageVersion": 1,
                    "expression": f"{self.different_types_test_topic}.nested_message.nested_message_2.nested_value_uint8 == 95",  # noqa: E501
                    "minimumTriggerIntervalMs": 10000,
                    "triggerMode": "ALWAYS",
                }
            },
            "signalsToCollect": [
                {"name": self.different_types_test_topic, "maxSampleCount": 1},
            ],
            "postTriggerCollectionDuration": 0,
            "s3UploadMetadata": {
                "bucketName": os.environ["TEST_S3_BUCKET_NAME"],
                "bucketOwner": os.environ["TEST_S3_BUCKET_OWNER"],
                "prefix": self.condition_based3_prefix,
                "region": os.environ["TEST_S3_BUCKET_REGION"],
            },
        }

        self.log_errors_to_ignore: List[re.Pattern] = []
        if self.context_kwargs.get("use_greengrass", False):
            # Sometimes fetching the credential with Greengrass times out. The client will
            # eventually retry, so we shouldn't worry much about it.
            self.log_errors_to_ignore.extend(
                [
                    re.compile(r".*?\[AwsBootstrap\.cpp\:\d+].*?ECSCredentialsClient.*"),
                    re.compile(r".*?\[AwsBootstrap\.cpp\:\d+].*?CurlHttpClient.*?Timeout.*"),
                ]
            )

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_ros_messages()
        self.context.destroy_aws_resources()
        self.context.stop_fwe()

        assert_no_log_entries(self.context.log_errors, self.log_errors_to_ignore)

    def wait_for_s3_objects_to_be_available(
        self, s3_client, *, bucket: str, prefix: str, min_number_of_files: int, stop=None
    ):
        kwargs = {}
        if stop is not None:
            kwargs["stop"] = stop

        for attempt in Retrying(**kwargs):
            with attempt:
                log.info(
                    "Waiting for S3 objects to be uploaded."
                    f" {bucket=} {prefix=} {min_number_of_files=}"
                )
                result = s3_client.list_objects(
                    Bucket=bucket,
                    Prefix=prefix,
                )
                assert "Contents" in result
                assert len(result["Contents"]) >= min_number_of_files

    def assert_ion_data(self, ion_data, prefix: str, campaign_sync_id: str, signal_name: str):
        for single_ion_data in ion_data.values():
            data = single_ion_data["data"]
            assert single_ion_data["prefix"] == prefix
            assert data[0]["ion_scheme_version"] == "1.0.1"
            assert data[0]["campaign_sync_id"] == campaign_sync_id
            assert data[0]["decoder_sync_id"] == "decoder_manifest_1"
            assert data[0]["vehicle_name"] == self.context.fwe_id

            for i in range(1, len(data)):
                if signal_name == self.image_topic:
                    self.assert_image_message(data[i], prefix)
                elif signal_name == self.point_field_topic:
                    self.assert_point_field_message(data[i])
                elif signal_name == self.different_types_test_topic:
                    self.assert_custom_message(data[i], prefix)

    def assert_image_message(self, data, prefix: str):
        msg = rclpy.serialization.deserialize_message(
            data["signal_byte_values"].byte_values,
            rosidl_runtime_py.utilities.get_message("sensor_msgs/msg/Image"),
        )
        assert msg.width == 0
        assert msg.height == 222
        assert msg.is_bigendian == 0
        assert msg.encoding == "SPECIAL test_complex_data.py encoding"
        assert msg.header.stamp.nanosec == 0
        assert msg.header.stamp.sec == 0

        # Validate step for condition based campaign
        if prefix == self.condition_based1_prefix:
            assert msg.step == 3

        assert len(msg.data) == self.image_size_bytes
        for j in range(0, len(msg.data)):
            assert msg.data[j] == j % 256

        assert data["signal_id"] == self.context.proto_factory.signal_name_to_id[self.image_topic]
        assert data["signal_name"] == self.image_topic
        assert data["signal_type"] == "sensor_msgs/msg/Image"

    def assert_point_field_message(self, data):
        msg = rclpy.serialization.deserialize_message(
            data["signal_byte_values"].byte_values,
            rosidl_runtime_py.utilities.get_message("sensor_msgs/msg/PointField"),
        )

        assert msg.name == ""
        assert msg.offset == 100
        assert msg.datatype == 1
        assert msg.count == 20

        assert (
            data["signal_id"]
            == self.context.proto_factory.signal_name_to_id[self.point_field_topic]
        )
        assert data["signal_name"] == self.point_field_topic
        assert data["signal_type"] == "sensor_msgs/msg/PointField"

    def assert_custom_message(self, data, prefix):
        msg = rclpy.serialization.deserialize_message(
            data["signal_byte_values"].byte_values,
            rosidl_runtime_py.utilities.get_message("ros2_custom_messages/msg/DifferentTypesTest"),
        )

        assert msg.value_bool
        assert msg.value_byte == b"\x01"
        assert msg.value_char == 2
        # Float value can be deserialized with a small measurement error
        assert round(msg.value_float, 1) == 3.4
        assert msg.value_double == 5.6
        assert msg.value_int8 == -7
        assert msg.value_uint8 == 8
        assert msg.value_int16 == 574
        assert msg.value_uint16 == 234
        assert msg.value_int32 == -15000
        assert msg.value_uint32 == 25789
        assert msg.value_int64 == 87654326
        assert msg.value_uint64 == 274748594
        assert msg.value_string == "Hello FleetWise"
        assert msg.value_wstring == "Hello FleetWise \U0001F01F"

        assert msg.nested_message.nested_value_bool

        # Validate condition trigger
        if prefix == self.condition_based3_prefix:
            assert msg.nested_message.nested_message_2.nested_value_uint8 == 95

        assert (
            data["signal_id"]
            == self.context.proto_factory.signal_name_to_id[self.different_types_test_topic]
        )
        assert data["signal_name"] == self.different_types_test_topic
        assert data["signal_type"] == "ros2_custom_messages/msg/DifferentTypesTest"

    @pytest.mark.parametrize(
        "setup", [{"use_greengrass": False}, {"use_greengrass": True}], ids=str, indirect=True
    )
    def test_heartbeat(self):
        # Start AWS IoT FleetWise and wait for startup and cloud connection
        self.context.start_fwe()

        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(
            [self.heartbeat1_collection_scheme, self.heartbeat2_collection_scheme]
        )
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {"decoder_manifest_1", "heartbeat1", "heartbeat2"}
                )

        s3_client = boto3.client("s3")
        number_of_files_to_check = 3
        self.wait_for_s3_objects_to_be_available(
            s3_client,
            bucket=os.environ["TEST_S3_BUCKET_NAME"],
            prefix=self.heartbeat1_prefix,
            min_number_of_files=number_of_files_to_check,
            stop=stop_after_delay(60),
        )
        self.wait_for_s3_objects_to_be_available(
            s3_client,
            bucket=os.environ["TEST_S3_BUCKET_NAME"],
            prefix=self.heartbeat2_prefix,
            min_number_of_files=number_of_files_to_check,
        )

        # Stop collection to reduce the noise in the tests.
        self.context.stop_cyclic_ros_messages()
        self.context.send_collection_schemes([])

        ion_data_heartbeat1 = self.context.get_results_from_s3(
            self.heartbeat1_prefix,
            max_number_of_files=number_of_files_to_check,
        )
        assert len(ion_data_heartbeat1) == number_of_files_to_check

        ion_data_heartbeat2 = self.context.get_results_from_s3(
            self.heartbeat2_prefix,
            max_number_of_files=number_of_files_to_check,
        )
        assert len(ion_data_heartbeat2) == number_of_files_to_check

        self.assert_ion_data(
            ion_data_heartbeat1,
            prefix=self.heartbeat1_prefix,
            campaign_sync_id="heartbeat1",
            signal_name=self.image_topic,
        )
        self.assert_ion_data(
            ion_data_heartbeat2,
            prefix=self.heartbeat2_prefix,
            campaign_sync_id="heartbeat2",
            signal_name=self.image_topic,
        )

        # The last file for each campaign may contain less than maxSampleCount messages depending on
        # when the ROS messages were stopped, but all the rest should be complete.
        for single_ion_data in list(ion_data_heartbeat1.values())[:-1]:
            if self.context.is_valgrind_enabled():
                # When run with valgrind, fewer messages than expected may be received due to
                # SIM-15573, but at least 1 message + metadata
                assert len(single_ion_data["data"]) >= 2
            else:
                assert len(single_ion_data["data"]) == 6  # 5 messages + metadata
        for single_ion_data in list(ion_data_heartbeat2.values())[:-1]:
            if self.context.is_valgrind_enabled():
                # When run with valgrind, fewer messages than expected may be received due to
                # SIM-15573, but at least 1 message + metadata
                assert len(single_ion_data["data"]) >= 2
            else:
                assert len(single_ion_data["data"]) == 6  # 5 messages + metadata

        reported_s3_objects = self.context.get_reported_uploaded_s3_objects()
        assert reported_s3_objects["heartbeat1"][:number_of_files_to_check] == list(
            ion_data_heartbeat1.keys()
        ), "Object keys reported to MQTT don't match the uploaded object keys"
        assert reported_s3_objects["heartbeat2"][:number_of_files_to_check] == list(
            ion_data_heartbeat2.keys()
        ), "Object keys reported to MQTT don't match the uploaded object keys"

        # Only clean up the bucket on success
        self.context.clean_up_s3_bucket([self.heartbeat1_prefix, self.heartbeat2_prefix])

    def test_custom_message(self):
        # Start AWS IoT FleetWise and wait for startup and cloud connection
        self.context.start_fwe()

        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(
            [self.heartbeat3_collection_scheme, self.condition_based3_collection_scheme]
        )
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {"decoder_manifest_1", "heartbeat3", "condition_based_3"}
                )
        # Allow minimum trigger time to elapse
        minimum_trigger_interval = (
            self.condition_based3_collection_scheme["collectionScheme"][
                "conditionBasedCollectionScheme"
            ]["minimumTriggerIntervalMs"]
            / 1000
        )
        time.sleep(minimum_trigger_interval)
        log.info("Changing value to trigger condition based campaign")
        self.context.rosigen.set_value(
            [
                self.different_types_test_topic,
                "nested_message",
                "nested_message_2",
                "nested_value_uint8",
            ],
            95,
        )
        # Don't allow another trigger interval to elapse, so the campaign should be triggered once
        time.sleep(minimum_trigger_interval - 2)
        log.info("Changing value again to avoid triggering condition based campaign")
        self.context.rosigen.set_value(
            [
                self.different_types_test_topic,
                "nested_message",
                "nested_message_2",
                "nested_value_uint8",
            ],
            0,
        )

        s3_client = boto3.client("s3")
        number_of_files_to_check = 3
        self.wait_for_s3_objects_to_be_available(
            s3_client,
            bucket=os.environ["TEST_S3_BUCKET_NAME"],
            prefix=self.heartbeat3_prefix,
            min_number_of_files=number_of_files_to_check,
            stop=stop_after_delay(60),
        )

        self.wait_for_s3_objects_to_be_available(
            s3_client,
            bucket=os.environ["TEST_S3_BUCKET_NAME"],
            prefix=self.condition_based3_prefix,
            min_number_of_files=1,
            stop=stop_after_delay(60),
        )

        # Stop collection to reduce the noise in the tests.
        self.context.stop_cyclic_ros_messages()
        self.context.send_collection_schemes([])

        ion_data_heartbeat3 = self.context.get_results_from_s3(
            self.heartbeat3_prefix,
            max_number_of_files=number_of_files_to_check,
        )
        assert len(ion_data_heartbeat3) == number_of_files_to_check

        ion_data_condition_based3 = self.context.get_results_from_s3(
            self.condition_based3_prefix,
            # try to download more to confirm only one file was uploaded
            max_number_of_files=number_of_files_to_check + 1,
        )
        assert len(ion_data_condition_based3) == 1

        self.assert_ion_data(
            ion_data_heartbeat3,
            prefix=self.heartbeat3_prefix,
            campaign_sync_id="heartbeat3",
            signal_name=self.different_types_test_topic,
        )

        self.assert_ion_data(
            ion_data_condition_based3,
            prefix=self.condition_based3_prefix,
            campaign_sync_id="condition_based_3",
            signal_name=self.different_types_test_topic,
        )

        for single_ion_data in list(ion_data_heartbeat3.values())[:-1]:
            if self.context.is_valgrind_enabled():
                # When run with valgrind, fewer messages than expected may be received due to
                # SIM-15573, but at least 1 message + metadata
                assert len(single_ion_data["data"]) >= 2
            else:
                assert len(single_ion_data["data"]) == 6  # 5 messages + metadata

        reported_s3_objects = self.context.get_reported_uploaded_s3_objects()
        assert reported_s3_objects["heartbeat3"][:number_of_files_to_check] == list(
            ion_data_heartbeat3.keys()
        ), "Object keys reported to MQTT don't match the uploaded object keys"
        assert reported_s3_objects["condition_based_3"][:1] == list(
            ion_data_condition_based3.keys()
        ), "Object keys reported to MQTT don't match the uploaded object keys"

        # Only clean up the bucket on success
        self.context.clean_up_s3_bucket([self.heartbeat3_prefix, self.condition_based3_prefix])

    @pytest.mark.skipif(
        is_hil(),
        reason="This test relies on making the ROS2 working with network namespaces. This is"
        " challenging in a HIL setup as the publisher and subscribers would be running on different"
        " machines, so they can't run on the same network namespace.",
    )
    @pytest.mark.parametrize(
        "setup",
        [
            {
                "enable_network_namespace": True,
                "ros2_config_file": "ros_config_high_load.json",
            }
        ],
        ids=["network_namespace"],
        indirect=True,
    )
    def test_large_files(self):
        # We are going to disable the network, so we expect the AWS SDK to log some errors
        self.log_errors_to_ignore.append(re.compile(r".*?\[AwsBootstrap\.cpp\:\d+].*"))

        # Slow down the network to give enough time for the test to disconnect the internet during
        # a multipart upload.
        self.context.link_throttle(rate_kbit=1 * 1024 * 8)  # 1MiB / sec

        # Start AWS IoT FleetWise and wait for startup and cloud connection
        self.context.start_fwe()

        self.context.send_decoder_manifest()
        self.context.send_collection_schemes([self.heartbeat_more_samples_collection_scheme])
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {"decoder_manifest_1", "heartbeat_more_samples"}
                )

        s3_client = boto3.client("s3")

        multipart_uploads = {}
        for attempt in Retrying():
            with attempt:
                log.info("Waiting for a multipart upload to start")
                multipart_uploads = s3_client.list_multipart_uploads(
                    Bucket=os.environ["TEST_S3_BUCKET_NAME"],
                    Prefix=self.heartbeat_more_samples_prefix,
                )
                assert "Uploads" in multipart_uploads
                assert len(multipart_uploads["Uploads"]) > 0

        # Stop collection to reduce the noise in the tests. We are only interested in a single
        # large file.
        self.context.stop_cyclic_ros_messages()
        self.context.send_collection_schemes([])

        multipart_upload = multipart_uploads["Uploads"][0]
        multipart_object_key = multipart_upload["Key"]
        upload_id = multipart_upload["UploadId"]

        for attempt in Retrying():
            with attempt:
                log.info(
                    f"Waiting for some parts to be completed. {multipart_object_key=} {upload_id=}"
                )
                result = s3_client.list_parts(
                    Bucket=os.environ["TEST_S3_BUCKET_NAME"],
                    Key=multipart_object_key,
                    UploadId=upload_id,
                )
                assert "Parts" in result
                assert len(result["Parts"]) > 0

        # Now disable the network
        log.info("Shutting down network")
        self.context.link_down()
        time.sleep(10)
        log.info("Enabling network again")
        self.context.link_up()
        self.context.link_throttle(delay_ms=0)

        for attempt in Retrying(stop=stop_after_delay(120)):
            with attempt:
                log.info(
                    "Waiting for multipart upload to be completed."
                    f" {multipart_object_key=} {upload_id=}"
                )
                result = s3_client.list_objects(
                    Bucket=os.environ["TEST_S3_BUCKET_NAME"],
                    Prefix=self.heartbeat_more_samples_prefix,
                )
                assert "Contents" in result
                object_keys = [content["Key"] for content in result["Contents"]]
                assert multipart_object_key in object_keys

        ion_data = self.context.get_results_from_s3(
            self.heartbeat_more_samples_prefix,
            max_number_of_files=1,
        )
        assert len(ion_data) == 1

        self.assert_ion_data(
            ion_data,
            prefix=self.heartbeat_more_samples_prefix,
            campaign_sync_id="heartbeat_more_samples",
            signal_name=self.image_topic,
        )

        assert len(ion_data[multipart_object_key]["data"]) == 21  # 20 messages + metadata

        # Only clean up the bucket on success
        self.context.clean_up_s3_bucket([self.heartbeat_more_samples_prefix])

    @pytest.mark.parametrize(
        "setup",
        [
            {
                "raw_data_buffer_config": RawDataBufferConfig(
                    # Configure the general raw data buffer parameters with very small values,
                    # which would make the signals fail to be collected.
                    reserved_bytes_per_signal=0,
                    max_samples_per_signal=1,
                    max_bytes_per_sample=40,
                    max_bytes_per_signal=50,
                    overrides_per_signal=[
                        # But then add overrides for the signal, to make sure the config overrides
                        # work
                        RawDataBufferSignalOverridesConfig(
                            interface_id="10",
                            message_id=(
                                "ImageTopicROS2_TOPIC_NUMBER_PLACEHOLDER:sensor_msgs/msg/Image"
                            ),
                            max_bytes=512 * 1024 * 1024,  # 512 MiB
                            max_samples=1000,
                            max_bytes_per_sample=10 * 1024 * 1024,  # 10 MiB
                        )
                    ],
                ),
            }
        ],
        ids=["signal_overrides"],
        indirect=True,
    )
    def test_multiple_campaigns(self):
        # Start AWS IoT FleetWise and wait for startup and cloud connection
        self.context.start_fwe()

        self.context.send_decoder_manifest()
        self.context.send_collection_schemes([self.heartbeat1_collection_scheme])
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1", "heartbeat1"})

        s3_client = boto3.client("s3")
        number_of_files_to_check = 3
        self.wait_for_s3_objects_to_be_available(
            s3_client,
            bucket=os.environ["TEST_S3_BUCKET_NAME"],
            prefix=self.heartbeat1_prefix,
            min_number_of_files=number_of_files_to_check,
            stop=stop_after_delay(60),
        )

        ion_data_heartbeat1 = self.context.get_results_from_s3(
            self.heartbeat1_prefix,
            max_number_of_files=number_of_files_to_check,
        )
        assert len(ion_data_heartbeat1) == number_of_files_to_check

        self.assert_ion_data(
            ion_data_heartbeat1,
            prefix=self.heartbeat1_prefix,
            campaign_sync_id="heartbeat1",
            signal_name=self.image_topic,
        )

        # The last file for each campaign may contain less than maxSampleCount messages depending on
        # when the ROS messages were stopped, but all the rest should be complete.
        for single_ion_data in list(ion_data_heartbeat1.values())[:-1]:
            if self.context.is_valgrind_enabled():
                # When run with valgrind, fewer messages than expected may be received due to
                # SIM-15573, but at least 1 message + metadata
                assert len(single_ion_data["data"]) >= 2
            else:
                assert len(single_ion_data["data"]) == 6  # 5 messages + metadata

        reported_s3_objects = self.context.get_reported_uploaded_s3_objects()
        assert reported_s3_objects["heartbeat1"][:number_of_files_to_check] == list(
            ion_data_heartbeat1.keys()
        ), "Object keys reported to MQTT don't match the uploaded object keys"

        self.context.send_collection_schemes(
            [self.heartbeat1_collection_scheme, self.heartbeat2_collection_scheme]
        )

        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {"decoder_manifest_1", "heartbeat1", "heartbeat2"}
                )

        self.wait_for_s3_objects_to_be_available(
            s3_client,
            bucket=os.environ["TEST_S3_BUCKET_NAME"],
            prefix=self.heartbeat2_prefix,
            min_number_of_files=number_of_files_to_check,
            stop=stop_after_delay(60),
        )

        # Stop collection to reduce the noise in the tests.
        self.context.stop_cyclic_ros_messages()
        self.context.send_collection_schemes([])

        ion_data_heartbeat2 = self.context.get_results_from_s3(
            self.heartbeat2_prefix,
            max_number_of_files=number_of_files_to_check,
        )
        assert len(ion_data_heartbeat2) == number_of_files_to_check

        self.assert_ion_data(
            ion_data_heartbeat2,
            prefix=self.heartbeat2_prefix,
            campaign_sync_id="heartbeat2",
            signal_name=self.image_topic,
        )

        # The last file for each campaign may contain less than maxSampleCount messages depending on
        # when the ROS messages were stopped, but all the rest should be complete.
        for single_ion_data in list(ion_data_heartbeat2.values())[:-1]:
            if self.context.is_valgrind_enabled():
                # When run with valgrind, fewer messages than expected may be received due to
                # SIM-15573, but at least 1 message + metadata
                assert len(single_ion_data["data"]) >= 2
            else:
                assert len(single_ion_data["data"]) == 6  # 5 messages + metadata

        reported_s3_objects = self.context.get_reported_uploaded_s3_objects()
        assert reported_s3_objects["heartbeat2"][:number_of_files_to_check] == list(
            ion_data_heartbeat2.keys()
        ), "Object keys reported to MQTT don't match the uploaded object keys"

        # Only clean up the bucket on success
        self.context.clean_up_s3_bucket([self.heartbeat1_prefix, self.heartbeat2_prefix])

    def test_condition_based(self):
        # Start AWS IoT FleetWise and wait for startup and cloud connection
        self.context.start_fwe()

        self.context.send_decoder_manifest()
        self.context.send_collection_schemes([self.condition_based1_collection_scheme])
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1", "condition_based_1"})

        # This campaign has to be triggered on step=3.
        # Step is incremented every second, so only one event is expected.
        for i in range(1, 25):
            self.context.rosigen.set_value([self.image_topic, "step"], i)
            time.sleep(1)

        s3_client = boto3.client("s3")
        number_of_files_to_check = 1
        self.wait_for_s3_objects_to_be_available(
            s3_client,
            bucket=os.environ["TEST_S3_BUCKET_NAME"],
            prefix=self.condition_based1_prefix,
            min_number_of_files=number_of_files_to_check,
            stop=stop_after_delay(60),
        )

        # Stop collection to reduce the noise in the tests.
        self.context.stop_cyclic_ros_messages()
        self.context.send_collection_schemes([])

        ion_data_condition_based1 = self.context.get_results_from_s3(
            self.condition_based1_prefix,
            # try to download more files than expected to verify that only one file was uploaded
            max_number_of_files=number_of_files_to_check + 1,
        )
        assert len(ion_data_condition_based1) == number_of_files_to_check

        self.assert_ion_data(
            ion_data_condition_based1,
            prefix=self.condition_based1_prefix,
            campaign_sync_id="condition_based_1",
            signal_name=self.image_topic,
        )

        reported_s3_objects = self.context.get_reported_uploaded_s3_objects()
        assert reported_s3_objects["condition_based_1"][:number_of_files_to_check] == list(
            ion_data_condition_based1.keys()
        ), "Object keys reported to MQTT don't match the uploaded object keys"

        # Only clean up the bucket on success
        self.context.clean_up_s3_bucket([self.condition_based1_prefix])

    def test_multiple_condition_based(self):
        # Start AWS IoT FleetWise and wait for startup and cloud connection
        self.context.start_fwe()

        # Intentionally send the documents in multiple messages and unusual order to ensure that
        # the partial signal IDs generated by FWE are unique.
        self.context.send_collection_schemes([self.condition_based1_collection_scheme])
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"condition_based_1"})

        self.context.send_decoder_manifest()
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1", "condition_based_1"})

        # Create an identical collection scheme, only changing the ID and prefix to ensure that both
        # campaigns really reference the same signals.
        condition_based1_copy_prefix = f"condition_based1_copy/raw-data/{self.context.fwe_id}/"
        condition_based1_copy_collection_scheme = deepcopy(self.condition_based1_collection_scheme)
        condition_based1_copy_collection_scheme["campaignSyncId"] = "condition_based_1_copy"
        condition_based1_copy_collection_scheme["s3UploadMetadata"][
            "prefix"
        ] = condition_based1_copy_prefix

        # Send the existing collection scheme last, so that the new campaign is processed first.
        # This is to ensure that the new collection scheme doesn't reuse the partial signal IDs from
        # existing campaign.
        self.context.send_collection_schemes(
            [
                self.condition_based2_collection_scheme,
                condition_based1_copy_collection_scheme,
                self.condition_based1_collection_scheme,
            ]
        )
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        "decoder_manifest_1",
                        "condition_based_1",
                        "condition_based_1_copy",
                        "condition_based_2",
                    }
                )

        # Expect each campaign to trigger only once: ImageTopic.step=3, PointFieldTopic.count=20
        # If partial signal id for ImageTopic.step and PointFieldTopic.count is same, second
        # campaign will be wrongly triggered twice with count=20 and count=30.
        for i in range(1, 25):
            self.context.rosigen.set_value([self.image_topic, "step"], i)
            self.context.rosigen.set_value([self.point_field_topic, "count"], i + 10)
            time.sleep(1)

        s3_client = boto3.client("s3")
        number_of_files_to_check = 1
        self.wait_for_s3_objects_to_be_available(
            s3_client,
            bucket=os.environ["TEST_S3_BUCKET_NAME"],
            prefix=self.condition_based1_prefix,
            min_number_of_files=number_of_files_to_check,
            stop=stop_after_delay(60),
        )
        self.wait_for_s3_objects_to_be_available(
            s3_client,
            bucket=os.environ["TEST_S3_BUCKET_NAME"],
            prefix=condition_based1_copy_prefix,
            min_number_of_files=number_of_files_to_check,
            stop=stop_after_delay(60),
        )
        self.wait_for_s3_objects_to_be_available(
            s3_client,
            bucket=os.environ["TEST_S3_BUCKET_NAME"],
            prefix=self.condition_based2_prefix,
            min_number_of_files=number_of_files_to_check,
            stop=stop_after_delay(60),
        )

        # Stop collection to reduce the noise in the tests.
        self.context.stop_cyclic_ros_messages()
        self.context.send_collection_schemes([])

        ion_data_condition_based1 = self.context.get_results_from_s3(
            self.condition_based1_prefix,
            # try to download more files than expected to verify that only one file was uploaded
            max_number_of_files=number_of_files_to_check + 1,
        )
        assert len(ion_data_condition_based1) == number_of_files_to_check

        self.assert_ion_data(
            ion_data_condition_based1,
            prefix=self.condition_based1_prefix,
            campaign_sync_id="condition_based_1",
            signal_name=self.image_topic,
        )

        ion_data_condition_based1_copy = self.context.get_results_from_s3(
            condition_based1_copy_prefix,
            # try to download more files than expected to verify that only one file was uploaded
            max_number_of_files=number_of_files_to_check + 1,
        )
        assert len(ion_data_condition_based1_copy) == number_of_files_to_check

        self.assert_ion_data(
            ion_data_condition_based1_copy,
            prefix=condition_based1_copy_prefix,
            campaign_sync_id="condition_based_1_copy",
            signal_name=self.image_topic,
        )

        ion_data_condition_based2 = self.context.get_results_from_s3(
            self.condition_based2_prefix,
            # try to download more files than expected to verify that only one file was uploaded
            max_number_of_files=number_of_files_to_check + 1,
        )
        assert len(ion_data_condition_based2) == number_of_files_to_check

        self.assert_ion_data(
            ion_data_condition_based2,
            prefix=self.condition_based2_prefix,
            campaign_sync_id="condition_based_2",
            signal_name=self.point_field_topic,
        )

        # Only clean up the bucket on success
        self.context.clean_up_s3_bucket(
            [self.condition_based1_prefix, self.condition_based2_prefix]
        )
