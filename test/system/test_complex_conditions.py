# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
import time
from pathlib import Path

import pytest
from testframework.context import Context

log = logging.getLogger(__name__)


@pytest.mark.parametrize(
    "setup",
    [{"use_faketime": False}, {"use_faketime": True}],
    indirect=True,
    ids=str,
)
class TestComplexConditionsCollectionScheme:
    @pytest.fixture(autouse=True, params=[{}])
    def setup(self, tmp_path: Path, request: pytest.FixtureRequest, worker_number: int):
        self.context_kwargs = request.param
        self.context = Context(
            tmp_path=tmp_path, worker_number=worker_number, **self.context_kwargs
        )
        self.context.start_cyclic_can_messages()

        log.info("Setting initial values")
        self.context.set_can_signal("Engine_Airflow", 50.0)
        self.context.set_can_signal("Parking_Brake_State", 0)

        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_can_messages()
        self.context.stop_fwe()
        self.context.destroy_aws_resources()

    def test_trigger_on_change(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        # Since the window time is 1000ms, it is possible for 2 triggers to occur in 2000ms, so
        # set minimum_publish_interval to be > 2000ms to prevent 2 triggers occurring on value
        # change.
        collection_schemes = [
            {
                "campaignSyncId": "triggerOnlyOnChange",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "expression": "last_window_min(Engine_Airflow) != Engine_Airflow"
                        " || last_window_max(Engine_Airflow) != Engine_Airflow",
                        "minimumTriggerIntervalMs": 2100,
                    }
                },
                "signalsToCollect": [
                    {"name": "Engine_Airflow"},
                    {"name": "Parking_Brake_State"},
                ],
            }
        ]
        self.context.send_collection_schemes(collection_schemes)
        log.info("Wait 20 to assure no triggers")
        time.sleep(20)

        assert len(self.context.received_data) == 0
        self.context.set_can_signal("Engine_Airflow", 60.0)
        log.info("After change wait 10 to assure one trigger")
        time.sleep(10)
        assert len(self.context.received_data) == 1  # After change data should be sent
        log.info("Wait 10 to assure no triggers")
        time.sleep(10)
        assert len(self.context.received_data) == 1
        self.context.set_can_signal("Engine_Airflow", 50.0)
        log.info("After change wait 10 to assure one trigger")
        time.sleep(10)
        assert len(self.context.received_data) == 2  # After second change again data should be sent
