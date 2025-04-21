# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
import time
from pathlib import Path

import pytest
from testframework.common import Retrying
from testframework.context import Context

log = logging.getLogger(__name__)


class TestSomeip:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, worker_number: int, someipigen):
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            use_someip_collection=True,
            someipigen_instance=someipigen.get_instance(),
        )

        log.info("Setting initial values")
        someipigen.set_value("Vehicle.ExampleSomeipInterface.X", 555)
        someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.A", 123)
        someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.B", True)
        someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.D", 456.789)
        someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.S", "no_trigger")

        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_fwe()
        self.context.destroy_aws_resources()

    def test_string_condition_someip(self, someipigen):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        collection_schemes = [
            {
                "campaignSyncId": "string_condition_1",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "expression": 'Vehicle.ExampleSomeipInterface.A1.S == "trigger_1"',
                        "minimumTriggerIntervalMs": 500,
                    },
                },
                "signalsToCollect": [
                    {"name": "Vehicle.ExampleSomeipInterface.X"},
                    {"name": "Vehicle.ExampleSomeipInterface.A1.A2.A"},
                    {"name": "Vehicle.ExampleSomeipInterface.A1.A2.B"},
                    {"name": "Vehicle.ExampleSomeipInterface.A1.A2.D"},
                    {"name": "Vehicle.ExampleSomeipInterface.A1.S"},
                ],
            },
            {
                "campaignSyncId": "string_condition_2",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "expression": 'Vehicle.ExampleSomeipInterface.A1.S == "trigger_2"',
                        "minimumTriggerIntervalMs": 500,
                    },
                },
                "signalsToCollect": [
                    {"name": "Vehicle.ExampleSomeipInterface.X"},
                    {"name": "Vehicle.ExampleSomeipInterface.A1.A2.A"},
                    {"name": "Vehicle.ExampleSomeipInterface.A1.A2.B"},
                    {"name": "Vehicle.ExampleSomeipInterface.A1.A2.D"},
                    {"name": "Vehicle.ExampleSomeipInterface.A1.S"},
                ],
            },
        ]
        self.context.send_collection_schemes(collection_schemes)

        # Verify the checkin of the collection schemes
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        collection_schemes[0]["campaignSyncId"],
                        collection_schemes[1]["campaignSyncId"],
                    }
                )
        someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.S", "trigger_1")

        # Condition triggered for the first campaign
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) >= 2
                for received_data in self.context.received_data:
                    campaign_sync_id = received_data.campaign_sync_id
                    assert campaign_sync_id == collection_schemes[0]["campaignSyncId"]

                # check timestamp are valid epoch timestamp
                signals = self.context.received_data[1].get_signal_values_with_timestamps(
                    "Vehicle.ExampleSomeipInterface.X"
                )
                assert len(signals) >= 1
                signal_timestamp = (
                    signals[0].relative_timestamp + self.context.received_data[1].receive_timestamp
                )
                assert signals[0].value is not None  # Verify the signal value is not None
                assert (
                    abs(time.time() - signal_timestamp / 1000) < 10 * 60
                ), "system under test and test platform have more than 10 minutes time difference"
