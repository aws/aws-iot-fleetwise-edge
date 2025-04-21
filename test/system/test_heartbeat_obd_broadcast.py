# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
from pathlib import Path

import pytest
from testframework.common import Retrying
from testframework.context import Context

log = logging.getLogger(__name__)


class TestHeartbeatCollectionScheme:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, worker_number: int):
        self.context = Context(
            tmp_path=tmp_path, worker_number=worker_number, use_obd_broadcast=True
        )
        self.context.start_cyclic_can_messages()

        log.info("Setting initial values")
        self.context.set_can_signal("Engine_Airflow", 0.0)
        self.context.set_can_signal("Parking_Brake_State", 0)
        self.context.set_obd_signal("ENGINE_SPEED", 1000.0)
        self.context.set_obd_signal("ENGINE_OIL_TEMPERATURE", 98.99)
        self.context.set_obd_signal("CONTROL_MODULE_VOLTAGE", 14.5)

        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_can_messages()
        self.context.stop_fwe()
        self.context.destroy_aws_resources()

    def test_heartbeat_interval(self):
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
                    {"name": "ENGINE_SPEED"},
                    {"name": "ENGINE_OIL_TEMPERATURE"},
                    {"name": "CONTROL_MODULE_VOLTAGE"},
                ],
            }
        ]
        self.context.send_collection_schemes(collection_schemes)
        log.info("Wait for three triggers with interval 5")
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) >= 3
                signals = self.context.received_data[-1].get_signal_values_with_timestamps(
                    "ENGINE_SPEED"
                )
                assert len(signals) >= 1

        log.info("first received data:" + str(self.context.received_data[0]))
        log.info("second received data:" + str(self.context.received_data[1]))
        log.info("third received data:" + str(self.context.received_data[2]))

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

        # check OBD arrived at least in third message - 2s flush time due to
        # ignore_unsupported_pid_requests prevents data coming earlier:
        signals = self.context.received_data[-1].get_signal_values_with_timestamps("ENGINE_SPEED")
        assert len(signals) >= 1
        log.info("ENGINE_SPEED: " + str(signals[0].value))
        assert signals[0].value > 990.0
        assert signals[0].value < 1010.0

        signals = self.context.received_data[-1].get_signal_values_with_timestamps(
            "ENGINE_OIL_TEMPERATURE"
        )
        assert len(signals) >= 1
        log.info("ENGINE_OIL_TEMPERATURE: " + str(signals[0].value))
        assert signals[0].value > 97.0
        assert signals[0].value < 100.0

        signals = self.context.received_data[-1].get_signal_values_with_timestamps(
            "CONTROL_MODULE_VOLTAGE"
        )
        assert len(signals) >= 1
        log.info("CONTROL_MODULE_VOLTAGE: " + str(signals[0].value))
        assert signals[0].value > 13.0
        assert signals[0].value < 16.0
