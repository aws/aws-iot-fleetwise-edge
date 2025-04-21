# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
import time
from datetime import timedelta
from pathlib import Path
from random import Random

import pytest
from testframework.context import Context

log = logging.getLogger(__name__)


class TestLongAvgLoad:
    @pytest.fixture(autouse=True)
    def setup(self, tmp_path: Path, request: pytest.FixtureRequest, worker_number: int):
        # Explicitly map test to metric to force us to set new metric string when adding new tests.
        test_name_to_metric_map = {self.test_load_test_1.__name__: "long_avg_load_test_1"}
        metrics_name = test_name_to_metric_map[request.node.name]

        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            number_channels=4,
            high_load=True,
            persistency_file=True,
            background_metrics=True,
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
        self.random = Random(9837234)
        self.create_collection_schemes(0, self.random)

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_cyclic_can_messages()
        self.context.stop_fwe()
        self.context.destroy_aws_resources()

    def create_collection_schemes(self, arn_counter, random: Random):
        self.collection_schemes = []
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

        # add heartbeats
        for i in range(1, 6):
            heartbeat = {
                "campaignSyncId": "heartbeat_" + str(i) + "_" + str(arn_counter),
                "collectionScheme": {
                    "timeBasedCollectionScheme": {
                        "periodMs": max(int(random.gauss(7000, 3000)), 100)
                    },
                },
                "signalsToCollect": [
                    {"name": "ENGINE_SPEED"},
                    {"name": "ENGINE_OIL_TEMPERATURE"},
                    {"name": "CONTROL_MODULE_VOLTAGE"},
                ],
            }
            for signal in available_signals:
                for channel_nr in range(1, 5):
                    # 10% chance
                    if random.random() < 0.15:
                        signal_name = signal
                        if channel_nr > 1:
                            signal_name += "_channel_" + str(channel_nr)
                        heartbeat["signalsToCollect"].append({"name": signal_name})
            self.collection_schemes.append(heartbeat)

        for i in range(1, 6):
            conditional = {
                "campaignSyncId": "conditional_" + str(i) + "_" + str(arn_counter),
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "expression": f"BPDAPS_BkPDrvApP > {random.uniform(0, 9000)}",
                        "minimumTriggerIntervalMs": max(int(random.gauss(5000, 2000)), 100),
                    },
                },
                "signalsToCollect": [{"name": "BPDAPS_BkPDrvApP"}],
            }
            self.collection_schemes.append(conditional)

    def test_load_test_1(self):
        log.info("collection schemes: " + str(self.collection_schemes))

        self.context.start_fwe()
        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(self.collection_schemes)
        log.info("Wait 23 hours changing signals every 10ms")

        start_time = time.time()
        different_campaigns_counter = 0
        different_campaigns_in_first_hour = 0
        i = 0
        last_received_len = 0
        # Run for more than 24 hours. Our scheduled pipeline runs every 24 hours, and the next run
        # will stop the current one and upload its logs.
        while (time.time() - start_time) < timedelta(hours=25).total_seconds():
            i = i + 1
            self.context.set_can_signal("BPDAPS_BkPDrvApP", self.random.uniform(0, 9000))
            # Every 30 seconds send "new" collection scheme
            if i % 3000 == 0:
                # Assure that at least one message arrives every 30 seconds
                assert self.context.receive_data_counter > last_received_len
                last_received_len = self.context.receive_data_counter
                different_campaigns_counter = different_campaigns_counter + 1
                self.create_collection_schemes(str(i), self.random)
                self.context.send_decoder_manifest()
                self.context.send_collection_schemes(self.collection_schemes)
                if different_campaigns_in_first_hour == 0 and time.time() - start_time > 60 * 60:
                    different_campaigns_in_first_hour = different_campaigns_counter
            time.sleep(0.01)

        counter_per_collection_scheme = {}
        condition_based_triggers = 0
        for r in self.context.received_data:
            arn = r.campaign_sync_id
            if arn.startswith("conditional_"):
                condition_based_triggers = condition_based_triggers + 1
            if arn not in counter_per_collection_scheme:
                counter_per_collection_scheme[arn] = 1
            else:
                counter_per_collection_scheme[arn] = counter_per_collection_scheme[arn] + 1

        log.info(
            "From the following collection schemes data was received: "
            + str(counter_per_collection_scheme)
        )
        # At least the two heartbeat should always trigger for the first hour
        assert len(counter_per_collection_scheme) > different_campaigns_in_first_hour * 2
        # At least 10% should trigger
        assert condition_based_triggers > different_campaigns_in_first_hour / 10
        log.info("first received data:" + str(self.context.received_data[0]))
        log.info("last received data:" + str(self.context.received_data[-1]))
