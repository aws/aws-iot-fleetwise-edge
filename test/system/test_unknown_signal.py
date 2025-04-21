# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import logging
from pathlib import Path

import pytest
from testframework.common import Retrying
from testframework.context import Context

log = logging.getLogger(__name__)


@pytest.mark.parametrize(
    "setup",
    [
        {"use_faketime": True},
    ],
    indirect=True,
    ids=str,
)
class TestUnknownSignalHandling:
    @pytest.fixture(autouse=True, params=[{}])
    def setup(self, tmp_path: Path, worker_number: int):
        self.context = Context(
            tmp_path=tmp_path,
            worker_number=worker_number,
            persistency_file=True,
            use_uds_dtc_generic_collection=True,
        )
        self.context.start_cyclic_can_messages()

        log.info("Setting initial values")
        self.context.set_can_signal("Engine_Airflow", 0.0)
        self.context.set_can_signal("Parking_Brake_State", 0)
        self.context.set_obd_signal("ENGINE_SPEED", 1000.0)
        self.context.set_obd_signal("ENGINE_OIL_TEMPERATURE", 98.99)
        self.context.set_obd_signal("CONTROL_MODULE_VOLTAGE", 14.5)

        self.context.set_dtc("ECM_DTC3", 0xAF)

        self.context.connect_to_cloud()

        yield  # pytest fixture: before is setup, after is tear-down

        self.context.stop_fwe()
        # Stop canigen after FWE to prevent OBD errors in the log:
        self.context.stop_cyclic_can_messages()
        self.context.destroy_aws_resources()

        log.info("Expect 0 errors")
        assert self.context.log_errors == []

    def test_unknown_signal_to_collect(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()

        # Create the first collection scheme with time-based collection
        collection_schemes = [
            {
                "campaignSyncId": "unknown_signal_to_collect",
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

        self.context.proto_factory.create_collection_schemes_proto(collection_schemes)

        # Add the "UNKNOWN" signal to the first collection scheme
        extra_collected_signal = (
            self.context.proto_factory.collection_schemes_proto.collection_schemes[
                0
            ].signal_information.add()
        )
        extra_collected_signal.signal_id = 123456  # Use a unique signal ID for the "UNKNOWN" signal
        extra_collected_signal.sample_buffer_size = 750
        extra_collected_signal.minimum_sample_period_ms = 0
        extra_collected_signal.fixed_window_period_ms = 0

        # Send the modified CollectionSchemes Protobuf object
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

        log.info("Wait for two triggers with interval 5")

        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) >= 2
                # Verify the collection and processing of other valid signals
                tire_rr_prs_signals = self.context.received_data[
                    -1
                ].get_signal_values_with_timestamps("TireRRPrs")
                assert len(tire_rr_prs_signals) >= 1
                assert (
                    tire_rr_prs_signals[0].value is not None
                )  # Verify the signal value is not None

                parking_brake_state_signals = self.context.received_data[
                    -1
                ].get_signal_values_with_timestamps("Parking_Brake_State")
                assert len(parking_brake_state_signals) >= 1
                assert parking_brake_state_signals[0].value is not None

    def test_unknown_signal_in_expression(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()

        # Create the second collection scheme with condition-based collection
        collection_schemes = [
            {
                "campaignSyncId": "valid_expression",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "minimumTriggerIntervalMs": 1000,
                        "expression": "TireRRPrs > 10",
                    }
                },
                "signalsToCollect": [
                    {"name": "TireRRPrs"},
                    {"name": "Parking_Brake_State"},
                    {"name": "Throttle__Position"},
                    {"name": "ENGINE_SPEED"},
                    {"name": "ENGINE_OIL_TEMPERATURE"},
                    {"name": "CONTROL_MODULE_VOLTAGE"},
                ],
            },
            {
                "campaignSyncId": "unknown_signal_in_expression",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "minimumTriggerIntervalMs": 1000,
                        "expression": "TireRRPrs > 10",
                    }
                },
                "signalsToCollect": [
                    {"name": "TireRRPrs"},
                    {"name": "Parking_Brake_State"},
                    {"name": "Throttle__Position"},
                    {"name": "ENGINE_SPEED"},
                    {"name": "ENGINE_OIL_TEMPERATURE"},
                    {"name": "CONTROL_MODULE_VOLTAGE"},
                ],
            },
        ]

        self.context.proto_factory.create_collection_schemes_proto(collection_schemes)

        # Adding expression with unknown signal
        collection_scheme = self.context.proto_factory.collection_schemes_proto.collection_schemes[
            1
        ]
        condition_tree = collection_scheme.condition_based_collection_scheme.condition_tree
        condition_node = condition_tree.node_operator.left_child
        condition_node.node_signal_id = 123456  # Using "UNKNOWN" signal in condition tree
        collected_signal = collection_scheme.signal_information.add()
        collected_signal.signal_id = condition_node.node_signal_id
        collected_signal.sample_buffer_size = 750
        collected_signal.minimum_sample_period_ms = 0
        collected_signal.fixed_window_period_ms = 0
        collected_signal.condition_only_signal = True

        # Send the modified CollectionSchemes Protobuf object
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
                        collection_schemes[1]["campaignSyncId"],
                    }
                )

        # Verify that data is received just for the valid campaign
        self.context.set_can_signal("TireRRPrs", 20.0)
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) >= 2
                for received_data in self.context.received_data:
                    campaign_sync_id = received_data.campaign_sync_id
                    assert campaign_sync_id == collection_schemes[0]["campaignSyncId"]

                # Verify the collection and processing of other valid signals for the valid campaign
                tire_rr_prs_signals = self.context.received_data[
                    0
                ].get_signal_values_with_timestamps("TireRRPrs")
                assert len(tire_rr_prs_signals) >= 1
                assert (
                    tire_rr_prs_signals[0].value is not None
                )  # Verify the signal value is not None

                parking_brake_state_signals = self.context.received_data[
                    0
                ].get_signal_values_with_timestamps("Parking_Brake_State")
                assert len(parking_brake_state_signals) >= 1
                assert parking_brake_state_signals[0].value is not None

    def test_unknown_signal_in_fetch_expression(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()

        # Campaign 1: Campaign with Fetch expression containing a signal not in DM
        # Campaign 2: Campaign with signal in condition expression being fetched
        # but the fetch expression contains a signal not in DM
        collection_schemes = [
            {
                "campaignSyncId": "unknown_signal_in_fetch_expression_1",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "minimumTriggerIntervalMs": 1000,
                        "expression": "TireRRPrs > 0",
                    }
                },
                "signalsToCollect": [
                    {"name": "TireRRPrs"},
                    {"name": "Parking_Brake_State"},
                    {"name": "Throttle__Position"},
                    {"name": "ENGINE_SPEED"},
                    {"name": "ENGINE_OIL_TEMPERATURE"},
                ],
                "signalsToFetch": [
                    {
                        "fullyQualifiedName": "Vehicle.ECU1.DTC_INFO",
                        "signalFetchConfig": {
                            "conditionBased": {
                                "conditionExpression": "Throttle__Position > 10",
                                "triggerMode": "ALWAYS",
                            }
                        },
                        "actions": ['custom_function("DTC_QUERY", -1, 4, -1)'],
                    }
                ],
            },
            {
                "campaignSyncId": "unknown_signal_in_fetch_expression_2",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "minimumTriggerIntervalMs": 1000,
                        "expression": "Vehicle.ECU1.DTC_INFO > 10",
                    }
                },
                "signalsToCollect": [
                    {"name": "TireRRPrs"},
                    {"name": "Parking_Brake_State"},
                    {"name": "Throttle__Position"},
                    {"name": "ENGINE_SPEED"},
                    {"name": "ENGINE_OIL_TEMPERATURE"},
                    {"name": "Vehicle.ECU1.DTC_INFO"},
                ],
                "signalsToFetch": [
                    {
                        "fullyQualifiedName": "Vehicle.ECU1.DTC_INFO",
                        "signalFetchConfig": {
                            "conditionBased": {
                                "conditionExpression": "Throttle__Position > 10",
                                "triggerMode": "ALWAYS",
                            }
                        },
                        "actions": ['custom_function("DTC_QUERY", -1, 4, -1)'],
                    }
                ],
            },
        ]

        self.context.proto_factory.create_collection_schemes_proto(collection_schemes)

        # Adding fetch expression with unknown signal to both campaigns
        signal_fetch_info_1 = (
            self.context.proto_factory.collection_schemes_proto.collection_schemes[
                0
            ].signal_fetch_information[0]
        )

        condition_tree = signal_fetch_info_1.condition_based.condition_tree
        condition_node = condition_tree.node_operator.left_child
        condition_node.node_signal_id = 123456  # Using "UNKNOWN" signal in condition tree

        signal_fetch_info_2 = (
            self.context.proto_factory.collection_schemes_proto.collection_schemes[
                1
            ].signal_fetch_information[0]
        )
        condition_tree = signal_fetch_info_2.condition_based.condition_tree
        condition_node = condition_tree.node_operator.left_child
        condition_node.node_signal_id = 123456  # Using "UNKNOWN" signal in condition tree

        # Send the modified CollectionSchemes Protobuf object
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
                        collection_schemes[1]["campaignSyncId"],
                    }
                )

        self.context.set_can_signal("TireRRPrs", 10)

        # All signals except the signal to fetch should be collected for campaign 1
        # No signals will be collected for campaign 2
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) >= 2

                for received_data in self.context.received_data:
                    campaign_sync_id = received_data.campaign_sync_id
                    assert campaign_sync_id == collection_schemes[0]["campaignSyncId"]
                # Verify the collection and processing of other valid signals
                tire_rr_prs_signals = self.context.received_data[
                    -1
                ].get_signal_values_with_timestamps("TireRRPrs")
                assert len(tire_rr_prs_signals) >= 1
                assert (
                    tire_rr_prs_signals[0].value is not None
                )  # Verify the signal value is not None

                parking_brake_state_signals = self.context.received_data[
                    0
                ].get_signal_values_with_timestamps("Parking_Brake_State")
                assert len(parking_brake_state_signals) >= 1
                assert parking_brake_state_signals[0].value is not None

    def test_unknown_signal_in_fetch_and_condition_expression(self):
        self.context.start_fwe()
        self.context.send_decoder_manifest()

        # Campaign 1: Signal to be fetched is an unknown signal, and used in condition expression
        # Campaign 2: Valid campaign
        collection_schemes = [
            {
                "campaignSyncId": "invalid_campaign",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "minimumTriggerIntervalMs": 1000,
                        "expression": "TireRRPrs > 0",
                    }
                },
                "signalsToCollect": [
                    {"name": "TireRRPrs"},
                    {"name": "Parking_Brake_State"},
                    {"name": "Throttle__Position"},
                    {"name": "ENGINE_SPEED"},
                    {"name": "ENGINE_OIL_TEMPERATURE"},
                    {"name": "Vehicle.ECU1.DTC_INFO"},
                ],
                "signalsToFetch": [
                    {
                        "fullyQualifiedName": "Vehicle.ECU1.DTC_INFO",
                        "signalFetchConfig": {
                            "timeBased": {
                                "executionFrequencyMs": 1000,
                            }
                        },
                        "actions": ['custom_function("DTC_QUERY", -1, 4, -1)'],
                    }
                ],
            },
            {
                "campaignSyncId": "valid_campaign",
                "collectionScheme": {
                    "conditionBasedCollectionScheme": {
                        "minimumTriggerIntervalMs": 1000,
                        "expression": "TireRRPrs > 0",
                    }
                },
                "signalsToCollect": [
                    {"name": "TireRRPrs"},
                    {"name": "Parking_Brake_State"},
                    {"name": "Throttle__Position"},
                    {"name": "ENGINE_SPEED"},
                    {"name": "ENGINE_OIL_TEMPERATURE"},
                    {"name": "Vehicle.ECU1.DTC_INFO"},
                ],
                "signalsToFetch": [
                    {
                        "fullyQualifiedName": "Vehicle.ECU1.DTC_INFO",
                        "signalFetchConfig": {
                            "timeBased": {
                                "executionFrequencyMs": 1000,
                            }
                        },
                        "actions": ['custom_function("DTC_QUERY", -1, 4, -1)'],
                    }
                ],
            },
        ]
        self.context.proto_factory.create_collection_schemes_proto(collection_schemes)

        condition_tree = self.context.proto_factory.collection_schemes_proto.collection_schemes[
            0
        ].condition_based_collection_scheme.condition_tree
        condition_node = condition_tree.node_operator.left_child
        condition_node.node_signal_id = 123456  # Using "UNKNOWN" signal in condition tree

        signal_fetch_info_1 = (
            self.context.proto_factory.collection_schemes_proto.collection_schemes[
                0
            ].signal_fetch_information[0]
        )
        signal_fetch_info_1.signal_id = 123456

        signal_fetch_info_1.condition_based.condition_trigger_mode = (
            self.context.proto_factory.get_trigger_mode("ALWAYS")
        )

        self.context.send_collection_schemes_proto(
            self.context.proto_factory.collection_schemes_proto
        )

        self.context.set_can_signal("TireRRPrs", 10)

        # Unknown signal cannot be fetched, hence the condition expression
        # will never be met and no data will be collected for campaign 1
        # Data will be collected only for the valid campaign
        for attempt in Retrying():
            with attempt:
                assert len(self.context.received_data) >= 2

                for received_data in self.context.received_data:
                    campaign_sync_id = received_data.campaign_sync_id
                    assert campaign_sync_id == collection_schemes[1]["campaignSyncId"]
                # Verify the collection and processing of other valid signals
                tire_rr_prs_signals = self.context.received_data[
                    -1
                ].get_signal_values_with_timestamps("TireRRPrs")
                assert len(tire_rr_prs_signals) >= 1
                assert (
                    tire_rr_prs_signals[0].value is not None
                )  # Verify the signal value is not None

                parking_brake_state_signals = self.context.received_data[
                    -1
                ].get_signal_values_with_timestamps("Parking_Brake_State")
                assert len(parking_brake_state_signals) >= 1

                dtc_signals = self.context.received_data[-1].get_signal_values_with_timestamps(
                    "Vehicle.ECU1.DTC_INFO"
                )
                assert len(dtc_signals) > 0
