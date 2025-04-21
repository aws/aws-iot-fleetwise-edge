# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
from datetime import datetime
from pathlib import Path

import pytest
from tenacity import stop_after_delay
from testframework.common import Retrying
from testframework.context import Context

log = logging.getLogger(__name__)


class TestHeartbeatWithRemoteProfiler:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, request: pytest.FixtureRequest, worker_number: int):
        # Explicitly map test to metric to force us to set new metric string when adding new tests.
        test_name_to_metric_map = {
            self.test_heartbeat_with_remote_profiler.__name__: "heartbeat_with_remote_profiler"
        }
        metrics_name = test_name_to_metric_map[request.node.name]

        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            enable_remote_profiler=True,
            persistency_file=True,
            metrics_name=metrics_name,
        )  # persistency_file= False will trigger an error in the logs
        self.context.start_cyclic_can_messages()

        log.info("Setting initial values")
        self.context.set_can_signal("Engine_Airflow", 0.0)
        self.context.set_can_signal("Parking_Brake_State", 0)
        self.context.set_obd_signal("ENGINE_SPEED", 1000.0)
        self.context.set_obd_signal("ENGINE_OIL_TEMPERATURE", 98.99)
        self.context.set_obd_signal("CONTROL_MODULE_VOLTAGE", 14.5)

        self.context.connect_to_cloud()
        # collects an OBD signal ENGINE_SPEED
        self.collection_schemes = [
            {
                "campaignSyncId": "heartbeat",
                "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 5000}},
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

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_fwe()
        # Stop canigen after FWE to prevent OBD errors in the log:
        self.context.stop_cyclic_can_messages()
        self.context.destroy_aws_resources()

    def test_heartbeat_with_remote_profiler(self):
        self.context.start_fwe()
        start_time = datetime.now()
        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(self.collection_schemes)
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents({"decoder_manifest_1", "heartbeat"})

        for attempt in Retrying(stop=stop_after_delay(240)):
            with attempt:
                # Note that this is a new metric unique for this test run. It can take up to 2 min
                # for new metrics to appear on CloudWatch:
                # https://docs.aws.amazon.com/AmazonCloudWatch/latest/monitoring/publishingMetrics.html#publishingDataPoints
                log.info("Waiting for remote profiler to upload metrics to CloudWatch")
                metrics = self.context.get_remote_profiler_metrics(start_time)
                assert len(metrics["variableMaxSinceStartup_RFrames0_id0"]) > 1
                assert (
                    metrics["variableMaxSinceStartup_RFrames0_id0"][1]["value"]
                    > metrics["variableMaxSinceStartup_RFrames0_id0"][0]["value"]
                )

        log.info(f"RemoteProfiler uploaded: {metrics}")

        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) > 1

        # check OBD arrived at least in second message:
        signals = self.context.received_data[1].get_signal_values_with_timestamps("ENGINE_SPEED")

        assert len(signals) >= 1
        log.info("ENGINE_SPEED: " + str(signals[0].value))
        assert signals[0].value > 990.0
        assert signals[0].value < 1010.0
