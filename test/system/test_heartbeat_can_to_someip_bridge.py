# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
import time
from pathlib import Path

import pytest
from testframework.common import Retrying
from testframework.context import Context

log = logging.getLogger(__name__)


class TestSomeipToCan:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, worker_number: int, can_to_someip: str):
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            use_can_to_someip=True,
            someip_instance_id_can=can_to_someip,
        )
        self.context.start_cyclic_can_messages()

        log.info("Setting initial values")
        self.context.set_can_signal("Engine_Airflow", 0.0)
        self.context.set_can_signal("Parking_Brake_State", 0)

        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_can_messages()
        self.context.stop_fwe()
        self.context.destroy_aws_resources()

    def test_heartbeat(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        collection_schemes = [
            {
                "campaignSyncId": "heartbeat",
                "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 1000}},
                "signalsToCollect": [
                    {"name": "TireRRPrs"},
                    {"name": "Parking_Brake_State"},
                    {"name": "Throttle__Position"},
                ],
            }
        ]
        self.context.send_collection_schemes(collection_schemes)
        log.info("Wait for two triggers with interval 5")
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) >= 2

        log.info("first received data:" + str(self.context.received_data[0]))
        log.info("second received data:" + str(self.context.received_data[1]))

        assert (
            self.context.received_data[1].receive_timestamp
            >= self.context.received_data[0].receive_timestamp
        )

        timediff = (
            self.context.received_data[1].receive_timestamp
            - self.context.received_data[0].receive_timestamp
        )

        assert timediff > 900
        assert timediff < 1100

        # check CAN timestamp are valid epoch timestamp
        signals = self.context.received_data[1].get_signal_values_with_timestamps(
            "Throttle__Position"
        )
        assert len(signals) >= 1
        signal_timestamp = (
            signals[0].relative_timestamp + self.context.received_data[1].receive_timestamp
        )
        assert (
            abs(time.time() - signal_timestamp / 1000) < 10 * 60
        ), "system under test and test platform have more than 10 minutes time difference"
