# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import logging
import os
import time
from math import ceil
from pathlib import Path
from random import Random

import boto3
import jsonschema
import pytest
from tenacity import stop_after_delay
from testframework.common import Retrying, is_hil
from testframework.context import Context, RawDataBufferConfig
from testframework.process_utils import attach_perf

log = logging.getLogger(__name__)


class TestLoad:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, request: pytest.FixtureRequest, worker_number: int):
        # Explicitly map test to metric to force us to set new metric string when adding new tests.
        test_name_to_metric_map = {
            "test_load_test_1": "load_test_1",
            "test_load_deep_expressions": "test_load_deep_expressions",
        }
        metrics_name = test_name_to_metric_map[request.node.name]

        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            number_channels=4,
            high_load=True,
            metrics_name=metrics_name,
        )
        self.context.start_cyclic_can_messages()

        log.info("Setting initial values")
        self.context.set_can_signal("Engine_Airflow", 0.0)
        self.context.set_can_signal("Parking_Brake_State", 0)
        self.context.set_obd_signal("ENGINE_SPEED", 1000.0)
        self.context.set_obd_signal("ENGINE_OIL_TEMPERATURE", 98.99)
        self.context.set_obd_signal("CONTROL_MODULE_VOLTAGE", 14.5)

        self.context.connect_to_cloud()
        # collects an OBD signal ENGINE_SPEED
        self.available_signals = [
            "ABSActvProt",
            "ABSIO",
            "ABSPr",
            "ACCAct370",
            "ACCDrvrSeltdSpd",
            "AutoBrkngAct",
            "AutoBrkngAvlbl",
            "BPDAPS_BkPDrvApP",
            "Brake_Lights",
            "Brake_to_release_Parking_Brake",
            "DDAjrSwAtv",
            "DrSbltAtc",
            "DrvThrtlOvrdIO",
            "Electric_Park_Brake_Switch",
            "Electric_Park_Brake_Switch",
            "Engine_Airflow",
            "Engine_Cooling_Temperature_ECT",
            "Engine_Torque",
            "FwdClnAlrtPr",
            "High_Beam",
            "HndsOffStrWhlDtMd",
            "HndsOffStrWhlDtSt",
            "Ky_IdDevPr",
            "LnCat",
            "Low_Beam",
            "Main_Light_Switch",
            "OtsAirTmp",
            "PDAjrSwAtv",
            "Parking_Brake_State",
            "PsSbltAtc",
            "PsngSysLat",
            "PsngSysLong",
            "RLDoorAjarSwAct",
            "RRDoorAjarSwAct",
            "TCSysDisSwAtv_12A",
            "Throttle__Position",
            "TireLFPrs",
            "TireLRPrs",
            "TireRFPrs",
            "TireRRPrs",
            "Vehicle_Odometer",
        ]

        self.random = Random(9837234)
        # add heartbeats
        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_can_messages()
        self.context.stop_fwe()
        self.context.destroy_aws_resources()
        assert self.context.collected_samples_counter > 0
        assert self.context.collected_samples_counter >= self.context.received_samples_counter
        # We can't compare the number of collected and received samples in an exact way because we
        # could have stopped the subscription before receiving all messages. But at least we need to
        # ensure that we received most of the samples.
        messages_not_received = (
            self.context.collected_samples_counter - self.context.received_samples_counter
        )
        assert messages_not_received / self.context.collected_samples_counter < 0.1
        self.context.check_metrics_against_alarms()

    def test_load_test_1(self, tmp_path: Path):
        """
        This load test sends 50 heartbeat collection schemes, 30 condition based collection
        schemes and one obd collection scheme.
        The same collection scheme and decoder manifest is sent every second again to FWE. The brake
        pedal pressure (used by condition based collection scheme) change every 10ms.
        The stress test takes 1 minute and puts the sample_high_bus_load.dbc on 4 CANs in parallel
        (>80% on 500KBit/s)
        """
        self.context.start_fwe()

        collection_schemes = []
        for i in range(1, 50):
            heartbeat = {
                "compression": "SNAPPY",
                "campaignSyncId": "heartbeat_" + str(i),
                "collectionScheme": {
                    "timeBasedCollectionScheme": {
                        "periodMs": max(int(self.random.gauss(35000, 15000)), 500)
                    },
                },
                "signalsToCollect": [],
            }
            for signal in self.available_signals:
                for channel_nr in range(1, 5):
                    if self.random.random() < 0.1:
                        signal_name = signal
                        if channel_nr > 1:
                            signal_name += "_channel_" + str(channel_nr)
                        heartbeat["signalsToCollect"].append({"name": signal_name})
            collection_schemes.append(heartbeat)

        obd_collection_scheme = {
            "campaignSyncId": "obd_collect",
            "collectionScheme": {
                "timeBasedCollectionScheme": {"periodMs": 1200},
            },
            "signalsToCollect": [
                {"name": "ENGINE_SPEED"},
                {"name": "ENGINE_OIL_TEMPERATURE"},
                {"name": "CONTROL_MODULE_VOLTAGE"},
            ],
        }
        collection_schemes.append(obd_collection_scheme)

        for i in range(1, 30):
            conditional = {
                "campaignSyncId": "conditional_" + str(i),
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "expression": f"BPDAPS_BkPDrvApP > {self.random.uniform(0, 9000)}",
                        "minimumTriggerIntervalMs": max(int(self.random.gauss(25000, 10000)), 500),
                    },
                },
                "signalsToCollect": [{"name": "BPDAPS_BkPDrvApP"}],
            }
            collection_schemes.append(conditional)

        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(collection_schemes)
        log.info("Wait 60 seconds changing signals every 10ms")

        # perf uses sampling, which has a very low overhead. We can always enable it for the
        # load test so that we can generate flame graphs as a CI artifact.
        with attach_perf(
            self.context.subprocess_helper,
            pid=self.context.fwe_process.pid,
            output_svg_file=tmp_path / "perf_fwe.svg",
        ):
            self.context.start_cpu_measurement()
            for i in range(0, 6000):
                self.context.set_can_signal("BPDAPS_BkPDrvApP", self.random.uniform(0, 9000))
                if i % 100 == 0:
                    self.context.send_decoder_manifest()
                    self.context.send_collection_schemes(collection_schemes)
                time.sleep(0.01)
            self.context.stop_cpu_measurement()

        counter_per_collection_scheme = {}
        for r in self.context.received_data:
            arn = r.campaign_sync_id
            if arn not in counter_per_collection_scheme:
                counter_per_collection_scheme[arn] = 1
            else:
                counter_per_collection_scheme[arn] = counter_per_collection_scheme[arn] + 1

        log.info(
            "From the following collection schemes data was received: "
            + str(counter_per_collection_scheme)
        )
        assert len(counter_per_collection_scheme) > 60
        log.info("first received data:" + str(self.context.received_data[0]))
        log.info("last received data:" + str(self.context.received_data[-1]))

    def test_load_deep_expressions(self, tmp_path: Path):
        """
        This load test sends 80 condition based collection schemes.
        Each campaign expression's AST consists of 127 nodes
        Depth of the expression is 6, our service supports max depth of 10
        The 2 signals (used in expression of the condition based campaign) change every 10ms.
        The stress test takes 1 minute and puts the sample_high_bus_load.dbc on 4 CANs in parallel
        (>80% on 500KBit/s)
        """
        self.context.start_fwe()
        self.context.send_decoder_manifest()

        # Base expression is (Signal1 > x || Signal2 < y) ==> depth 1
        additional_depth = 5
        number_of_campaigns = 80
        max_number_of_signals_to_collect = len(self.available_signals)
        nested_collection_schemes = self.create_nested_collection_schemes(
            additional_depth, number_of_campaigns, max_number_of_signals_to_collect
        )
        self.context.send_collection_schemes(nested_collection_schemes)
        log.info("Wait 60 seconds changing signals every 10ms")

        with attach_perf(
            self.context.subprocess_helper,
            pid=self.context.fwe_process.pid,
            output_svg_file=tmp_path / "perf_fwe.svg",
        ):
            self.context.start_cpu_measurement()
            for _i in range(0, 6000):
                self.context.set_can_signal("BPDAPS_BkPDrvApP", self.random.uniform(0, 9000))
                self.context.set_can_signal("TireLRPrs", self.random.uniform(0, 510))
                time.sleep(0.01)
            self.context.stop_cpu_measurement()

        counter_per_collection_scheme = {}
        for r in self.context.received_data:
            arn = r.campaign_sync_id
            if arn not in counter_per_collection_scheme:
                counter_per_collection_scheme[arn] = 1
            else:
                counter_per_collection_scheme[arn] = counter_per_collection_scheme[arn] + 1

        log.info(
            "From the following collection schemes data was received: "
            + str(counter_per_collection_scheme)
        )
        assert len(counter_per_collection_scheme) > 60
        log.info("first received data:" + str(self.context.received_data[0]))
        log.info("last received data:" + str(self.context.received_data[-1]))

    def create_nested_collection_schemes(
        self, additional_depth: int, number_of_campaigns: int, max_number_of_signals_to_collect: int
    ):
        nested_collection_schemes = []
        for i in range(1, 81):
            nested_condition_scheme = {
                "campaignSyncId": f"nested_condition_{i}",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "expression": self.create_nested_expression(additional_depth),
                        "minimumTriggerIntervalMs": max(int(self.random.gauss(25000, 10000)), 500),
                    },
                },
                "signalsToCollect": [
                    {"name": "BPDAPS_BkPDrvApP", "maxSampleCount": 350},
                    {"name": "ACCDrvrSeltdSpd", "maxSampleCount": 350},
                ],
            }
            for signal in self.available_signals:
                if (
                    len(nested_condition_scheme["signalsToCollect"])
                    >= max_number_of_signals_to_collect
                ):
                    break
                for channel_nr in range(1, 5):
                    if self.random.random() < 0.1:
                        signal_name = signal
                        if channel_nr > 1:
                            signal_name += "_channel_" + str(channel_nr)
                        nested_condition_scheme["signalsToCollect"].append(
                            {"name": signal_name, "maxSampleCount": 350}
                        )
            nested_collection_schemes.append(nested_condition_scheme)
        return nested_collection_schemes

    def create_nested_expression(self, additional_depth: int):
        def create_expression(depth: int) -> str:
            if depth == 1:
                return (
                    f"(BPDAPS_BkPDrvApP > {self.random.uniform(0, 9000)} "
                    f"|| ACCDrvrSeltdSpd < {self.random.uniform(0, 200)})"
                )
            else:
                left_expression = create_expression(depth - 1)
                right_expression = create_expression(depth - 1)
                return f"({left_expression}) && ({right_expression})"

        return create_expression(additional_depth)


class TestComplexData:
    """This is a test that validate FWE can extract complex data from a ROS2 source"""

    @pytest.fixture(autouse=True, params=[{}], ids=["default_ros2_context"])
    def setup(self, tmp_path: Path, request: pytest.FixtureRequest, worker_number: int):
        self.image_topic = f"ImageTopic{worker_number}"

        self.context_kwargs = {
            "ros2_enabled": True,
        }

        self.context_kwargs.update(request.param)
        if "raw_data_buffer_config" in self.context_kwargs:
            for overrides in self.context_kwargs["raw_data_buffer_config"].overrides_per_signal:
                overrides.message_id = overrides.message_id.replace(
                    "ROS2_TOPIC_NUMBER_PLACEHOLDER", str(worker_number)
                )

        test_name_to_metric_map = {
            "test_high_ros2_load_with_slow_connection[network_namespace]": (
                "test_high_ros2_load_with_slow_connection_network_namespace_"
            ),
        }
        metrics_name = test_name_to_metric_map[request.node.name]
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            **self.context_kwargs,
            metrics_name=metrics_name,
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

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_ros_messages()
        self.context.destroy_aws_resources()
        self.context.stop_fwe()
        self.context.check_metrics_against_alarms()

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
                self.assert_image_message(data[i], prefix)

    def assert_image_message(self, data, prefix: str):
        import rclpy.serialization
        import rosidl_runtime_py.utilities

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

        assert len(msg.data) == self.image_size_bytes
        for j in range(0, len(msg.data)):
            assert msg.data[j] == j % 256

        assert data["signal_id"] == self.context.proto_factory.signal_name_to_id[self.image_topic]
        assert data["signal_name"] == self.image_topic
        assert data["signal_type"] == "sensor_msgs/msg/Image"

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
                "raw_data_buffer_config": RawDataBufferConfig(
                    max_bytes=1 * 1024 * 1024 * 1024,  # 1 GiB
                    reserved_bytes_per_signal=0,
                    max_samples_per_signal=1000,
                    max_bytes_per_signal=64 * 1024 * 1024,  # 64 MiB
                ),
            }
        ],
        ids=["network_namespace"],
        indirect=True,
    )
    def test_high_ros2_load_with_slow_connection(self):
        # We want to ensure that upload is slower than ROS2 incoming messages, so we need to control
        # exactly the amount of data being collected and the network speed.
        max_sample_count = 5
        period_ms = 400
        max_file_size_bytes = self.image_size_bytes * max_sample_count

        collected_samples_per_second = max_sample_count * (1000 / period_ms)
        collected_bytes_per_second = self.image_size_bytes * collected_samples_per_second
        seconds_to_fill_buffer_twice = 2 * (
            self.context_kwargs["raw_data_buffer_config"].max_bytes_per_signal
            / collected_bytes_per_second
        )
        # Now determine the max network speed we can allow so that the first file takes long enough
        # to upload and raw data is overwritten.
        max_network_speed_bytes = max_file_size_bytes / seconds_to_fill_buffer_twice
        rate_kbit = ceil(max_network_speed_bytes / 1024 * 8)

        self.heartbeat_more_samples_prefix = (
            f"heartbeat_more_samples/raw-data/{self.context.fwe_id}/"
        )
        self.heartbeat_more_samples_collection_scheme = {
            "campaignSyncId": "heartbeat_more_samples",
            "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": period_ms}},
            "signalsToCollect": [
                {"name": self.image_topic, "maxSampleCount": max_sample_count},
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

        log.info(
            "Throttling network to give enough time for the raw buffer to become full and"
            f" start discarding data. {seconds_to_fill_buffer_twice=} {rate_kbit=}"
        )
        self.context.link_throttle(rate_kbit=rate_kbit)

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

        self.wait_for_s3_objects_to_be_available(
            s3_client,
            bucket=os.environ["TEST_S3_BUCKET_NAME"],
            prefix=self.heartbeat_more_samples_prefix,
            min_number_of_files=1,
            stop=stop_after_delay(60),
        )

        self.context.link_throttle(delay_ms=0)

        number_of_files_to_check = 3
        self.wait_for_s3_objects_to_be_available(
            s3_client,
            bucket=os.environ["TEST_S3_BUCKET_NAME"],
            prefix=self.heartbeat_more_samples_prefix,
            min_number_of_files=number_of_files_to_check,
            stop=stop_after_delay(60),
        )

        self.context.stop_cyclic_ros_messages()
        self.context.send_collection_schemes([])

        ion_data = self.context.get_results_from_s3(
            self.heartbeat_more_samples_prefix,
            max_number_of_files=number_of_files_to_check,
        )
        assert len(ion_data) == number_of_files_to_check

        self.assert_ion_data(
            ion_data,
            prefix=self.heartbeat_more_samples_prefix,
            campaign_sync_id="heartbeat_more_samples",
            signal_name=self.image_topic,
        )

        # First file should be complete since once a file upload starts none of the raw data it
        # needs should be deleted until the upload is completed.
        first_ion_data = list(ion_data.values())[0]
        assert len(first_ion_data["data"]) == max_sample_count + 1  # messages with data + metadata

        # For the remaining files we can find any number of messages because some of the data could
        # have been released while the first file was being uploaded.
        for single_ion_data in list(ion_data.values())[1:]:
            # We should expect at most 20 messages + metadata, but we should never have a file with
            # metadata only.
            assert len(single_ion_data["data"]) > 1
            assert len(single_ion_data["data"]) <= (
                max_sample_count + 1
            )  # metadata + any number up to max samples

        # Only clean up the bucket on success
        self.context.clean_up_s3_bucket([self.heartbeat_more_samples_prefix])


class TestStoreAndForwardLoad:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, request: pytest.FixtureRequest, worker_number: int):
        test_name_to_metric_map = {
            "test_store_and_forward_load_test": "test_store_and_forward_load_test",
            "test_store_and_forward_multiple_partitions_load_test": (
                "test_store_and_forward_multiple_partitions_load_test"
            ),
            "test_store_and_forward_multiple_campaigns_load_test": (
                "test_store_and_forward_multiple_campaigns_load_test"
            ),
        }
        metrics_name = test_name_to_metric_map[request.node.name]
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            persistency_file=True,
            use_store_and_forward=True,
            number_channels=4,
            high_load=True,
            metrics_name=metrics_name,
        )
        self.context.connect_to_cloud()
        self.context.start_fwe()
        self.context.start_cyclic_can_messages()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_can_messages()
        self.context.destroy_aws_resources()
        self.context.stop_fwe()
        self.context.check_metrics_against_alarms()

    def _send_collection_schemes(self, schemes):
        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(schemes)
        self._wait_for_checkin_documents(schemes)

    def _wait_for_checkin_documents(self, schemes):
        for attempt in Retrying():
            with attempt:
                expected_documents = {"decoder_manifest_1"}
                for scheme in schemes:
                    expected_documents.add(scheme["campaignSyncId"])
                self.context.verify_checkin_documents(expected_documents)

    def _get_partition_size_on_disk(self, campaign_name, partition_storage_location) -> int:
        """
        Get the size of all stream files for the partition.
        This does NOT include KV stores (e.g. checkpoints)
        """
        partition_dir = os.path.join(
            self.context.persistency_folder, campaign_name, partition_storage_location
        )
        cmd = (
            f"if [ -d {partition_dir} ]; then "
            f'find {partition_dir} -type f -name "*.log"'
            " | xargs -r stat --format=%s"
            ' | awk "{s+=\\$1} END {print s}"; fi'
        )
        res = self.context.subprocess_helper.target_check_output(cmd.split(), shell=True).strip()
        file_size_sum = 0 if res == "" else int(res)
        print(f"File size sum for {partition_dir}: {file_size_sum}")
        return file_size_sum

    def _wait_for_partitions_to_have_size(self, campaign_name, partition_locations, size):
        # Give a full minute for partitions to have the specified size
        for attempt in Retrying(stop=stop_after_delay(60)):
            with attempt:
                for location in partition_locations:
                    assert self._get_partition_size_on_disk(campaign_name, location) >= size

    def _wait_for_all_data_received(self, size):
        for attempt in Retrying(stop=stop_after_delay(180)):
            with attempt:
                assert self.context.receive_data_bytes >= size

    def test_store_and_forward_load_test(self, tmp_path: Path):
        campaign_name = "load_test"
        campaign_sync_id = campaign_name + "#8713432"
        partition_storage_location = "abc"

        collect_signal = "Electric_Park_Brake_Switch"
        forward_signal = "Main_Light_Switch"

        available_signals = [
            "ABSActvProt",
            "ABSIO",
            "ABSPr",
            "ACCAct370",
            "ACCDrvrSeltdSpd",
            "AutoBrkngAct",
            "AutoBrkngAvlbl",
            "BPDAPS_BkPDrvApP",
            "Brake_Lights",
            "Brake_to_release_Parking_Brake",
            "DDAjrSwAtv",
            "DrSbltAtc",
            "DrvThrtlOvrdIO",
            "Electric_Park_Brake_Switch",
            "Engine_Airflow",
            "Engine_Cooling_Temperature_ECT",
            "Engine_Torque",
            "FwdClnAlrtPr",
            "High_Beam",
            "HndsOffStrWhlDtMd",
            "HndsOffStrWhlDtSt",
            "Ky_IdDevPr",
            "LnCat",
            "Low_Beam",
            "Main_Light_Switch",
            "OtsAirTmp",
            "PDAjrSwAtv",
            "Parking_Brake_State",
            "PsSbltAtc",
            "PsngSysLat",
            "PsngSysLong",
            "RLDoorAjarSwAct",
            "RRDoorAjarSwAct",
            "TCSysDisSwAtv_12A",
            "Throttle__Position",
            "TireLFPrs",
            "TireLRPrs",
            "TireRFPrs",
            "TireRRPrs",
            "Vehicle_Odometer",
        ]

        signals_to_collect = [
            {"name": signal, "dataPartitionId": 0} for signal in available_signals
        ]

        collect_scheme = {
            "campaignSyncId": campaign_sync_id,
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "expression": f"{collect_signal} > 0.0",
                    "minimumTriggerIntervalMs": 1000,
                }
            },
            "signalsToCollect": signals_to_collect,
            "storeAndForwardConfiguration": [
                {
                    "storageOptions": {
                        "maximumSizeInBytes": 600000,
                        "storageLocation": partition_storage_location,
                        "minimumTimeToLiveInSeconds": 600,
                    },
                    "uploadOptions": {"conditionTree": f"{forward_signal} == 1.0"},
                },
            ],
            "spoolingMode": "TO_DISK",
            "campaignArn": campaign_name,
        }

        self._send_collection_schemes([collect_scheme])

        with attach_perf(
            self.context.subprocess_helper,
            pid=self.context.fwe_process.pid,
            output_svg_file=tmp_path / "perf_fwe.svg",
        ):
            self.context.start_cpu_measurement()

            collect_start_time = time.time()
            self.context.set_can_signal(collect_signal, 1)
            # Collect Data until the store is full
            self._wait_for_partitions_to_have_size(
                campaign_name=campaign_name,
                partition_locations=[partition_storage_location],
                size=500000,
            )
            self.context.set_can_signal(collect_signal, 0)
            collect_end_time = time.time()

            # Forward all data
            forward_start_time = time.time()
            self.context.set_can_signal(forward_signal, 1)
            self._wait_for_all_data_received(size=500000)
            self.context.set_can_signal(forward_signal, 0)
            forward_end_time = time.time()

            self.context.stop_cpu_measurement()

        print(f"Storing took {collect_end_time - collect_start_time:.4f} seconds")
        print(f"Forwarding took {forward_end_time - forward_start_time:.4f} seconds")

    def test_store_and_forward_multiple_partitions_load_test(self, tmp_path: Path):
        campaign_name = "load_test_multiple_partitions"
        campaign_sync_id = campaign_name + "#8713432"
        partition_storage_location = "abc_"

        collect_signal = "Electric_Park_Brake_Switch"
        forward_signal = "Main_Light_Switch"

        # Each partition will have 8 signals
        signals_to_collect = [
            {"name": "ABSActvProt", "dataPartitionId": 0},
            {"name": "ABSIO", "dataPartitionId": 0},
            {"name": "ABSPr", "dataPartitionId": 0},
            {"name": "ACCAct370", "dataPartitionId": 0},
            {"name": "ACCDrvrSeltdSpd", "dataPartitionId": 0},
            {"name": "AutoBrkngAct", "dataPartitionId": 0},
            {"name": "AutoBrkngAvlbl", "dataPartitionId": 0},
            {"name": "BPDAPS_BkPDrvApP", "dataPartitionId": 0},
            {"name": "Brake_Lights", "dataPartitionId": 1},
            {"name": "Brake_to_release_Parking_Brake", "dataPartitionId": 1},
            {"name": "DDAjrSwAtv", "dataPartitionId": 1},
            {"name": "DrSbltAtc", "dataPartitionId": 1},
            {"name": "DrvThrtlOvrdIO", "dataPartitionId": 1},
            {"name": "Electric_Park_Brake_Switch", "dataPartitionId": 1},
            {"name": "AutoBrkngAvlbl", "dataPartitionId": 1},
            {"name": "Engine_Cooling_Temperature_ECT", "dataPartitionId": 1},
            {"name": "Engine_Torque", "dataPartitionId": 2},
            {"name": "FwdClnAlrtPr", "dataPartitionId": 2},
            {"name": "High_Beam", "dataPartitionId": 2},
            {"name": "HndsOffStrWhlDtMd", "dataPartitionId": 2},
            {"name": "HndsOffStrWhlDtSt", "dataPartitionId": 2},
            {"name": "Ky_IdDevPr", "dataPartitionId": 2},
            {"name": "LnCat", "dataPartitionId": 2},
            {"name": "Low_Beam", "dataPartitionId": 2},
            {"name": "Main_Light_Switch", "dataPartitionId": 3},
            {"name": "OtsAirTmp", "dataPartitionId": 3},
            {"name": "PDAjrSwAtv", "dataPartitionId": 3},
            {"name": "Parking_Brake_State", "dataPartitionId": 3},
            {"name": "PsSbltAtc", "dataPartitionId": 3},
            {"name": "PsngSysLat", "dataPartitionId": 3},
            {"name": "PsngSysLong", "dataPartitionId": 3},
            {"name": "RLDoorAjarSwAct", "dataPartitionId": 3},
            {"name": "RRDoorAjarSwAct", "dataPartitionId": 4},
            {"name": "TCSysDisSwAtv_12A", "dataPartitionId": 4},
            {"name": "Throttle__Position", "dataPartitionId": 4},
            {"name": "TireLFPrs", "dataPartitionId": 4},
            {"name": "TireLRPrs", "dataPartitionId": 4},
            {"name": "TireRFPrs", "dataPartitionId": 4},
            {"name": "TireRRPrs", "dataPartitionId": 4},
            {"name": "Vehicle_Odometer", "dataPartitionId": 4},
        ]

        store_and_forward_config = [
            {
                "storageOptions": {
                    "maximumSizeInBytes": 200000,
                    "storageLocation": partition_storage_location + str(i),
                    "minimumTimeToLiveInSeconds": 600,
                },
                "uploadOptions": {"conditionTree": f"{forward_signal} == 1.0"},
            }
            for i in range(5)
        ]

        collect_scheme = {
            "campaignSyncId": campaign_sync_id,
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "expression": f"{collect_signal} > 0.0",
                    "minimumTriggerIntervalMs": 1000,
                }
            },
            "signalsToCollect": signals_to_collect,
            "storeAndForwardConfiguration": store_and_forward_config,
            "spoolingMode": "TO_DISK",
            "campaignArn": campaign_name,
        }

        self._send_collection_schemes([collect_scheme])

        with attach_perf(
            self.context.subprocess_helper,
            pid=self.context.fwe_process.pid,
            output_svg_file=tmp_path / "perf_fwe.svg",
        ):
            self.context.start_cpu_measurement()

            collect_start_time = time.time()
            self.context.set_can_signal(collect_signal, 1)
            # Collect Data until the store is full
            for i in range(5):
                self._wait_for_partitions_to_have_size(
                    campaign_name=campaign_name,
                    partition_locations=[partition_storage_location + str(i)],
                    size=100000,
                )

            self.context.set_can_signal(collect_signal, 0)
            collect_end_time = time.time()

            # Forward all data
            forward_start_time = time.time()
            self.context.set_can_signal(forward_signal, 1)
            # 100000 bytes per partition, 5 partitions: 500,000 bytes should be received
            self._wait_for_all_data_received(size=500000)
            self.context.set_can_signal(forward_signal, 0)
            forward_end_time = time.time()

            self.context.stop_cpu_measurement()

        print(f"Storing took {collect_end_time - collect_start_time:.4f} seconds")
        print(f"Forwarding took {forward_end_time - forward_start_time:.4f} seconds")

    def test_store_and_forward_multiple_campaigns_load_test(self, tmp_path: Path):
        collect_signal = "Electric_Park_Brake_Switch"
        forward_signal = "Main_Light_Switch"

        available_signals = [
            "ABSActvProt",
            "ABSIO",
            "ABSPr",
            "ACCAct370",
            "ACCDrvrSeltdSpd",
            "AutoBrkngAct",
            "AutoBrkngAvlbl",
            "BPDAPS_BkPDrvApP",
            "Brake_Lights",
            "Brake_to_release_Parking_Brake",
            "DDAjrSwAtv",
            "DrSbltAtc",
            "DrvThrtlOvrdIO",
            "Electric_Park_Brake_Switch",
            "Engine_Airflow",
            "Engine_Cooling_Temperature_ECT",
            "Engine_Torque",
            "FwdClnAlrtPr",
            "High_Beam",
            "HndsOffStrWhlDtMd",
            "HndsOffStrWhlDtSt",
            "Ky_IdDevPr",
            "LnCat",
            "Low_Beam",
            "Main_Light_Switch",
            "OtsAirTmp",
            "PDAjrSwAtv",
            "Parking_Brake_State",
            "PsSbltAtc",
            "PsngSysLat",
            "PsngSysLong",
            "RLDoorAjarSwAct",
            "RRDoorAjarSwAct",
            "TCSysDisSwAtv_12A",
            "Throttle__Position",
            "TireLFPrs",
            "TireLRPrs",
            "TireRFPrs",
            "TireRRPrs",
            "Vehicle_Odometer",
        ]

        signals_to_collect = [
            {"name": signal, "dataPartitionId": 0} for signal in available_signals
        ]

        collection_schemes = []

        campaign_name_format = "load_test_multiple_campaigns_"
        partition_storage_location = "abc"

        for i in range(20):
            # Create 20 campaigns that store all available signals into partition 0
            campaign_name = campaign_name_format + str(i)
            campaign_sync_id = campaign_name + "#8713432"
            collect_scheme = {
                "campaignSyncId": campaign_sync_id,
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "expression": f"{collect_signal} > 0.0",
                        "minimumTriggerIntervalMs": 1000,
                    }
                },
                "signalsToCollect": signals_to_collect,
                "storeAndForwardConfiguration": [
                    {
                        "storageOptions": {
                            "maximumSizeInBytes": 600000,
                            "storageLocation": partition_storage_location,
                            "minimumTimeToLiveInSeconds": 600,
                        },
                        "uploadOptions": {"conditionTree": f"{forward_signal} == 1.0"},
                    },
                ],
                "spoolingMode": "TO_DISK",
                "campaignArn": campaign_name,
            }
            collection_schemes.append(collect_scheme)

        self._send_collection_schemes(collection_schemes)

        with attach_perf(
            self.context.subprocess_helper,
            pid=self.context.fwe_process.pid,
            output_svg_file=tmp_path / "perf_fwe.svg",
        ):
            self.context.start_cpu_measurement()

            collect_start_time = time.time()
            self.context.set_can_signal(collect_signal, 1)
            # Collect Data until the store is full
            for i in range(20):
                campaign_name = campaign_name_format + str(i)
                self._wait_for_partitions_to_have_size(
                    campaign_name=campaign_name,
                    partition_locations=[partition_storage_location],
                    size=300000,
                )
            self.context.set_can_signal(collect_signal, 0)
            collect_end_time = time.time()

            # Forward all data
            forward_start_time = time.time()
            self.context.set_can_signal(forward_signal, 1)
            # 300000 bytes per campaign, 20 campaigns: 6,000,000 bytes should be received
            self._wait_for_all_data_received(size=6000000)
            self.context.set_can_signal(forward_signal, 0)
            forward_end_time = time.time()

            self.context.stop_cpu_measurement()

        print(f"Storing took {collect_end_time - collect_start_time:.4f} seconds")
        print(f"Forwarding took {forward_end_time - forward_start_time:.4f} seconds")


class TestLoadSomeip:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, request: pytest.FixtureRequest, worker_number: int, someipigen):
        test_name_to_metric_map = {
            "test_load_test_someip": "test_load_test_someip",
        }
        metrics_name = test_name_to_metric_map[request.node.name]
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            use_someip_collection=True,
            someipigen_instance=someipigen.get_instance(),
            metrics_name=metrics_name,
        )

        self.random = Random(9837234)

        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_fwe()
        self.context.destroy_aws_resources()
        self.context.check_metrics_against_alarms()

    def create_campaign(self, random: Random):
        available_signals = [
            "Vehicle.ExampleSomeipInterface.X",
            "Vehicle.ExampleSomeipInterface.A1.A2.A",
            "Vehicle.ExampleSomeipInterface.A1.A2.B",
            "Vehicle.ExampleSomeipInterface.A1.A2.D",
            "Vehicle.ExampleSomeipInterface.A1.A2.E",
            "Vehicle.ExampleSomeipInterface.A1.A2.F",
            "Vehicle.ExampleSomeipInterface.A1.A2.G",
            "Vehicle.ExampleSomeipInterface.A1.A2.H",
            "Vehicle.ExampleSomeipInterface.A1.A2.I",
            "Vehicle.ExampleSomeipInterface.A1.A2.J",
            "Vehicle.ExampleSomeipInterface.A1.A2.K",
            "Vehicle.ExampleSomeipInterface.A1.A2.L",
            "Vehicle.ExampleSomeipInterface.A1.A2.M",
            "Vehicle.ExampleSomeipInterface.A1.A2.N",
            "Vehicle.ExampleSomeipInterface.A1.A2.O",
            "Vehicle.ExampleSomeipInterface.A1.A2.P",
            "Vehicle.ExampleSomeipInterface.A1.A2.Q",
            "Vehicle.ExampleSomeipInterface.A1.S",
            "Vehicle.ExampleSomeipInterface.A1.A",
            "Vehicle.ExampleSomeipInterface.A1.B",
            "Vehicle.ExampleSomeipInterface.A1.C",
            "Vehicle.ExampleSomeipInterface.A1.D",
            "Vehicle.ExampleSomeipInterface.A1.E",
            "Vehicle.ExampleSomeipInterface.A1.F",
            "Vehicle.ExampleSomeipInterface.A1.G",
            "Vehicle.ExampleSomeipInterface.A1.H",
            "Vehicle.ExampleSomeipInterface.A1.I",
            "Vehicle.ExampleSomeipInterface.A1.J",
            "Vehicle.ExampleSomeipInterface.A1.K",
            "Vehicle.ExampleSomeipInterface.A1.L",
            "Vehicle.ExampleSomeipInterface.Temperature",
        ]

        # 40 Heartbeat campaigns
        self.collection_schemes = []
        for i in range(40):
            campaign_sync_id = "load_test_heartbeat_someip_" + str(i)
            heartbeat_scheme = {
                "campaignSyncId": campaign_sync_id,
                "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 1000}},
                "signalsToCollect": [],
            }
            for signal in available_signals:
                if self.random.random() < 0.1:
                    heartbeat_scheme["signalsToCollect"].append(
                        {"name": signal, "minimumSamplingIntervalMs": 5000}
                    )
            self.collection_schemes.append(heartbeat_scheme)

        # 40 condition based campaigns
        condition_expression_1 = f"Vehicle.ExampleSomeipInterface.X > {random.uniform(0, 600)}"
        condition_expression_2 = (
            f"Vehicle.ExampleSomeipInterface.A1.A2.A < {random.uniform(0, 600)}"
        )
        condition_expression_3 = (
            f"Vehicle.ExampleSomeipInterface.A1.A2.B || {condition_expression_2}"
        )
        for i in range(40):
            campaign_sync_id = "load_test_condition_someip_" + str(i)
            condition_scheme = {
                "campaignSyncId": campaign_sync_id,
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "expression": f"{condition_expression_1} && {condition_expression_3}",
                        "minimumTriggerIntervalMs": 500,
                    },
                },
                "signalsToCollect": [],
            }
            for signal in available_signals:
                if self.random.random() < 0.1:
                    condition_scheme["signalsToCollect"].append(
                        {"name": signal, "minimumSamplingIntervalMs": 5000}
                    )
            self.collection_schemes.append(condition_scheme)

    def test_load_test_someip(self, someipigen, tmp_path: Path):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        self.create_campaign(self.random)
        self.context.send_collection_schemes(self.collection_schemes)

        with attach_perf(
            self.context.subprocess_helper,
            pid=self.context.fwe_process.pid,
            output_svg_file=tmp_path / "perf_fwe.svg",
        ):
            self.context.start_cpu_measurement()
            for i in range(0, 6000):
                someipigen.set_value("Vehicle.ExampleSomeipInterface.X", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.A", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.B", bool(i % 2))
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.D", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.E", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.F", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.G", bool(i % 2))
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.H", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.I", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.J", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.K", bool(i % 2))
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.L", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.M", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.N", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.O", bool(i % 2))
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.P", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.Q", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A", bool(i % 2))
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.B", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.C", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.D", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.E", bool(i % 2))
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.F", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.G", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.H", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.I", bool(i % 2))
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.J", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.K", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.L", i)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.S", str(i) * 10)
                someipigen.set_value("Vehicle.ExampleSomeipInterface.Temperature", i)
                time.sleep(0.01)
            self.context.stop_cpu_measurement()

        counter_per_collection_scheme = {}
        for r in self.context.received_data:
            arn = r.campaign_sync_id
            if arn not in counter_per_collection_scheme:
                counter_per_collection_scheme[arn] = 1
            else:
                counter_per_collection_scheme[arn] = counter_per_collection_scheme[arn] + 1

        log.info(
            "From the following collection schemes data was received: "
            + str(counter_per_collection_scheme)
        )

        assert len(counter_per_collection_scheme) > 40
        assert len(self.context.received_data) > 80


class TestDTCLoadTest:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, request: pytest.FixtureRequest, worker_number: int):
        # Explicitly map test to metric to force us to set new metric string when adding new tests.
        test_name_to_metric_map = {self.test_dtc_load_test.__name__: "test_dtc_load_test"}
        metrics_name = test_name_to_metric_map[request.node.name]

        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            number_channels=4,
            persistency_file=True,
            high_load=True,
            use_uds_dtc_generic_collection=True,
            metrics_name=metrics_name,
        )
        self.context.start_cyclic_can_messages()

        log.info("Setting initial values")
        # We intentially set fetch condition as false
        self.context.set_dtc("ECM_DTC3", 0xAF)
        self.context.set_dtc("TCU_DTC3", 0xAF)
        self.context.set_can_signal("Throttle__Position", 0)
        self.context.set_can_signal("Vehicle_Odometer", 30000)

        self.context.connect_to_cloud()

        self.available_actions = [
            'custom_function("DTC_QUERY", -1, 4, -1)',
            'custom_function("DTC_QUERY", 1, 2, -1)',
            'custom_function("DTC_QUERY", 1, 6, -1)',
            'custom_function("DTC_QUERY", 1, 4, 144)',
        ]
        self.random = Random(9837234)
        self.collection_schemes = []

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_fwe()
        self.context.stop_cyclic_can_messages()
        self.context.destroy_aws_resources()
        self.context.check_metrics_against_alarms()

    def load_and_validate_dtc_json(self, val):
        dtc_info = json.loads(val)
        with open(self.context.script_path + "../../../interfaces/uds-dtc/udsDtcSchema.json") as fp:
            dtc_schema = json.load(fp)
        jsonschema.validate(dtc_info, schema=dtc_schema)
        return dtc_info

    def test_dtc_load_test(self, tmp_path: Path):

        self.context.start_fwe()
        self.context.send_decoder_manifest()

        number_of_campaigns = 20

        # This test will run 20 campaigns with 2 fetch requests each: one periodical and one
        # conditional. Conditional fetch will trigger every 3s. The throttling in static config
        # is also set to 3s to avoid request spamming.
        # Each fetch request contains at least one and up to 4 actions. Three out of four available
        # actions require sequential requests.
        # The overall load on the system will then be 40 (best case) to 160 (worst case) actions
        # multiplied by worst case 3 requests per action being processed by edge.
        # The expectation for this test is to not to have data loss for all campaigns and have at
        # least one sample collected for all of them over the period of 150s
        for i in range(number_of_campaigns // 2):  # :2 since there are two campaigns per counter
            heartbeat = {
                "campaignSyncId": "heartbeat_" + str(i),
                "collectionScheme": {
                    "timeBasedCollectionScheme": {
                        "periodMs": max(int(self.random.gauss(10000, 2000)), 500),
                    }
                },
                "signalsToCollect": [
                    {"name": "Vehicle.ECU1.DTC_INFO"},
                ],
                "signalsToFetch": [
                    {
                        "fullyQualifiedName": "Vehicle.ECU1.DTC_INFO",
                        "signalFetchConfig": {
                            "timeBased": {
                                "executionFrequencyMs": max(
                                    int(self.random.gauss(10000, 2000)), 500
                                ),
                            }
                        },
                        "actions": [],
                    },
                    {
                        "fullyQualifiedName": "Vehicle.ECU1.DTC_INFO",
                        "signalFetchConfig": {
                            "conditionBased": {
                                "conditionExpression": "Throttle__Position > "
                                f"{self.random.uniform(0, 100)}",
                                "triggerMode": "ALWAYS",
                            }
                        },
                        "actions": [],
                    },
                ],
            }
            conditional = {
                "campaignSyncId": "conditional_" + str(i),
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "expression": f"BPDAPS_BkPDrvApP > {self.random.uniform(0, 9000)}",
                        "minimumTriggerIntervalMs": max(int(self.random.gauss(25000, 10000)), 500),
                    },
                },
                "signalsToCollect": [
                    {"name": "BPDAPS_BkPDrvApP"},
                    {"name": "Vehicle.ECU1.DTC_INFO"},
                ],
                "signalsToFetch": [
                    {
                        "fullyQualifiedName": "Vehicle.ECU1.DTC_INFO",
                        "signalFetchConfig": {
                            "timeBased": {
                                "executionFrequencyMs": max(
                                    int(self.random.gauss(10000, 2000)), 500
                                ),
                            }
                        },
                        "actions": [],
                    },
                    {
                        "fullyQualifiedName": "Vehicle.ECU1.DTC_INFO",
                        "signalFetchConfig": {
                            "conditionBased": {
                                "conditionExpression": "Throttle__Position > "
                                f"{self.random.uniform(0, 100)}",
                                "triggerMode": "ALWAYS",
                            }
                        },
                        "actions": [],
                    },
                ],
            }

            if self.random.random() < 0.50:
                heartbeat["signalsToFetch"][1]["signalFetchConfig"]["conditionBased"][
                    "triggerMode"
                ] = "RISING_EDGE"
            if self.random.random() < 0.50:
                conditional["signalsToFetch"][1]["signalFetchConfig"]["conditionBased"][
                    "triggerMode"
                ] = "RISING_EDGE"

            self.collection_schemes.append(heartbeat)
            self.collection_schemes.append(conditional)

        for action in self.available_actions:
            for campaign in self.collection_schemes:
                for fetch_signal in campaign["signalsToFetch"]:
                    if self.random.random() < 0.30:
                        fetch_signal["actions"].append(action)
                    # If no actions were added randomly, add at least one
                    if len(fetch_signal["actions"]) == 0:
                        fetch_signal["actions"].append(self.available_actions[0])

        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(self.collection_schemes)
        log.info("Wait 60 seconds changing signals every 10ms")

        # perf uses sampling, which has a very low overhead. We can always enable it for the
        # load test so that we can generate flame graphs as a CI artifact.
        with attach_perf(
            self.context.subprocess_helper,
            pid=self.context.fwe_process.pid,
            output_svg_file=tmp_path / "perf_fwe.svg",
        ):
            self.context.start_cpu_measurement()
            for i in range(0, 15000):
                self.context.set_can_signal("BPDAPS_BkPDrvApP", self.random.uniform(0, 9000))
                self.context.set_can_signal("Throttle__Position", self.random.uniform(1, 100))
                # Every 3s reset the trigger condition to not to spam fetch requests
                if i % 3 == 0:
                    self.context.set_can_signal("Throttle__Position", 0)
                time.sleep(0.01)
            self.context.stop_cpu_measurement()

        counter_per_collection_scheme = {}
        for r in self.context.received_data:
            arn = r.campaign_sync_id
            if arn not in counter_per_collection_scheme:
                counter_per_collection_scheme[arn] = 1
            else:
                counter_per_collection_scheme[arn] = counter_per_collection_scheme[arn] + 1

        log.info(
            "From the following collection schemes data was received: "
            + str(counter_per_collection_scheme)
        )
        assert len(counter_per_collection_scheme) == len(self.collection_schemes)
