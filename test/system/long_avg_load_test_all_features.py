# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import json
import logging
import random
import threading
import time
from datetime import datetime, timedelta
from pathlib import Path
from random import Random
from typing import Optional

import pytest
from testframework.common import Retrying
from testframework.context import Context
from testframework.protofactory import (
    LastKnownStateCommandOperation,
    LastKnownStateSignalInformation,
    OnChangeUpdateStrategy,
    PeriodicUpdateStrategy,
    SignalType,
    StateTemplate,
)

log = logging.getLogger(__name__)


class TestLongAvgLoadAllFeatures:
    @pytest.fixture(autouse=True)
    def setup(
        self,
        tmp_path: Path,
        request: pytest.FixtureRequest,
        worker_number: int,
        someipigen,
        someip_device_shadow_editor,
    ):
        # Explicitly map test to metric to force us to set new metric string when adding new tests.
        test_name_to_metric_map = {
            self.test_load_test_all_features.__name__: "long_avg_load_test_all_features"
        }
        metrics_name = test_name_to_metric_map[request.node.name]
        self.command_count = 0

        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            number_channels=4,
            high_load=True,
            persistency_file=True,
            background_metrics=True,
            metrics_name=metrics_name,
            someip_commands_enabled=True,
            can_commands_enabled=True,
            use_uds_dtc_generic_collection=True,
            use_someip_collection=True,
            someipigen_instance=someipigen.get_instance(),
            use_someip_device_shadow=True,
            someip_device_shadow_editor_instance=someip_device_shadow_editor.get_instance(),
            last_known_state_enabled=True,
        )

        self.context.start_cyclic_can_messages()
        self.context.start_can_command_server()
        log.info("Setting initial values")
        self.context.set_dtc("ECM_DTC3", 0xAF)
        self.context.set_dtc("TCU_DTC3", 0xAF)

        self.context.set_can_signal("Engine_Airflow", 20.0)
        self.context.set_can_signal("Throttle__Position", 0)
        self.context.set_can_signal("Vehicle_Odometer", 30000)
        self.context.set_can_signal("Parking_Brake_State", 1.0)

        self.context.set_obd_signal("ENGINE_SPEED", 1000.0)
        self.context.set_obd_signal("ENGINE_OIL_TEMPERATURE", 98.99)
        self.context.set_obd_signal("CONTROL_MODULE_VOLTAGE", 14.5)

        someipigen.set_value("Vehicle.ExampleSomeipInterface.X", 555)
        someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.A", 123)
        someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.B", True)
        someipigen.set_value("Vehicle.ExampleSomeipInterface.A1.A2.D", 456.789)

        self.context.connect_to_cloud()

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

        self.available_signals_some_ip = [
            {"name": "Vehicle.ExampleSomeipInterface.X"},
            {"name": "Vehicle.ExampleSomeipInterface.A1.A2.A"},
            {"name": "Vehicle.ExampleSomeipInterface.A1.A2.B"},
            {"name": "Vehicle.ExampleSomeipInterface.A1.A2.D"},
        ]

        self.random = Random(9837234)
        self.create_collection_schemes(0, self.random)

        yield  # pytest fixture: before is setup, after is tear-down
        self.context.stop_can_command_server()
        self.context.stop_cyclic_can_messages()
        self.context.stop_fwe()
        self.context.destroy_aws_resources()

    def add_collection_schemes(self, arn_counter, random: Random):
        collection_schemes = []
        # add heartbeat campaigns
        for i in range(1, 4):
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
            for signal in self.available_signals:
                for channel_nr in range(1, 5):
                    # 15% chance
                    if random.random() < 0.15:
                        signal_name = signal
                        if channel_nr > 1:
                            signal_name += "_channel_" + str(channel_nr)
                        heartbeat["signalsToCollect"].append({"name": signal_name})
            collection_schemes.append(heartbeat)

        # add condition based campaigns
        for i in range(1, 4):
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
            collection_schemes.append(conditional)
        return collection_schemes

    def add_fetch_collection_schemes(self, arn_counter, random: Random):
        fetch_collection_schemes = []
        # add condition based fetch campaigns
        for i in range(1, 3):
            fetch_condition_based = {
                "campaignSyncId": "fetch_conditional_" + str(i) + str(arn_counter),
                "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 20000}},
                "signalsToCollect": [
                    {"name": "Vehicle.ECU1.DTC_INFO"},
                ],
                "signalsToFetch": [
                    {
                        "fullyQualifiedName": "Vehicle.ECU1.DTC_INFO",
                        "signalFetchConfig": {
                            "conditionBased": {
                                "conditionExpression": f"Throttle__Position > "
                                f"{random.uniform(0,100)}",
                                "triggerMode": "ALWAYS",
                            }
                        },
                        "actions": ['custom_function("DTC_QUERY", -1, 4, -1)'],
                    }
                ],
            }
            fetch_collection_schemes.append(fetch_condition_based)

        # add time based fetch campaigns
        for i in range(1, 3):
            fetch_time_based = {
                "campaignSyncId": "fetch_heartbeat_" + str(i) + str(arn_counter),
                "diagnosticsMode": "SEND_ACTIVE_DTCS",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "conditionLanguageVersion": 1,
                        "expression": "!isNull(Vehicle.ECU1.DTC_INFO)",
                        "minimumTriggerIntervalMs": 10000,
                        "triggerMode": "ALWAYS",
                    },
                },
                "signalsToCollect": [
                    {"name": "Vehicle.ECU1.DTC_INFO"},
                    {"name": "Vehicle_Odometer"},
                ],
                "signalsToFetch": [
                    {
                        "fullyQualifiedName": "Vehicle.ECU1.DTC_INFO",
                        "signalFetchConfig": {
                            "timeBased": {
                                "executionFrequencyMs": 10000,
                            }
                        },
                        "actions": ['custom_function("DTC_QUERY", -1, 4, -1)'],
                    }
                ],
            }
            fetch_collection_schemes.append(fetch_time_based)
        return fetch_collection_schemes

    def add_store_and_forward_collection_schemes(self, arn_counter, random: Random):
        store_and_forward_collection_schemes = []
        # add store and forward campaigns
        collect_signal = "Electric_Park_Brake_Switch"
        forward_signal = "Main_Light_Switch"
        for i in range(1, 4):
            partition_storage_location = "abc"
            campaign_name = "store_and_forward_" + str(i) + str(arn_counter)
            store_and_forward = {
                "campaignSyncId": campaign_name,
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "expression": f"{collect_signal} > 0.0",
                        "minimumTriggerIntervalMs": 5000,
                    }
                },
                "signalsToCollect": [
                    {
                        "name": collect_signal,
                        "dataPartitionId": 0,
                    },
                ],
                "storeAndForwardConfiguration": [
                    {
                        "storageOptions": {
                            "maximumSizeInBytes": 40000,
                            "storageLocation": partition_storage_location,
                            "minimumTimeToLiveInSeconds": 600,
                        },
                        "uploadOptions": {"conditionTree": f"{forward_signal} == 1.0"},
                    },
                ],
                "spoolingMode": "TO_DISK",
                "campaignArn": "store_and_forward" + str(i) + str(arn_counter),
            }
            store_and_forward_collection_schemes.append(store_and_forward)
        return store_and_forward_collection_schemes

    def add_some_ip_collection_schemes(self, arn_counter, random: Random):
        some_ip_collection_schemes = []
        # add condition based SOME IP campaigns
        for i in range(1, 4):
            some_ip_conditional = {
                "campaignSyncId": "conditional_some_ip_" + str(i) + str(arn_counter),
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "expression": f"Vehicle.ExampleSomeipInterface.X > {random.uniform(0,100)}",
                        "minimumTriggerIntervalMs": 5000,
                    }
                },
                "signalsToCollect": self.available_signals_some_ip,
            }
            some_ip_collection_schemes.append(some_ip_conditional)

        # add time based SOME IP campaigns
        for i in range(1, 4):
            some_ip_heartbeat = {
                "campaignSyncId": "heartbeat_some_ip_" + str(i) + str(arn_counter),
                "collectionScheme": {"timeBasedCollectionScheme": {"periodMs": 10000}},
                "signalsToCollect": self.available_signals_some_ip,
            }
            some_ip_collection_schemes.append(some_ip_heartbeat)

        return some_ip_collection_schemes

    def create_collection_schemes(self, arn_counter, random: Random):
        self.collection_schemes = []
        self.collection_schemes.extend(self.add_collection_schemes(arn_counter, random))
        self.collection_schemes.extend(self.add_fetch_collection_schemes(arn_counter, random))
        self.collection_schemes.extend(
            self.add_store_and_forward_collection_schemes(arn_counter, random)
        )
        self.collection_schemes.extend(self.add_some_ip_collection_schemes(arn_counter, random))

    def perform_shadow_operations(self, someip_device_shadow_editor):
        reported_temp = random.randint(10, 40)
        initial_shadow_document = {"state": {"reported": {"temperature": reported_temp}}}
        shadow_document_json = json.dumps(initial_shadow_document)
        someip_device_shadow_editor.update_shadow("test-shadow", shadow_document_json)

        new_shadow_document = {
            "state": {"reported": {"temperature": reported_temp}, "desired": {"temperature": 45}}
        }
        shadow_document_json = json.dumps(new_shadow_document)
        someip_device_shadow_editor.update_shadow("test-shadow", shadow_document_json)
        someip_device_shadow_editor.delete_shadow("test-shadow")

    def check_execute_command(
        self,
        actuator_name,
        get_value,
        value,
        signal_type,
        timeout: Optional[timedelta] = None,
        issued_time: Optional[datetime] = None,
        expected_value=None,
    ):
        timeout = timeout or timedelta(seconds=10)
        expected_value = expected_value if expected_value is not None else value
        command_id = str(random.getrandbits(16))
        self.context.send_actuator_command_request(
            command_id,
            actuator_name,
            value,
            signal_type,
            timeout,
            issued_time=issued_time,
        )

    def perform_commands_check(self, someipigen):
        self.check_execute_command(
            "Vehicle.actuator1",
            lambda cmd_id: someipigen.get_value("Int32"),
            123,
            SignalType.INT32,
        )
        self.check_execute_command(
            "Vehicle.actuator2",
            lambda cmd_id: someipigen.get_value("Int64"),
            456,
            SignalType.INT64,
        )
        self.check_execute_command(
            "Vehicle.actuator3",
            lambda cmd_id: someipigen.get_value("Boolean"),
            True,
            SignalType.BOOL,
        )
        self.check_execute_command(
            "Vehicle.actuator4",
            lambda cmd_id: someipigen.get_value("Float"),
            789.0,
            SignalType.FLOAT32,
        )
        self.check_execute_command(
            "Vehicle.actuator5",
            lambda cmd_id: someipigen.get_value("Double"),
            9999.0,
            SignalType.FLOAT64,
        )

    def send_command(
        self,
        *,
        state_template_sync_id: str,
        operation: LastKnownStateCommandOperation,
        deactivate_after_seconds: Optional[int] = None,
    ):
        self.command_count += 1
        command_id = f"command_{self.command_count}"
        len(self.context.received_command_responses) + 1
        self.context.send_last_known_state_command_request(
            command_id=command_id,
            state_template_sync_id=state_template_sync_id,
            operation=operation,
            deactivate_after_seconds=deactivate_after_seconds,
        )

    def send_lks_template(self):
        self.context.send_state_templates(
            state_templates_to_add=[
                StateTemplate(
                    sync_id="lks1",
                    signals=[
                        LastKnownStateSignalInformation(fqn="Engine_Airflow"),
                    ],
                    update_strategy=OnChangeUpdateStrategy(),
                ),
                StateTemplate(
                    sync_id="lks2",
                    signals=[
                        LastKnownStateSignalInformation(fqn="Parking_Brake_State"),
                    ],
                    update_strategy=PeriodicUpdateStrategy(5000),
                ),
            ]
        )

    def activate_and_test_on_change(self):
        deactivate_after_seconds = 8
        self.send_command(
            state_template_sync_id="lks1",
            operation=LastKnownStateCommandOperation.ACTIVATE,
            deactivate_after_seconds=deactivate_after_seconds,
        )
        time.sleep(1)
        self.context.set_can_signal("Engine_Airflow", self.random.uniform(0, 100))
        time.sleep(deactivate_after_seconds + 1)

    def activate_and_test_periodic(self):
        self.send_command(
            state_template_sync_id="lks2",
            operation=LastKnownStateCommandOperation.ACTIVATE,
        )
        time.sleep(15)  # Wait for a few periods
        self.send_command(
            state_template_sync_id="lks2",
            operation=LastKnownStateCommandOperation.DEACTIVATE,
        )

    def run_operations(self, someipigen, someip_device_shadow_editor):
        start_time = time.time()
        stop_threads = threading.Event()
        fetch_ctr = 0
        store_ctr = 0

        def run_command_operations():
            while (
                not stop_threads.is_set()
                and (time.time() - start_time) < timedelta(hours=25).total_seconds()
            ):
                self.perform_commands_check(someipigen)
                sleep_time = self.random.uniform(60, 300)
                time.sleep(sleep_time)

        def run_shadow_operations():
            while (
                not stop_threads.is_set()
                and (time.time() - start_time) < timedelta(hours=25).total_seconds()
            ):
                self.perform_shadow_operations(someip_device_shadow_editor)
                sleep_time = self.random.uniform(120, 480)
                time.sleep(sleep_time)

        def run_lks_operations():
            while (
                not stop_threads.is_set()
                and (time.time() - start_time) < timedelta(hours=25).total_seconds()
            ):
                self.activate_and_test_on_change()
                self.activate_and_test_periodic()
                sleep_time = self.random.uniform(180, 600)
                time.sleep(sleep_time)

        def run_campaign_updates():
            campaign_ctr = 0
            while (
                not stop_threads.is_set()
                and (time.time() - start_time) < timedelta(hours=25).total_seconds()
            ):
                campaign_ctr += 1
                self.create_collection_schemes(str(campaign_ctr), self.random)
                self.context.send_decoder_manifest()
                self.context.send_collection_schemes(self.collection_schemes)
                campaign_ctr += 1
                sleep_time = self.random.uniform(300, 600)
                time.sleep(sleep_time)

        threads = []
        operations = [
            run_command_operations,
            run_shadow_operations,
            run_lks_operations,
            run_campaign_updates,
        ]

        for operation in operations:
            thread = threading.Thread(target=operation)
            thread.daemon = True
            thread.start()
            threads.append(thread)

        while (time.time() - start_time) < timedelta(hours=25).total_seconds():
            self.context.set_can_signal("BPDAPS_BkPDrvApP", self.random.uniform(0, 9000))
            self.context.set_can_signal("Throttle__Position", self.random.uniform(0, 100))
            someipigen.set_value("Vehicle.ExampleSomeipInterface.X", int(random.uniform(0, 100)))

            fetch_ctr += 1
            if fetch_ctr >= 3:
                if self.random.choice([True, False]):
                    self.context.set_can_signal("Throttle Position", 0)
                fetch_ctr = 0

            store_ctr += 1
            if store_ctr >= 60000:
                self.context.set_can_signal(
                    "Electric_Park_Brake_Switch", self.random.choice([0, 1])
                )
                self.context.set_can_signal("Main_Light_Switch", self.random.choice([0, 1]))
                store_ctr = 0

            time.sleep(0.01)

        stop_threads.set()

    def test_load_test_all_features(self, someipigen, someip_device_shadow_editor):
        self.context.start_fwe()
        self.context.send_decoder_manifest()
        self.context.send_collection_schemes(self.collection_schemes)
        self.send_lks_template()

        for attempt in Retrying():
            with attempt:
                expected_documents = {"decoder_manifest_1", "lks1", "lks2"}
                for cs in self.collection_schemes:
                    expected_documents.add(cs["campaignSyncId"])
                self.context.verify_checkin_documents(expected_documents)

        self.run_operations(someipigen, someip_device_shadow_editor)
