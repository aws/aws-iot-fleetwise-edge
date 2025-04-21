# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import logging
import os
import time
from pathlib import Path

import boto3
import pytest
from testframework.common import Retrying
from testframework.context import Context

log = logging.getLogger(__name__)


@pytest.mark.parametrize(
    "setup",
    [
        {"expression": "custom_function('abs', Engine_Airflow) == 60.0", "set_value": -60.0},
        {"expression": "custom_function('max', 10.0, Engine_Airflow) == 60.0", "set_value": 60.0},
        {
            "expression": "custom_function('min', -10.0, Engine_Airflow) == -60.0",
            "set_value": -60.0,
        },
        {"expression": "custom_function('pow', 2.0, Engine_Airflow) == 32.0", "set_value": 5.0},
        {"expression": "custom_function('log', 2.0, Engine_Airflow) == 5.0", "set_value": 32.0},
        {"expression": "custom_function('ceil', Engine_Airflow) == 124.0", "set_value": 123.5},
        {"expression": "custom_function('floor', Engine_Airflow) == 123.0", "set_value": 123.5},
    ],
    ids=str,
    indirect=True,
)
class TestCustomFunctionMath:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, worker_number: int, request: pytest.FixtureRequest):
        self.params = request.param
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
        )
        self.context.start_cyclic_can_messages()

        log.info("Setting initial values")
        self.context.set_can_signal("Engine_Airflow", 1.0)

        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_can_messages()
        self.context.stop_fwe()
        self.context.destroy_aws_resources()

    def test_custom_function_math(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        collection_schemes = [
            {
                "campaignSyncId": "customFunctionCampaignSyncId",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "expression": self.params["expression"],  # noqa: E501
                        "minimumTriggerIntervalMs": 2100,
                    }
                },
                "signalsToCollect": [
                    {"name": "Engine_Airflow"},
                ],
            }
        ]
        self.context.send_collection_schemes(collection_schemes)
        log.info("Wait 8 seconds to check no triggers with initial values")
        time.sleep(8)
        assert len(self.context.received_data) == 0

        log.info("Set value for 1s and wait for collected data")
        self.context.set_can_signal("Engine_Airflow", self.params["set_value"])
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) >= 1
                signals = self.context.received_data[-1].get_signal_values_with_timestamps(
                    "Engine_Airflow"
                )
                assert len(signals) >= 1
                assert signals[0].value == self.params["set_value"]


class TestCustomFunctionMultiRisingEdgeTrigger:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, worker_number: int):
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            custom_decoder_json_files=[
                "../../tools/cloud/custom-decoders-multi-rising-edge-trigger.json"
            ],
            node_json_files=["../../tools/cloud/custom-nodes-multi-rising-edge-trigger.json"],
            use_named_signal_data_source=True,
        )
        self.context.start_cyclic_can_messages()

        log.info("Setting initial values")
        self.context.set_can_signal("Engine_Airflow", 0)
        self.context.set_can_signal("Parking_Brake_State", 0)

        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_can_messages()
        self.context.stop_fwe()
        self.context.destroy_aws_resources()

    def test_custom_function_multi_rising_edge_trigger(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        collection_schemes = [
            {
                "campaignSyncId": "customFunctionCampaignSyncId",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "expression": "custom_function('MULTI_RISING_EDGE_TRIGGER', 'Engine_Airflow', Engine_Airflow, 'Parking_Brake_State', Parking_Brake_State)",  # noqa: E501
                    }
                },
                "signalsToCollect": [
                    {"name": "Vehicle.MultiRisingEdgeTrigger"},
                ],
            }
        ]
        self.context.send_collection_schemes(collection_schemes)

        # Verify the checkin of the collection schemes
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        collection_schemes[0]["campaignSyncId"],
                    }
                )

        log.info("Wait 8 seconds to check no triggers")
        time.sleep(8)
        assert len(self.context.received_data) == 0

        log.info("Set first signal and wait for collected data")
        self.context.set_can_signal("Engine_Airflow", 1)
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) == 1
                signals = self.context.received_data[-1].get_signal_values_with_timestamps(
                    "Vehicle.MultiRisingEdgeTrigger"
                )
                assert len(signals) == 1
                assert signals[0].value == '["Engine_Airflow"]'

        log.info("Set second signal and wait for collected data")
        self.context.set_can_signal("Parking_Brake_State", 1)
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) == 2
                signals = self.context.received_data[-1].get_signal_values_with_timestamps(
                    "Vehicle.MultiRisingEdgeTrigger"
                )
                assert len(signals) == 1
                assert signals[0].value == '["Parking_Brake_State"]'

    def test_custom_function_multi_rising_edge_trigger_with_unknown_signal(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        collection_schemes = [
            {
                "campaignSyncId": "customFunctionCampaignSyncId",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "expression": "custom_function('MULTI_RISING_EDGE_TRIGGER', 'Engine_Airflow', Engine_Airflow)",  # noqa: E501
                    }
                },
                "signalsToCollect": [
                    {"name": "Vehicle.MultiRisingEdgeTrigger"},
                ],
            }
        ]
        # Add an extra condition to the function with an unknown signal ID
        self.context.proto_factory.create_collection_schemes_proto(collection_schemes)
        collection_scheme = self.context.proto_factory.collection_schemes_proto.collection_schemes[
            0
        ]
        params = (
            collection_scheme.condition_based_collection_scheme.condition_tree.node_function.custom_function.params  # noqa: E501
        )
        extra_param_1 = params.add()
        extra_param_1.node_string_value = "unknown"
        extra_param_2 = params.add()
        extra_param_2.node_signal_id = 12345
        collected_signal = collection_scheme.signal_information.add()
        collected_signal.signal_id = extra_param_2.node_signal_id
        collected_signal.sample_buffer_size = 750
        collected_signal.minimum_sample_period_ms = 0
        collected_signal.fixed_window_period_ms = 0
        collected_signal.condition_only_signal = True
        self.context.send_collection_schemes_proto(
            self.context.proto_factory.collection_schemes_proto
        )

        # Verify the checkin of the collection schemes
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        collection_schemes[0]["campaignSyncId"],
                    }
                )

        log.info("Wait 8 seconds to check no triggers")
        time.sleep(8)
        assert len(self.context.received_data) == 0

        log.info("Set first signal and wait for collected data")
        self.context.set_can_signal("Engine_Airflow", 1)
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) == 1
                signals = self.context.received_data[-1].get_signal_values_with_timestamps(
                    "Vehicle.MultiRisingEdgeTrigger"
                )
                assert len(signals) == 1
                assert signals[0].value == '["Engine_Airflow"]'


@pytest.mark.parametrize(
    "setup",
    [
        {"python_config": {"micropython": {}}},
        {"python_config": {"cpython": {}}},
    ],
    ids=str,
    indirect=True,
)
class TestCustomFunctionPython:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, worker_number: int, request: pytest.FixtureRequest):
        self.params = request.param

        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            custom_decoder_json_files=["../../tools/cloud/custom-decoders-histogram.json"],
            node_json_files=["../../tools/cloud/custom-nodes-histogram.json"],
            use_named_signal_data_source=True,
            use_script_engine=True,
            extra_static_config=self.params["python_config"],
            persistency_file=True,
        )

        s3_client = boto3.client("s3")
        s3_client.upload_file(
            "../../tools/cloud/custom-function-python-histogram/histogram.py",
            os.environ["TEST_S3_BUCKET_NAME"],
            f"custom-function-histogram-{self.context.random}/histogram.py",
        )

        self.context.start_cyclic_can_messages()

        log.info("Setting initial values")
        self.context.set_can_signal("Engine_Airflow", 0)

        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_can_messages()
        self.context.stop_fwe()
        self.context.destroy_aws_resources()

    def test_custom_function_python_histogram(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()

        collection_schemes = [
            {
                "campaignSyncId": "customFunctionCampaignSyncId",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "expression": f"custom_function('python', 'custom-function-histogram-{self.context.random}', 'histogram', Engine_Airflow)",  # noqa: E501
                    }
                },
                "signalsToCollect": [
                    {"name": "Vehicle.Histogram"},
                ],
            }
        ]
        self.context.send_collection_schemes(collection_schemes)

        # Verify the checkin of the collection schemes
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        collection_schemes[0]["campaignSyncId"],
                    }
                )

        log.info("Wait for first histogram")
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) == 1
                signals = self.context.received_data[-1].get_signal_values_with_timestamps(
                    "Vehicle.Histogram"
                )
                assert len(signals) == 1
        histogram_data = json.loads(signals[0].value)
        assert len(histogram_data) == 100
        assert histogram_data[49] == 1000  # All counts in bin 49 (val 0)
        assert histogram_data[54] == 0  # None in bin 54

        self.context.set_can_signal("Engine_Airflow", 100)

        log.info("Wait for second histogram")
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) == 2
                signals = self.context.received_data[-1].get_signal_values_with_timestamps(
                    "Vehicle.Histogram"
                )
                assert len(signals) == 1
        histogram_data = json.loads(signals[0].value)
        assert len(histogram_data) == 100
        assert histogram_data[54] > 0  # At least some counts in bin 54 (val 100)
