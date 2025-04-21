# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
import time
from pathlib import Path

import pytest
from tenacity import stop_after_delay
from testframework.common import Retrying
from testframework.context import Context

log = logging.getLogger(__name__)


class TestTwoCollectionSchemes:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, worker_number: int):
        self.context = Context(tmp_path=tmp_path, worker_number=worker_number)
        self.context.start_cyclic_can_messages()

        log.info("Setting initial values")
        self.context.set_can_signal("Engine_Torque", -200.0)
        self.context.set_can_signal("Parking_Brake_State", 0)

        self.context.connect_to_cloud()
        self.collection_scheme_engine_torque = {
            "campaignSyncId": "collection_scheme_engine_torque",
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "expression": "Engine_Torque > -100",
                    "minimumTriggerIntervalMs": 5000,
                }
            },
            "signalsToCollect": [{"name": "TireRRPrs"}],
            "postTriggerCollectionDuration": 3000,
        }
        self.collection_scheme_parking_brake = {
            "campaignSyncId": "collection_scheme_parking_brake",
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "expression": "Parking_Brake_State == 1.0",
                    "minimumTriggerIntervalMs": 5000,
                }
            },
            "signalsToCollect": [{"name": "Throttle__Position"}],
            "postTriggerCollectionDuration": 3000,
        }

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_can_messages()
        self.context.stop_fwe()
        self.context.destroy_aws_resources()

    def test_both_collection_schemes_trigger(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(
            [self.collection_scheme_engine_torque, self.collection_scheme_parking_brake]
        )
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        "collection_scheme_engine_torque",
                        "collection_scheme_parking_brake",
                    }
                )

        log.info("Wait 8 seconds to check no triggers with initial values")
        time.sleep(8)

        assert len(self.context.received_data) == 0

        log.info("Setting Engine_Torque for one second above -100")
        self.context.set_can_signal("Engine_Torque", -90)
        time.sleep(1)
        self.context.set_can_signal("Engine_Torque", -110)
        log.info("Wait 8 seconds for data to arrive")
        time.sleep(8)

        assert len(self.context.received_data) == 1
        log.info("received data:" + str(self.context.received_data[0]))
        assert self.context.received_data[0].campaign_sync_id == "collection_scheme_engine_torque"

        log.info("Setting Parking_Brake_State for one second to true")
        self.context.set_can_signal("Parking_Brake_State", 1)
        time.sleep(1)  # 1 second
        self.context.set_can_signal("Parking_Brake_State", 0)
        # Start accelerating as after time is 3 seconds the next 2 seconds should be recorded
        # Throttle position from 0 to 40
        log.info("Increase Throttle__Position every 50ms from 1 to 90")
        for i in range(1, 90):
            self.context.set_can_signal("Throttle__Position", i)
            # 50 milliseconds. In dbc the frame is sent every 12ms so we will see
            time.sleep(0.05)
            # the value staying at the same value for some time
        log.info("Wait 3 seconds")
        time.sleep(3)

        # One new set of data after seconds trigger with increasing throttle position
        assert len(self.context.received_data) == 2
        log.info("received data:" + str(self.context.received_data[1]))
        assert self.context.received_data[1].campaign_sync_id == "collection_scheme_parking_brake"
        signals = self.context.received_data[1].get_signal_values_with_timestamps(
            "Throttle__Position"
        )
        assert len(signals) >= 1
        # newest throttle position should be at least 30.0
        assert signals[0].value >= 30.0
        # values should increase
        assert signals[0].value >= signals[10].value

    def test_trigger_order_different_from_publish_order(self):
        collection_scheme_engine_torque_5s = {
            "campaignSyncId": "collection_scheme_engine_torque",
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "expression": "Engine_Torque > -100",
                    "minimumTriggerIntervalMs": 5000,
                }
            },
            "signalsToCollect": [{"name": "TireRRPrs"}],
            "postTriggerCollectionDuration": 5000,
        }
        collection_scheme_parking_brake_500ms = {
            "campaignSyncId": "collection_scheme_parking_brake",
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "expression": "Parking_Brake_State == 1.0",
                    "minimumTriggerIntervalMs": 5000,
                }
            },
            "signalsToCollect": [{"name": "Throttle__Position"}],
            "postTriggerCollectionDuration": 500,
        }

        self.context.start_fwe()
        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(
            [collection_scheme_engine_torque_5s, collection_scheme_parking_brake_500ms]
        )
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        "collection_scheme_engine_torque",
                        "collection_scheme_parking_brake",
                    }
                )

        # Wait more than 5s for trigger condition to be valid
        time.sleep(10)

        log.info("Setting Engine_Torque for one second above -100")
        self.context.set_can_signal("Engine_Torque", -90)
        time.sleep(1)
        self.context.set_can_signal("Engine_Torque", -110)

        log.info("Setting Parking_Brake_State for one second to true")
        self.context.set_can_signal("Parking_Brake_State", 1)
        time.sleep(1)  # 1 second
        self.context.set_can_signal("Parking_Brake_State", 0)

        log.info("Wait 10 seconds for both collectionSchemes data to arrive")
        time.sleep(10)

        assert len(self.context.received_data) == 2

        # First collectionScheme data arrived comes from second collectionScheme triggered
        # because of shorter after time
        log.info("received data:" + str(self.context.received_data[0]))
        assert self.context.received_data[0].campaign_sync_id == "collection_scheme_parking_brake"

        # Second collectionScheme data arrived comes from first collectionScheme triggered
        # because of longer after time
        log.info("received data:" + str(self.context.received_data[1]))
        assert self.context.received_data[1].campaign_sync_id == "collection_scheme_engine_torque"

    def test_trigger_before_collection_scheme(self):
        self.context.start_fwe()
        log.info("Setting Engine_Torque for one second above -100 before sending collectionScheme")
        self.context.set_can_signal("Engine_Torque", -90)
        time.sleep(1)
        self.context.set_can_signal("Engine_Torque", -110)
        log.info("Wait 5 seconds then send collectionScheme")
        time.sleep(5)
        assert len(self.context.received_data) == 0
        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(
            [self.collection_scheme_engine_torque, self.collection_scheme_parking_brake]
        )
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        "collection_scheme_engine_torque",
                        "collection_scheme_parking_brake",
                    }
                )

        log.info("Wait 6 seconds then check if data is sent")
        time.sleep(6)
        assert len(self.context.received_data) == 0

    def test_trigger_time_based_condition_based_different_sampling(self):

        collection_scheme_engine_torque_5s = {
            "campaignSyncId": "collection_scheme_engine_torque",
            "collectionScheme": {
                "conditionBasedCollectionScheme": {
                    "expression": "Engine_Torque > -100",
                    "minimumTriggerIntervalMs": 5000,
                }
            },
            "signalsToCollect": [
                {"name": "TireRRPrs", "minimumSamplingIntervalMs": 100},
                {"name": "Engine_Torque", "minimumSamplingIntervalMs": 100},
            ],
            "postTriggerCollectionDuration": 5000,
        }
        heartbeat = {
            "campaignSyncId": "heartbeat",
            "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 5000}},
            "signalsToCollect": [{"name": "Engine_Torque"}],
        }
        self.context.set_can_signal("Engine_Torque", -110)
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        self.context.send_collection_schemes([collection_scheme_engine_torque_5s, heartbeat])
        for attempt in Retrying():
            with attempt:
                self.context.verify_checkin_documents(
                    {
                        self.context.proto_factory.decoder_manifest_sync_id,
                        "collection_scheme_engine_torque",
                        "heartbeat",
                    }
                )

        for attempt in Retrying(stop=stop_after_delay(12)):
            with attempt:
                assert len(self.context.received_data) >= 2

        assert self.context.received_data[1].campaign_sync_id == "heartbeat"

        number_messages_received_before = len(self.context.received_data)
        log.info("Setting Engine_Torque for one second above -100")
        self.context.set_can_signal("Engine_Torque", -90)
        time.sleep(10)
        self.context.set_can_signal("Engine_Torque", -110)
        time.sleep(1)
        new_elements = self.context.received_data[number_messages_received_before:]
        assert len([e for e in new_elements if e.campaign_sync_id == "heartbeat"]) >= 1
        assert (
            len(
                [e for e in new_elements if e.campaign_sync_id == "collection_scheme_engine_torque"]
            )
            >= 1
        )
